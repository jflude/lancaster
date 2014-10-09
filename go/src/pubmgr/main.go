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
var run = true
var publisherPath = "../publisher"
var sourceVersion = "<DEV>"
var filePatternFlag filePattern
var publishers = make(map[string]*commander.Command)
var heartBeatMS = 500
var maxIdle = 2
var loopback = true
var udpStatsAddr = "none"

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

func init() {
	var err error
	if env, err = mmd.LookupEnvironment(); err != nil {
		log.Fatal(err)
	}
        flag.StringVar(&udpStatsAddr, "us", udpStatsAddr, "Publish stats to udp address")
	flag.Var(&filePatternFlag, "fp", "Pattern to match for files")
	flag.IntVar(&heartBeatMS, "heartbeat", heartBeatMS, "Heartbeat interval (in millis)")
	flag.IntVar(&maxIdle, "maxidle", maxIdle, "Maximum idle time (in ms) before sending a partial packet")
	flag.BoolVar(&loopback, "loopback", loopback, "Enable multicast loopback")
	flag.Usage = usage
	flag.StringVar(&env, "env", env, "Environment to match against feed environment. Defaults to local MMD environment.")
	flag.StringVar(&publisherPath, "pub", publisherPath, "Path to publisher exeutable")
}

func usage() {
	v, err := exec.Command(publisherPath, "-v").Output()
	var publisherVersion string
	if err != nil {
		publisherVersion = "Error, can't find Publisher at: " + publisherPath
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
	if _, err = os.Stat(publisherPath); err != nil {
		log.Fatalln(err)
	}
	commander.SetDefaultLogger(log.New(os.Stderr, log.Prefix(), log.Flags()))
	commander.SetDefaultStdIO(nil, os.Stderr, os.Stdout)
	err = discoveryLoop()
	if err != nil {
		log.Fatal(err)
	}
	log.Println("Done")
}

func discoveryLoop() error {
	pmatch := func(s string) bool {
		for _, p := range filePatternFlag {
			if p.MatchString(s) {
				return true
			}
		}
		return false
	}

	watcher, err := fsnotify.NewWatcher()
	if err != nil {
		return err
	}
	defer watcher.Close()
	for _, d := range flag.Args() {
		filepath.Walk(d, func(path string, info os.FileInfo, err error) error {
			if err != nil {
				return err
			}
			if info.IsDir() || !pmatch(path) {
				return nil
			}
			startIfNeeded(path)
			return nil
		})
		err = watcher.Add(d)
		if err != nil {
			return err
		}
	}
	for {
		select {
		case event := <-watcher.Events:
			if pmatch(event.Name) {
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
	// log.Println("New Feed:", desc, ", from:", from, "disc:", jsstr)
	// si := &PublisherInstance{name: desc, discovery: disc}
	// feeds[desc] = si
	// go si.run()
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

		cmd, err := commander.New(publisherPath)
		if err != nil {
			log.Fatal(err)
		}
		cmd.BeforeStart = func(*commander.Command) error {
			state.port, err = reservePort()
			if err != nil {
				return err
			}
			log.Println("Reserving port", state.port, "for", name)
			opts := []string{
				"-j",             // show stats in JSON format
				"-a", advertAddr, // advertize publisher presence
				"-i", clientInterface, // mcast interface
				"-e", env,
				"-p", name} // error message prefix
			if loopback {
				opts = append(opts, "-l")
			}
			cmd.Args = append(opts,
				path,
				listenAddress+":"+strconv.Itoa(state.port), // tcp addr
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

func (pi *PublisherInstance) run() {
	var err error
	pi.commander, err = commander.New("../../cachester/Publisher",
		"-j",
	)
	pi.commander.Name = pi.name
	if err != nil {
		log.Fatalln("Failed to create commander for:", pi, "error:", err)
	}
        if udpStatsAddr != "" && udpStatsAddr != "none" {
		pi.commander.Env["UDP_STATS_URL"] = udpStatsAddr
	}
	pi.commander.AutoRestart = false
	err = pi.commander.Run()
	log.Println("Commander for:", pi, "exited:", err)
}

func chkFatal(err error) {
	if err != nil {
		log.Fatal(err)
	}
}
