package main

import (
	"flag"
	"fmt"
	"github.peak6.net/platform/gocore.git/commander"
	"github.peak6.net/platform/gocore.git/mmd"
	"gopkg.in/fsnotify.v1"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
)

type filePattern []*regexp.Regexp

type PublisherInstance struct {
	name      string
	commander *commander.Command
}

var stop = make(chan struct{})
var env string
var execPath = getExecDir()
var sourceVersion = "<DEV>"
var filePatternFlag filePattern = makeFilePattern("feed.*")
var publishers = make(map[string]*commander.Command)
var heartBeatMS = 1000
var maxIdle = 2
var loopback = true
var udpStatsAddr = "127.0.0.1:9411"
var defaultDirectory = "/dev/shm/"
var restartOnExit bool

func (fp *filePattern) String() string {
	return fmt.Sprint([]*regexp.Regexp(*fp))
}

func (fp *filePattern) Set(v string) error {
	*fp = append(*fp, regexp.MustCompile(v))
	return nil
}

func (pi *PublisherInstance) String() string {
	return pi.commander.String()
}

func makeFilePattern(pattern string) filePattern {
	aFilePattern := filePattern{}
	return append(aFilePattern, regexp.MustCompile(pattern))
}

func init() {
	var err error
	if env, err = mmd.LookupEnvironment(); err != nil {
		log.Fatal(err)
	}

	flag.Usage = usage
	flag.StringVar(&udpStatsAddr, "stats", udpStatsAddr, "UDP address to publish stats to")
	flag.Var(&filePatternFlag, "fp", "Pattern to match for files")
	flag.IntVar(&heartBeatMS, "heartbeat", heartBeatMS, "Heartbeat interval (in ms)")
	flag.IntVar(&maxIdle, "maxidle", maxIdle, "Maximum idle time (in ms) before sending a partial packet")
	flag.BoolVar(&loopback, "loopback", loopback, "Enable multicast loopback")
	flag.StringVar(&env, "env", env, "Environment to match against feed environment (default: local MMD environment)")
	flag.StringVar(&execPath, "path", execPath, "Path to Cachester executables")
	flag.BoolVar(&restartOnExit, "restart", true, "Restart publisher instances when they exit")
}

func getExecDir() string {
	dir, _ := filepath.Split(os.Args[0])
	if dir != "" {
		strconv.AppendQuoteRune([]byte(dir), filepath.Separator)
	}

	return dir
}

func usage() {
	v, err := exec.Command(execPath + "publisher", "-v").Output()
	var publisherVersion string
	if err != nil {
		publisherVersion = "Error, can't find Publisher at: " + execPath
	} else {
		publisherVersion = strings.TrimSpace(string(v))
	}

	fmt.Fprintln(os.Stderr, ""+
		"        Source: "+sourceVersion+
		"\n     Publisher: "+publisherVersion+
		"\n\n Usage: "+os.Args[0]+" [flags] DIR1 [DIR2...]")

	flag.PrintDefaults()
	os.Exit(1)
}

func main() {
	var err error
	flag.Parse()
	log.Println("Patterns:", filePatternFlag)

	if _, err = os.Stat(execPath + "publisher"); err != nil {
		log.Fatalln(err)
	}

	commander.SetDefaultLogger(log.New(os.Stderr, log.Prefix(), log.Flags()))

	err = discoveryLoop()
	if err != nil {
		log.Fatal(err)
	}

	log.Println("Done")
}

func matchPattern(s string) bool {
	for _, p := range filePatternFlag {
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

			if info.IsDir() || !matchPattern(path) {
				return nil
			}

			startIfNeeded(path)
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
					log.Println("Event:", event)
				}
				break
			}
		case errMsg := <-watcher.Errors:
			log.Println("error:", errMsg)
		}
	}

	return nil
}

func startIfNeeded(path string) {
	name := filepath.Base(path)
	_, ok := publishers[name]
	if ok {
		log.Println("Already publishing:", name)
	} else {
		publishers[name] = nil
		addr, err := mcastAddrFor(name)
		if err != nil {
			log.Println(err)
			return
		}

		log.Println("Starting publisher for:", name, "on", addr)

		var state struct {
			port int
		}

		cmd, err := commander.New(execPath + "publisher")
		if err != nil {
			log.Fatal(err)
		}

		cmd.Name = "publisher(" + name + ")"

		cmd.BeforeStart = func(c *commander.Command) error {
			state.port, err = reservePort()
			if err != nil {
				return err
			}

			log.Println("Reserving port", state.port, "for", name)

			opts := []string{
				"-j",
				"-a", advertAddr,
				"-i", mcastInterface,
				"-e", env,
				"-p", name}

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
				fmt.Sprint(heartBeatMS*1000),
				fmt.Sprint(maxIdle*1000),
			)

			return nil
		}

		cmd.AfterStop = func(*commander.Command) error {
			releasePort(state.port)
			state.port = 0
			return nil
		}

		log.Println("CMD:", cmd)
		go cmd.Run()
	}
}

func chkFatal(err error) {
	if err != nil {
		log.Fatal(err)
	}
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
