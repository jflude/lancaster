package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"github.peak6.net/platform/gocore.git/appinfo"
	"github.peak6.net/platform/gocore.git/commander"
	"github.peak6.net/platform/gocore.git/mmd"
	"github.peak6.net/platform/gocore.git/logger"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"time"
)

var verbose bool
var stop = make(chan struct{})
var ignore = make(map[string]bool)
var subscribers = make(map[string]*Subscriber)
var hasRun = make(map[string]bool)
var advertAddr = "227.1.1.227:11227"
var hostPattern string
var descPattern string
var env string
var run = true
var wireProtocolVersion = "*"
var execPath = getExecDir()
var sourceVersion = "<DEV>"
var udpStatsAddr = "127.0.0.1:9411"
var dataInterfaces = "bond0,eth0"
var restartOnExit bool
var deleteOld bool
var maxMissedHB int
var pauseSecs int
var queueSize int64
var touchPeriodMS = 1000
var registerAppInfo = true
var releaseLogPath = getExecDir() + "../"
var logDir = "/plogs/submgr"
var fileLogger logger.Logger


type Discovery struct {
	Hostname string
	Env      string
	Version  string
	Data     []struct {
		Port int
		Description string
	}
}

type Subscriber struct {
	name string
	disco Discovery
	cmdr *commander.Command
}

func (si *Subscriber) String() string {
	return si.cmdr.String()
}

func init() {
	logger.SetApplication("cachester")
	logger.SetLogger("submgr")
	logger.SetType("submgr")
	logger.SetCategory("mgr")
	logger.SetAlias("submgr")

	var err error
	if env, err = mmd.LookupEnvironment(); err != nil {
		logger.FatalError(err)
	}

	flag.BoolVar(&verbose, "verbose", false, "Verbose")
	flag.StringVar(&udpStatsAddr, "stats", udpStatsAddr, "UDP address to publish stats to")
	flag.StringVar(&advertAddr, "advert", advertAddr, "Address to listen on for advertised publishers")
	flag.StringVar(&descPattern, "desc", ".*", "Regex to match against advertised descriptions")
	flag.StringVar(&hostPattern, "host", ".*", "Regex to match against advertised hostnames")
	flag.StringVar(&env, "env", env, "Environment to match against advertised environments (default: local MMD environment)")
	flag.StringVar(&wireProtocolVersion, "protocol", wireProtocolVersion, "Required wire protocol version (* means any)")
	flag.StringVar(&execPath, "path", execPath, "Path to Cachester executables")
	flag.BoolVar(&restartOnExit, "restart", true, "Restart subscribers when they exit")
	flag.BoolVar(&deleteOld, "delete", true, "Delete old storage files before (re)starting")
	flag.IntVar(&touchPeriodMS, "touch", touchPeriodMS, "Period (in ms) of storage touches by subscribers")
	flag.IntVar(&maxMissedHB, "missed", -1, "Number of missed heartbeats allowed (default: use subscriber's value")
	flag.IntVar(&pauseSecs, "pause", 0, "Number of seconds to pause between subscriber launches")
	flag.Int64Var(&queueSize, "queue", -1, "Size of subscriber's change queue (default: use publisher's value)")
	flag.StringVar(&dataInterfaces, "i", dataInterfaces, "Network interfaces to receive multicast data on")
	flag.BoolVar(&registerAppInfo, "appinfo", registerAppInfo, "Registers an MMD app.info.submgr service")
	flag.StringVar(&releaseLogPath, "release", releaseLogPath, "Path to RELEASE_LOG file. Only used when -appinfo is true.")
	flag.StringVar(&logDir, "logdir", logDir, "Path to log directory")

	flag.Usage = usage
	flag.Parse()
	logger.InitStashLog(logDir)
}

func usage() {
	fmt.Fprintln(os.Stderr, "submgr " + sourceVersion)
	fmt.Fprintln(os.Stderr, "\nSyntax: " + os.Args[0] + " [OPTIONS]")
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
	var err error
	if _, err := os.Stat(execPath + "subscriber"); err != nil {
		logger.FatalError(err)
	}

	if maxMissedHB < 0 && maxMissedHB != -1 {
		logger.FatalError("invalid value for allowed missed heartbeats")
	}

	if registerAppInfo {
		err = appinfo.Setup("submgr", releaseLogPath, func() bool { return true })
		if err != nil {
			logger.LogWarn("cannot register appinfo service")
		}
	}

	err = discoveryLoop()
	if err != nil {
		logger.FatalError(err)
	}

	logger.LogInfo("done")
}

