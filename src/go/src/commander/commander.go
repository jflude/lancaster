/*
   Copyright (c)2014-2017 Peak6 Investments, LP.
   Use of this source code is governed by the COPYING file.
*/

package commander

import (
	"errors"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"sync"
	"sync/atomic"
	"syscall"
	"time"
)

type Command struct {
	id                     int64
	App                    string
	Args                   []string
	BeforeStart            func(cmd *Command) error
	AfterStart             func(cmd *Command) error
	AfterStop              func(cmd *Command) error
	Env                    map[string]string
	Name                   string
	Cmd                    *exec.Cmd
	Stdin                  io.Reader
	Stdout                 io.Writer
	Stderr                 io.Writer
	Pid                    int
	LaunchCount            int
	AutoRestart            bool
	DeathSignal            syscall.Signal
	MinTimeBetweenRestarts time.Duration
	LastRunTime            time.Duration
	LastProcessState       *os.ProcessState
	LastExitCode           error
	Failure                error // Failure marks this object as unrunnable
	startTime              time.Time
	stopped                chan struct{}
}

type ArgsProducer func(command *Command) []string

type commandGroup struct {
	entries  map[int64]*Command
	lock     sync.Mutex
	shutdown chan struct{}
}

var NotRunning = errors.New("Not running")
var entryId int64

var defaultStdin io.Reader = os.Stdin
var defaultStdout io.Writer = os.Stdout
var defaultStderr io.Writer = os.Stderr

func New(app string, stdin io.Reader, stdout, stderr io.Writer, args ...string) (*Command, error) {
	if _, err := exec.LookPath(app); err != nil {
		return nil, err
	}

	return &Command{
		id:                     atomic.AddInt64(&entryId, 1),
		App:                    app,
		Name:                   app,
		Args:                   args,
		AutoRestart:            false,
		stopped:                make(chan struct{}),
		Env:                    make(map[string]string),
		DeathSignal:            syscall.SIGKILL,
		MinTimeBetweenRestarts: 5 * time.Second,
		Stdin:  stdin,
		Stdout: stdout,
		Stderr: stderr,
	}, nil
}

func NewGroup() *commandGroup {
	return &commandGroup{
		entries:  make(map[int64]*Command),
		shutdown: make(chan struct{}),
	}
}

func (cg *commandGroup) New(app string, stdin io.Reader, stdout, stderr io.Writer, args ...string) (*Command, error) {
	c, err := New(app, stdin, stdout, stderr, args...)
	if err != nil {
		return nil, err
	}

	cg.lock.Lock()
	cg.entries[c.id] = c
	cg.lock.Unlock()
	return c, err
}

func SetStdin(stream io.Reader) {
	defaultStdin = stream
}

func SetStdout(stream io.Writer) {
	defaultStdout = stream
}

func SetStderr(stream io.Writer) {
	defaultStderr = stream
}

func (cg *commandGroup) WaitForShutdown() {
	<-cg.shutdown
}

func (cg *commandGroup) StopWithSignal(sig syscall.Signal) {
	defer close(cg.shutdown)
	cg.lock.Lock()

	wg := &sync.WaitGroup{}
	for _, c := range cg.entries {
		wg.Add(1)
		delete(cg.entries, c.id)

		go func(c *Command) {
			c.StopWithSignal(sig)
			defer wg.Done()
		}(c)
	}

	cg.lock.Unlock()
}

func (c *Command) String() string {
	cmd := c.Cmd
	if cmd != nil {
		return fmt.Sprintf("%s (pid: %d, uptime: %s)", c.Name, cmd.Process.Pid, c.Uptime())
	} else if c.LastProcessState != nil {
		pid := c.LastProcessState.Pid()
		return fmt.Sprintf("%s (stopped, was pid: %d, runtime: %s)", c.Name, pid, c.LastRunTime)
	} else {
		return fmt.Sprintf("%s (not started)", c.Name)
	}
}

