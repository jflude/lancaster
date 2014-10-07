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
var subscriberPath = "../subscriber"
var sourceVersion = "<DEV>"

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
	flag.Usage = usage
	flag.BoolVar(&verbose, "verbose", false, "Verbose")
	flag.StringVar(&advertAddr, "aa", advertAddr, "Address to listen for advertised feeds on")
	flag.StringVar(&feedPattern, "fp", ".*", "Regex to match against feed descriptors")
	flag.StringVar(&hostPattern, "hp", ".*", "Regex to match against host names")
	flag.StringVar(&env, "env", env, "Environment to match against feed environment. Defaults to local MMD environment.")
	flag.StringVar(&wireProtocolVersion, "wpv", wireProtocolVersion, "Required wire protocol version (* means any)")
	flag.StringVar(&subscriberPath, "sub", subscriberPath, "Path to subscriber exeutable")
	flag.Parse()

	if _, err := os.Stat(subscriberPath); err != nil {
		log.Fatalln(err)
	}
	commander.SetDefaultLogger(log.New(os.Stderr, log.Prefix(), log.Flags()))
	commander.SetDefaultStdIO(nil, os.Stderr, os.Stdout)
	err := discoveryLoop()
	if err != nil {
		log.Fatal(err)
	}
	log.Println("Done")
}

func discoveryLoop() error {
	iface, err := net.InterfaceByName("bond0")
	chkFatal(err)
	advertAddrHostPort := strings.Split(advertAddr, ":")
	advertAddrHost := advertAddrHostPort[0]
	advertAddrPort, err := strconv.Atoi(advertAddrHostPort[1])
	if err != nil {
		log.Fatal("Error parsing advertisement address ", advertAddr, " : ", err)
	}
	addr := &net.UDPAddr{IP: net.ParseIP(advertAddrHost), Port: advertAddrPort}
	sock, err := net.ListenMulticastUDP("udp", iface, addr)
	log.Println("Listening for advertisements on:", addr)
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
			log.Println("Bad discovery format (", err, "), ignoring:", jsstr)
		} else if wireProtocolVersion != "*" && disc.Version != wireProtocolVersion {
			log.Println("Version mismatch, expected:", wireProtocolVersion, "got:", jsstr)
		} else if len(disc.Data) != 1 {
			log.Println("Unsupported discovery message, wrong number of data elements:", jsstr)
		} else {
			desc := disc.Data[0].Description
			if env != disc.Env {
				log.Println("No match on env:", env, "ignoring:", jsstr)
			} else if !hp.MatchString(disc.Hostname) {
				log.Println("No match on host pattern:", hostPattern, "ignoring:", jsstr)
			} else if !fp.MatchString(desc) {
				log.Println("No match on feed pattern:", feedPattern, "ignoring:", jsstr)
			} else if feeds[desc] != nil {
				//log.Println("Already have a feed for:", desc, "ignoring:", jsstr)
			} else {
				log.Println("New Feed:", desc, ", from:", from, "disc:", jsstr)
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

	si.commander, err = commander.New(subscriberPath,
		"-j",
		"-p", si.discovery.Data[0].Description,
		"shm:/client."+si.discovery.Data[0].Description,
		strconv.Itoa(1024*1024),
		addr[0]+":"+strconv.Itoa(si.discovery.Data[0].Port),
	)
	if err != nil {
		log.Fatalln("Failed to create commander for:", si, "error:", err)
	}
	si.commander.Name = si.name
	si.commander.AutoRestart = false
	err = si.commander.Run()
	log.Println("Commander for:", si, "exited:", err)
	delete(feeds, si.name)         // deregister this feed
	ignore = make(map[string]bool) // reset ignore list, allow us to rediscover this stripe
}

func chkFatal(err error) {
	if err != nil {
		log.Fatal(err)
	}
}
