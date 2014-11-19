package main

import (
	"encoding/json"
	"flag"
	"fmt"
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
var subscriberPath = getExecDir() + "subscriber"
var sourceVersion = "<DEV>"
var udpStatsAddr = "127.0.0.1:9411"
var shmDirectory = "/dev/shm"
var mcastInterface = "bond0"
var restartOnExit bool
var deleteOldStorages bool
var queueSize int64

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
	flag.StringVar(&mcastInterface, "i", mcastInterface, "Multicast interface")
	if env, err = mmd.LookupEnvironment(); err != nil {
		log.Fatal(err)
	}
}

func getExecDir() string {
	dir, _ := filepath.Split(os.Args[0])
	if dir != "" {
		strconv.AppendQuoteRune([]byte(dir), filepath.Separator)
	}

	return dir
}

func usage() {
	v, err := exec.Command(subscriberPath, "-v").Output()
	var subscriberVersion string
	if err != nil {
		subscriberVersion = "Error, can't find subscriber at: " + subscriberPath
	} else {
		subscriberVersion = strings.TrimSpace(string(v))
	}

	fmt.Fprintln(os.Stderr, ""+
		"         Source: "+sourceVersion+
		"\n     Subscriber: "+subscriberVersion+
		"\n  Wire Protocol: "+wireProtocolVersion+
		"\n\n Usage: "+os.Args[0]+" [flags]")

	flag.PrintDefaults()
	os.Exit(1)
}

func main() {
	var err error
	flag.Usage = usage
	flag.BoolVar(&verbose, "verbose", false, "Verbose")
	flag.StringVar(&udpStatsAddr, "stats", udpStatsAddr, "UDP address to publish stats to")
	flag.StringVar(&advertAddr, "aa", advertAddr, "Address to listen for advertised feeds on")
	flag.StringVar(&feedPattern, "fp", ".*", "Regex to match against feed descriptors")
	flag.StringVar(&hostPattern, "hp", ".*", "Regex to match against host names")
	flag.StringVar(&env, "env", env, "Environment to match against feed environment (default: local MMD environment)")
	flag.StringVar(&wireProtocolVersion, "wpv", wireProtocolVersion, "Required wire protocol version (* means any)")
	flag.StringVar(&subscriberPath, "sub", subscriberPath, "Path to subscriber executable")
	flag.BoolVar(&restartOnExit, "restartOnExit", true, "Restart subscriber instances when they exit")
	flag.BoolVar(&deleteOldStorages, "deleteOldStorages", true, "Delete old storage files before (re)starting")
	flag.Int64Var(&queueSize, "queueSize", -1, "Size of subscriber's change queue (default: use publisher's size)")
	flag.Parse()

	if _, err := os.Stat(subscriberPath); err != nil {
		log.Fatalln(err)
	}

	commander.SetDefaultLogger(log.New(os.Stderr, log.Prefix(), log.Flags()))
	commander.SetDefaultStdIO(nil, os.Stderr, os.Stdout)
	err = discoveryLoop()
	if err != nil {
		log.Fatal(err)
	}

	logln("Done")
}

func discoveryLoop() error {
	iface, err := net.InterfaceByName(mcastInterface)
	chkFatal(err)

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
			logln("Bad discovery format (", err, "), ignoring: ", jsstr)
		} else if wireProtocolVersion != "*" && disc.Version != wireProtocolVersion {
			logln("Wire version mismatch, expected: ", wireProtocolVersion, "got: ", jsstr)
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

	si.commander, err = commander.New(subscriberPath)
	if err != nil {
		log.Fatalln("Failed to create commander for:", si, ", error:", err)
	}

	si.commander.Args = append(opts, storePath, addr[0]+":"+strconv.Itoa(si.discovery.Data[0].Port))
	si.commander.Name = si.name
	si.commander.AutoRestart = false

	if deleteOldStorages {
		si.commander.BeforeStart = func(*commander.Command) error {
			storePathToDelete := storePath
			if strings.HasPrefix(storePath, "shm:") {
				storePathToDelete = strings.Replace(storePathToDelete, "shm:", shmDirectory, 1)
			}

			removeFileCommand := exec.Command("rm", "-f", storePathToDelete)
			err := removeFileCommand.Run()
			if err != nil {
				log.Fatalln("Could not delete storage file at ", storePathToDelete)
			}

			return err
		}
	}

	si.commander.AfterStart = func(c *commander.Command) error {
		// Linux - coredumps should include shared mmap segments
		filt := fmt.Sprintf("/proc/%d/coredump_filter", c.Pid)
		exist, err := fileExists(filt)
		if exist {
			var f *os.File
			f, err = os.OpenFile(filt, os.O_WRONLY, 0644)
			if err == nil {
				_, err2 := f.WriteString("0x2F")
				err = f.Close()
				if err2 != nil {
					err = err2
				}
			}
		}

		return err
	}

	si.commander.AutoRestart = restartOnExit
	err = si.commander.Run()

	logln("Commander for: ", si, " exited: ", err)
	delete(feeds, si.name)         // deregister this feed
	ignore = make(map[string]bool) // reset ignore list, allow us to rediscover this stripe
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
