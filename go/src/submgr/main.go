package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"github.peak6.net/platform/gocore.git/appinfo"
	"github.peak6.net/platform/gocore.git/commander"
	"github.peak6.net/platform/gocore.git/mmd"
	"log"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
)

var verbose bool
var stop = make(chan struct{})
var ignore = make(map[string]bool)
var feeds = make(map[string]*SubscriberInstance)
var advertAddr = "227.1.1.227:11227"
var hostPattern string
var feedPattern string
var env string
var run = true
var wireProtocolVersion = "*"
var execPath = getExecDir()
var sourceVersion = "<DEV>"
var udpStatsAddr = "127.0.0.1:9411"
var mcastInterfaces = "bond0,eth0"
var restartOnExit bool
var deleteOldStorages bool
var queueSize int64
var registerAppInfo = true
var releaseLogPath = getExecDir() + "/../"

func logln(args ...interface{}) {
	log.Println(args...)
}

type Discovery struct {
	Hostname string
	Env      string
	Version  string
	Data     []struct {
		Port        int
		Description string
	}
}

type SubscriberInstance struct {
	name      string
	discovery Discovery
	commander *commander.Command
}

func (si *SubscriberInstance) String() string {
	return si.commander.String()
}

func init() {
	var err error
	if env, err = mmd.LookupEnvironment(); err != nil {
		log.Fatal(err)
	}

	flag.BoolVar(&verbose, "verbose", false, "Verbose")
	flag.StringVar(&udpStatsAddr, "stats", udpStatsAddr, "UDP address to publish stats to")
	flag.StringVar(&advertAddr, "aa", advertAddr, "Address to listen for advertised feeds on")
	flag.StringVar(&feedPattern, "fp", ".*", "Regex to match against feed descriptors")
	flag.StringVar(&hostPattern, "hp", ".*", "Regex to match against host names")
	flag.StringVar(&env, "env", env, "Environment to match against feed environment (default: local MMD environment)")
	flag.StringVar(&wireProtocolVersion, "wpv", wireProtocolVersion, "Required wire protocol version (* means any)")
	flag.StringVar(&execPath, "path", execPath, "Path to Cachester executables")
	flag.BoolVar(&restartOnExit, "restart", true, "Restart subscriber instances when they exit")
	flag.BoolVar(&deleteOldStorages, "deleteOldStorages", true, "Delete old storage files before (re)starting")
	flag.Int64Var(&queueSize, "queueSize", -1, "Size of subscriber's change queue (default: use publisher's size)")
	flag.StringVar(&mcastInterfaces, "i", mcastInterfaces, "Multicast interfaces to use")
	flag.BoolVar(&registerAppInfo, "appinfo", registerAppInfo, "Registers an MMD app.info.submgr service")
	flag.StringVar(&releaseLogPath, "releaseLogPath", releaseLogPath, "Path to RELEASE_LOG file. Only used when -appinfo is true.")

	flag.Usage = usage
	flag.Parse()
}

func usage() {
	fmt.Fprintln(os.Stderr, "submgr "+sourceVersion)
	fmt.Fprintln(os.Stderr, "\nSyntax: "+os.Args[0]+" [OPTIONS]")
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
		log.Fatalln(err)
	}

	commander.SetDefaultLogger(log.New(os.Stderr, log.Prefix(), log.Flags()))

	if registerAppInfo {
		err = appinfo.Setup("submgr", releaseLogPath, func() bool { return true });
		if err != nil {
			log.Println("Couldn't register appinfo service:", err)
		}
	}

	err = discoveryLoop()
	if err != nil {
		log.Fatal(err)
	}

	logln("Done")
}