func discoveryLoop() error {
	var iface *net.Interface
	var err error
	for _, dataInterface := range strings.Split(dataInterfaces, ",") {
		if !interfaceExists(dataInterface) {
			logger.LogInfo("interface", dataInterface, "not found")
		} else {
			iface, err = net.InterfaceByName(dataInterface)
			if err != nil {
				logger.FatalError(err)
			}
		}
	}

	advertAddrHostPort := strings.Split(advertAddr, ":")
	advertAddrHost := advertAddrHostPort[0]
	advertAddrPort, err := strconv.Atoi(advertAddrHostPort[1])
	if err != nil {
		logger.FatalError("cannot parse advert address", advertAddr, ":", err)
	}

	addr := &net.UDPAddr{IP: net.ParseIP(advertAddrHost), Port: advertAddrPort}
	logger.LogInfo("listening for adverts on:", addr)

	sock, err := net.ListenMulticastUDP("udp", iface, addr)
	if err != nil {
		logger.FatalError(err)
	}

	data := make([]byte, 4096)
	fp, err := regexp.Compile(descPattern)
	if err != nil {
		return err
	}

	hp, err := regexp.Compile(hostPattern)
	if err != nil {
		return err
	}

	for run {
		n, from, err := sock.ReadFrom(data)
		if err != nil {
			logger.FatalError(err)
		}

		jsbin := data[:n]
		jsstr := string(jsbin)
		if ignore[jsstr] {
			continue
		}

		ignore[jsstr] = true
		var disco Discovery
		err = json.Unmarshal(jsbin, &disco)

		if err != nil {
			logger.LogError("invalid discovery format:", err, "from:", from, "[" + jsstr + "]")
		} else if wireProtocolVersion != "*" && disco.Version != wireProtocolVersion {
			logger.LogWarn("wire version mismatch, expected", wireProtocolVersion,
				"but got version", disco.Version, "from", from, "[" + jsstr + "]")
		} else if len(disco.Data) != 1 {
			logger.LogError("unsupported discovery message, wrong number of data elements [" + jsstr + "]")
		} else {
			desc := disco.Data[0].Description
			if env != disco.Env {
				logger.LogInfo("no match on environment:", env, "[" + jsstr + "]")
			} else if !hp.MatchString(disco.Hostname) {
				logger.LogInfo("no match on hostname:", hostPattern, "[" + jsstr + "]")
			} else if !fp.MatchString(desc) {
				logger.LogInfo("no match on description:", descPattern, "[" + jsstr + "]")
			} else if subscribers[desc] == nil {
				if !restartOnExit {
					if _, exists := hasRun[desc]; exists {
						logger.LogInfo("spent subscriber:", desc, "from:", from, "[" + jsstr + "]")
						continue
					}
				}

				si := &Subscriber{name: desc, disco: disco}
				subscribers[desc] = si
				hasRun[desc] = true

				logger.LogInfo("new subscriber:", desc, "from:", from, "[" + jsstr + "]")
				go si.run()

				time.Sleep(time.Duration(pauseSecs) * time.Second)
			}
		}
	}

	return nil
}

func (si *Subscriber) run() {
	addr, err := net.LookupHost(si.disco.Hostname)
	if err != nil {
		logger.FatalError(err)
	}

	storePath := "shm:/client." + si.disco.Data[0].Description

	opts := []string{
		"-p", si.disco.Data[0].Description,
		"-T", fmt.Sprint(touchPeriodMS * 1000),
		"-L",
		"-j" }

	if queueSize != -1 {
		opts = append(opts, "-q", strconv.FormatInt(queueSize, 10))
	}

	if udpStatsAddr != "" {
		opts = append(opts, "-S", udpStatsAddr)
	}

	if maxMissedHB != -1 {
		opts = append(opts, "-H", fmt.Sprint(maxMissedHB))
	}

	si.cmdr, err = commander.New(execPath + "subscriber", nil, nil, fileLogger)
	if err != nil {
		logger.FatalError("cannot create commander for:", si, ":", err)
	}

	si.cmdr.Name = "subscriber(" + si.name + ")"
	si.cmdr.Args = append(opts, storePath, addr[0] + ":" + strconv.Itoa(si.disco.Data[0].Port))

	if deleteOld {
		si.cmdr.BeforeStart = func(*commander.Command) error {
			removeFileCommand := exec.Command(execPath + "deleter", "-f", storePath)
			err := removeFileCommand.Run()
			if err != nil {
				logger.FatalError("cannot delete storage file:", storePath)
			}

			return err
		}
	}

	err = si.cmdr.Run()
	logger.LogInfo("commander for:", si, "exited:", err)

	delete(subscribers, si.name)
	ignore = make(map[string]bool)
}

func interfaceExists(interfaceName string) bool {
	interfaces, err := net.Interfaces()
	if err != nil {
		logger.FatalError(err)
	}

	for _, anInterface := range interfaces {
		if strings.EqualFold(anInterface.Name, interfaceName) {
			return true
		}
	}

	return false
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