func (c *Command) Stop() error {
	return c.StopWithSignal(syscall.SIGTERM)
}

func (c *Command) StopWithSignal(sig syscall.Signal) error {
	c.AutoRestart = false
	if c.Cmd == nil {
		return NotRunning
	}

	log.Println("Sending signal:", sig, "to:", c)
	c.SendSignal(sig)
	<-c.stopped
	return nil
}

func (c *Command) SendSignal(sig syscall.Signal) error {
	if c.Cmd == nil {
		return NotRunning
	}

	return c.Cmd.Process.Signal(sig)
}

func (c *Command) StartTime() (time.Time, error) {
	if c.Cmd != nil {
		return c.startTime, nil
	}

	return time.Time{}, NotRunning
}

func (c *Command) Uptime() time.Duration {
	if c.Cmd != nil {
		return time.Now().Sub(c.startTime)
	} else {
		return 0
	}
}

func (c *Command) initCmd() error {
	cmd := exec.Command(c.App, c.Args...)
	c.initSysProcAttr(cmd)
	if len(c.Env) == 0 {
		cmd.Env = os.Environ()
	} else {
		env := make([]string, 0, len(c.Env))
		for k, v := range c.Env {
			env = append(env, k+"="+v)
		}
		cmd.Env = env
	}

	if c.Stdin != nil {
		cmd.Stdin = c.Stdin
	} else {
		cmd.Stdin = defaultStdin
	}
	if c.Stdout != nil {
		cmd.Stdout = c.Stdout
	} else {
		cmd.Stdout = defaultStdout
	}
	if c.Stderr != nil {
		cmd.Stderr = c.Stderr
	} else {
		cmd.Stderr = defaultStderr
	}

	cmd.Env = append(cmd.Env, fmt.Sprintf("LAUNCH_COUNT=%d", c.LaunchCount))
	c.Cmd = cmd
	return nil
}

func (c *Command) Run() error {
	defer func() { c.Cmd = nil }()
	var err error
	for c.AutoRestart || c.LaunchCount == 0 {
		if c.LastRunTime > 0 && c.LastRunTime < c.MinTimeBetweenRestarts {
			st := c.MinTimeBetweenRestarts - c.LastRunTime
			log.Println("Process:", c, "only ran for:", c.LastRunTime, "sleeping:", st)
			time.Sleep(st)
		}

		c.LaunchCount++
		c.startTime = time.Now()
		if c.BeforeStart != nil {
			err = c.BeforeStart(c)
			if err != nil {
				log.Println("BeforeStart error:", err)
				c.Failure = err
				break
			}
		}

		c.initCmd()
		err := c.Cmd.Start()
		if err != nil {
			log.Println("Error starting:", c, err)
			c.Failure = err
			return err
		}

		c.Pid = c.Cmd.Process.Pid
		log.Println("Started:", c, "command line:", c.Cmd.Args)

		if c.AfterStart != nil {
			err = c.AfterStart(c)
			if err != nil {
				log.Println("AfterStart error:", err)
				c.Failure = err

				c.Cmd.Process.Kill()
				c.Cmd.Wait()
				c.LastProcessState = c.Cmd.ProcessState
				c.Cmd = nil

				log.Println("Killed:", err, c, c.LastProcessState)

				if c.AfterStop != nil {
					err = c.AfterStop(c)
					if err != nil {
						log.Println("AfterStop error:", err)
					}
				}

				break
			}
		}

		err = c.Cmd.Wait()
		c.LastProcessState = c.Cmd.ProcessState
		c.Cmd = nil

		log.Println("Exited:", err, c, c.LastProcessState)
		end := time.Now()
		c.LastRunTime = end.Sub(c.startTime)

		if c.AfterStop != nil {
			err = c.AfterStop(c)
			if err != nil {
				log.Println("AfterStop error:", err)
				c.Failure = err
				break
			}
		}
	}

	log.Println("Exiting:", c)
	close(c.stopped)
	return c.Failure
}