func discoveryLoop() error {
	var iface *net.Interface
	var err error
	for _, mcastInterface := range strings.Split(mcastInterfaces, ",") {
		if !interfaceExists(mcastInterface) {
			logln("Interface ", mcastInterface, " not found")
		} else {
			iface, err = net.InterfaceByName(mcastInterface)
			chkFatal(err)
		}
	}

	advertAddrHostPort := strings.Split(advertAddr, ":")
	advertAddrHost := advertAddrHostPort[0]
	advertAddrPort, err := strconv.Atoi(advertAddrHostPort[1])
	if err != nil {
		log.Fatal("Error parsing advert address ", advertAddr, ": ", err)
	}

	addr := &net.UDPAddr{IP: net.ParseIP(advertAddrHost), Port: advertAddrPort}
	sock, err := net.ListenMulticastUDP("udp", iface, addr)
	logln("Listening for adverts on: ", addr)
	chkFatal(err)

	data := make([]byte, 4096)
	fp, err := regexp.Compile(feedPattern)
	if err != nil {
		return err
	}

	hp, err := regexp.Compile(hostPattern)
	if err != nil {
		return err
	}

	for run {
		n, from, err := sock.ReadFrom(data)
		chkFatal(err)
		jsbin := data[:n]
		jsstr := string(jsbin)
		if ignore[jsstr] {
			continue
		}

		ignore[jsstr] = true
		var disc Discovery
		err = json.Unmarshal(jsbin, &disc)

		if err != nil {
			logln("Bad discovery format \"", err, "\", ignoring: ", jsstr)
		} else if wireProtocolVersion != "*" && disc.Version != wireProtocolVersion {
			logln("Wire version mismatch, expected: ", wireProtocolVersion, ", got: ", jsstr)
		} else if len(disc.Data) != 1 {
			logln("Unsupported discovery message, wrong number of data elements: ", jsstr)
		} else {
			desc := disc.Data[0].Description
			if env != disc.Env {
				logln("No match on env: ", env, ", ignoring: ", jsstr)
			} else if !hp.MatchString(disc.Hostname) {
				logln("No match on host pattern: ", hostPattern, ", ignoring: ", jsstr)
			} else if !fp.MatchString(desc) {
				logln("No match on feed pattern: ", feedPattern, ", ignoring: ", jsstr)
			} else if feeds[desc] != nil {
				//logln("Already have a feed for: ", desc, ", ignoring: ", jsstr)
			} else {
				logln("New Feed1:", desc, ", from: ", from, ", disc: ", jsstr)
				logln("New Feed2:", desc, ", from: ", from, ", disc: ", jsstr)
				si := &SubscriberInstance{name: desc, discovery: disc}
				feeds[desc] = si
				go si.run()
			}
		}
	}

	return nil
}

func (si *SubscriberInstance) run() {
	addr, err := net.LookupHost(si.discovery.Hostname)
	chkFatal(err)
	storePath := "shm:/client." + si.discovery.Data[0].Description

	opts := []string{
		"-j",
		"-p", si.discovery.Data[0].Description}

	if queueSize != -1 {
		opts = append(opts, "-q", strconv.FormatInt(queueSize, 10))
	}

	if udpStatsAddr != "" {
		opts = append(opts, "-S", udpStatsAddr)
	}

	si.commander, err = commander.New(execPath + "subscriber")
	if err != nil {
		log.Fatalln("Failed to create commander for: ", si, ", error: ", err)
	}

	si.commander.Name = "subscriber(" + si.name + ")"

	si.commander.Args = append(opts, storePath, addr[0]+":"+strconv.Itoa(si.discovery.Data[0].Port))
	si.commander.AutoRestart = false

	if deleteOldStorages {
		si.commander.BeforeStart = func(*commander.Command) error {
			removeFileCommand := exec.Command(execPath+"deleter", "-f", storePath)
			err := removeFileCommand.Run()
			if err != nil {
				log.Fatalln("Could not delete storage file at ", storePath)
			}

			return err
		}
	}

	si.commander.AutoRestart = restartOnExit
	err = si.commander.Run()

	logln("Commander for: ", si, " exited: ", err)
	delete(feeds, si.name)         // deregister this feed
	ignore = make(map[string]bool) // reset ignore list, allow us to rediscover this stripe
}

func interfaceExists(interfaceName string) bool {
	interfaces, err := net.Interfaces()
	if err != nil {
		log.Fatal(err)
	}

	for _, anInterface := range interfaces {
		if strings.EqualFold(anInterface.Name, interfaceName) {
			return true
		}
	}

	return false
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
