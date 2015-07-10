package main

import (
	"flag"
	"fmt"
	"github.peak6.net/platform/gocore.git/appinfo"
	"github.peak6.net/platform/gocore.git/commander"
	"github.peak6.net/platform/gocore.git/mmd"
	"github.peak6.net/platform/gocore.git/logger"
	"github.com/go-fsnotify/fsnotify"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"time"
)

type StoragePattern []*regexp.Regexp

type Publisher struct {
	name      string
	commander *commander.Command
}

var stop = make(chan struct{})
var env string
var execPath = getExecDir()
var sourceVersion = "<DEV>"
var storePattern StoragePattern = StoragePattern{}
var publishers = make(map[string]*commander.Command)
var heartBeatMS = 1000
var maxAgeMS = 2
var orphanTimeoutMS = 3000
var pauseSecs int
var loopback = true
var udpStatsAddr = "127.0.0.1:9411"
var defaultDirectory = "/dev/shm/"
var restartOnExit bool
var registerAppInfo = true
var releaseLogPath = getExecDir() + "../"
var logDir = "/plogs/pubmgr"
var fileLogger logger.Logger

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
	logger.SetApplication("cachester")
	logger.SetLogger("pubmgr")
	logger.SetType("pubmgr")
	logger.SetCategory("mgr")
	logger.SetAlias("pubmgr")

	var err error
	if env, err = mmd.LookupEnvironment(); err != nil {
		logger.FatalError(err)
	}


	flag.StringVar(&udpStatsAddr, "stats", udpStatsAddr, "UDP address to publish stats to")
	flag.Var(&storePattern, "store", "Pattern(s) to match for storages to be published")
	flag.IntVar(&heartBeatMS, "heartbeat", heartBeatMS, "Heartbeat interval (in ms)")
	flag.IntVar(&maxAgeMS, "age", maxAgeMS, "Maximum packet age (in ms) allowed before sending a partial packet")
	flag.IntVar(&orphanTimeoutMS, "orphan", orphanTimeoutMS, "Maximum time (in ms) allowed between storage touches")
	flag.BoolVar(&loopback, "loopback", loopback, "Enable loopback of multicast data and adverts")
	flag.StringVar(&env, "env", env, "Environment to associate with publishers (default: local MMD environment)")
	flag.StringVar(&execPath, "path", execPath, "Path to Cachester executables")
	flag.BoolVar(&restartOnExit, "restart", true, "Restart publishers when they exit")
	flag.IntVar(&pauseSecs, "pause", 0, "Number of seconds to pause between publisher launches")
	flag.BoolVar(&registerAppInfo, "appinfo", registerAppInfo, "Registers an MMD app.info.pubmgr service")
	flag.StringVar(&releaseLogPath, "release", releaseLogPath, "Path to RELEASE_LOG file. Only used when -appinfo is true.")
	flag.StringVar(&logDir, "logdir", logDir, "Path to log directory")

	flag.Usage = usage
	flag.Parse()
	logger.InitStashLog(logDir)
}

func usage() {
	fmt.Fprintln(os.Stderr, "pubmgr " + sourceVersion)
	fmt.Fprintln(os.Stderr, "\nSyntax: " + os.Args[0] + " [OPTIONS] DIRECTORY [DIRECTORY ...]")
	flag.PrintDefaults()
	os.Exit(1)
}

func getExecDir() string {
	dir, _ := filepath.Split(os.Args[0])
	if dir != "" {
		strconv.AppendQuoteRune([]byte(dir), filepath.Separator)
	}

	return dir
}

func main() {
	if len(storePattern) == 0 {
		logger.FatalError("no store pattern specified")
	}

	logger.LogInfo("store pattern:", storePattern)

	var err error
	err = initializePostFlagsParsed()
	if err != nil {
		logger.FatalError(err)
	}

	if _, err = os.Stat(execPath + "publisher"); err != nil {
		logger.FatalError(err)
	}

	if registerAppInfo {
		err = appinfo.Setup("pubmgr", releaseLogPath, func() bool { return true })
		if err != nil {
			logger.LogWarn("cannot register appinfo service:", err)
		}
	}

	err = discoveryLoop()
	if err != nil {
		logger.FatalError(err)
	}

	logger.LogInfo("Done")
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
					logger.LogInfo("event:", event)
				}
				break
			}
		case errMsg := <-watcher.Errors:
			logger.LogError(errMsg)
		}
	}

	return nil
}

func startIfNeeded(path string) {
	name := filepath.Base(path)
	_, ok := publishers[name]
	if ok {
		logger.LogInfo("already publishing:", name)
		return
	}

	publishers[name] = nil
	addr, err := getMcastAddrFor(name)
	if err != nil {
		logger.LogError(err)
		return
	}

	logger.LogInfo("starting publisher for:", name, "on", addr)

	cmd, err := commander.New(execPath + "publisher", nil, nil, fileLogger)
	if err != nil {
		logger.FatalError(err)
	}

	var state struct { port int }

	cmd.Name = "publisher(" + name + ")"
	cmd.AutoRestart = restartOnExit

	cmd.BeforeStart = func(c *commander.Command) error {
		state.port, err = reservePort()
		if err != nil {
			return err
		}

		logger.LogInfo("reserving port", state.port, "for", name)

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
			"-j" }

		if loopback {
			opts = append(opts, "-l")
		}

		if udpStatsAddr != "" {
			opts = append(opts, "-S", udpStatsAddr)
		}

		c.Args = append(opts,
			path,
			listenAddress + ":" + strconv.Itoa(state.port),
			addr + ":" + strconv.Itoa(state.port),
		)

		return nil
	}

	cmd.AfterStop = func(*commander.Command) error {
		releasePort(state.port)
		state.port = 0
		return nil
	}

	logger.LogInfo("new publisher:", cmd)
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
