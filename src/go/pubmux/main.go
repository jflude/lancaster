/*
   Copyright (c)2014-2017 Peak6 Investments, LP.
   Use of this source code is governed by the COPYING file.
*/

package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"time"

	"github.com/peak6/lancaster/commander"

	"github.com/fsnotify/fsnotify"
)

type Publisher struct {
	name      string
	commander *commander.Command
}

type StoragePattern []*regexp.Regexp

var (
	env              = "test"
	execPath         = getExecDir()
	storePattern     = StoragePattern{}
	publishers       = make(map[string]*commander.Command)
	heartBeatMS      = 1000
	maxAgeMS         = 2
	orphanTimeoutMS  = 3000
	pauseSecs        int
	loopback         = true
	udpStatsAddr     = "127.0.0.1:9411"
	defaultDirectory = "/dev/shm/"
	restartOnExit    bool
)

func (fp *StoragePattern) String() string {
	return fmt.Sprint([]*regexp.Regexp(*fp))
}

func (fp *StoragePattern) Set(v string) error {
	*fp = append(*fp, regexp.MustCompile(v))
	return nil
}

func (pi *Publisher) String() string {
	return pi.commander.String()
}

func init() {
	log.SetPrefix("submgr: ")

	flag.StringVar(&udpStatsAddr, "stats", udpStatsAddr, "UDP address to publish stats to")
	flag.Var(&storePattern, "store", "Pattern(s) to match for storages to be published")
	flag.IntVar(&heartBeatMS, "heartbeat", heartBeatMS, "Heartbeat interval (in ms)")
	flag.IntVar(&maxAgeMS, "age", maxAgeMS, "Maximum packet age (in ms) allowed before sending a partial packet")
	flag.IntVar(&orphanTimeoutMS, "orphan", orphanTimeoutMS, "Maximum time (in ms) allowed between storage touches")
	flag.BoolVar(&loopback, "loopback", loopback, "Enable loopback of multicast data and adverts")
	flag.StringVar(&env, "env", env, "Environment to associate with publishers")
	flag.StringVar(&execPath, "path", execPath, "Path to Lancaster executables")
	flag.BoolVar(&restartOnExit, "restart", true, "Restart publishers when they exit")
	flag.IntVar(&pauseSecs, "pause", 0, "Number of seconds to pause between publisher launches")

	flag.Parse()
}

func getExecDir() string {
	dir, _ := filepath.Split(os.Args[0])
	if dir != "" {
		strconv.AppendQuoteRune([]byte(dir), filepath.Separator)
	}

	return dir
}

func matchPattern(s string) bool {
	for _, p := range storePattern {
		if p.MatchString(s) {
			return true
		}
	}

	return false
}

func discoveryLoop() error {
	watcher, err := fsnotify.NewWatcher()
	if err != nil {
		return err
	}

	defer watcher.Close()

	filePaths := flag.Args()
	if len(filePaths) == 0 {
		filePaths = make([]string, 1)
		filePaths[0] = defaultDirectory
	}

	for _, filePath := range filePaths {
		filepath.Walk(filePath, func(path string, info os.FileInfo, err error) error {
			if err != nil {
				return err
			}

			if !info.IsDir() && matchPattern(path) {
				startIfNeeded(path)
			}

			return nil
		})

		err = watcher.Add(filePath)
		if err != nil {
			return err
		}
	}

	for {
		select {
		case event := <-watcher.Events:
			if matchPattern(event.Name) {
				switch event.Op {
				case fsnotify.Create, fsnotify.Write:
					startIfNeeded(event.Name)
				default:
					log.Println("event:", event)
				}
			}
		case errMsg := <-watcher.Errors:
			log.Println(errMsg)
		}
	}
}

func startIfNeeded(path string) {
	name := filepath.Base(path)
	_, ok := publishers[name]
	if ok {
		log.Println("already publishing: ", name)
		return
	}

	publishers[name] = nil
	addr, err := getMcastAddrFor(name)
	if err != nil {
		log.Println(err)
		return
	}

	log.Println("starting publisher for: ", name, " on: ", addr)

	cmd, err := commander.New(execPath+"publisher", nil, nil, nil)
	if err != nil {
		log.Fatalln(err)
	}

	var state struct{ port int }

	cmd.Name = "publisher(" + name + ")"
	cmd.AutoRestart = restartOnExit

	cmd.BeforeStart = func(c *commander.Command) error {
		state.port, err = reservePort()
		if err != nil {
			return err
		}

		log.Println("reserving port", state.port, "for", name)

		opts := []string{
			"-p", name,
			"-e", env,
			"-i", dataInterface,
			"-a", advertAddr,
			"-I", advertInterface,
			"-P", fmt.Sprint(maxAgeMS * 1000),
			"-H", fmt.Sprint(heartBeatMS * 1000),
			"-O", fmt.Sprint(orphanTimeoutMS * 1000),
			"-L",
			"-j"}

		if loopback {
			opts = append(opts, "-l")
		}

		if udpStatsAddr != "" {
			opts = append(opts, "-S", udpStatsAddr)
		}

		c.Args = append(opts,
			path,
			listenAddress+":"+strconv.Itoa(state.port),
			addr+":"+strconv.Itoa(state.port),
		)

		return nil
	}

	cmd.AfterStop = func(*commander.Command) error {
		releasePort(state.port)
		state.port = 0
		return nil
	}

	log.Println("new publisher:", cmd)
	go cmd.Run()

	time.Sleep(time.Duration(pauseSecs) * time.Second)
}

func fileExists(path string) (bool, error) {
	_, err := os.Stat(path)
	if err == nil {
		return true, nil
	}

	if os.IsNotExist(err) {
		return false, nil
	}

	return false, err
}

func main() {
	if len(storePattern) == 0 {
		log.Fatalln("no store pattern specified")
	}

	log.Println("store pattern: ", storePattern)

	var err error
	err = initializePostFlagsParsed()
	if err != nil {
		log.Fatalln(err)
	}

	if _, err = os.Stat(execPath + "publisher"); err != nil {
		log.Fatalln(err)
	}

	err = discoveryLoop()
	if err != nil {
		log.Fatalln(err)
	}

	log.Println("Done")
}
