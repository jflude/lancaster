/*
   Copyright (c)2014-2017 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the COPYING file.
*/

package main

import (
	"commander"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"time"
)

type Subscribers struct {
	sync.RWMutex
	subsMap map[string]*Subscriber
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

type Subscriber struct {
	name string
	disc Discovery
	cmd  *commander.Command
}

var (
	ignore              = make(map[string]bool)
	subscribers         = Subscribers{subsMap: make(map[string]*Subscriber)}
	hasRun              = make(map[string]bool)
	advertAddr          = "227.1.1.227:11227"
	hostPattern         string
	descPattern         string
	env                 = "test"
	run                 = true
	wireProtocolVersion = "*"
	execPath            = getExecDir()
	udpStatsAddr        = "127.0.0.1:9411"
	dataInterfaces      = "bond0,eth0"
	restartOnExit       bool
	deleteOld           bool
	maxMissedHB         int
	pauseSecs           int
	queueSize           int64
	touchPeriodMS       = 1000
)

func init() {
	log.SetPrefix("pubmgr: ")

	flag.StringVar(&udpStatsAddr, "stats", udpStatsAddr, "UDP address to publish stats to")
	flag.StringVar(&advertAddr, "advert", advertAddr, "Address to listen on for advertised publishers")
	flag.StringVar(&descPattern, "desc", ".*", "Regex to match against advertised descriptions")
	flag.StringVar(&hostPattern, "host", ".*", "Regex to match against advertised hostnames")
	flag.StringVar(&env, "env", env, "Environment to match against advertised environments")
	flag.StringVar(&wireProtocolVersion, "protocol", wireProtocolVersion, "Required wire protocol version (* means any)")
	flag.StringVar(&execPath, "path", execPath, "Path to Lancaster executables")
	flag.BoolVar(&restartOnExit, "restart", true, "Restart subscribers when they exit")
	flag.BoolVar(&deleteOld, "delete", true, "Delete old storage files before (re)starting")
	flag.IntVar(&touchPeriodMS, "touch", touchPeriodMS, "Period (in ms) of storage touches by subscribers")
	flag.IntVar(&maxMissedHB, "missed", -1, "Number of missed heartbeats allowed (default: use subscriber's value")
	flag.IntVar(&pauseSecs, "pause", 0, "Number of seconds to pause between subscriber launches")
	flag.Int64Var(&queueSize, "queue", -1, "Size of subscriber's change queue (default: use publisher's value)")
	flag.StringVar(&dataInterfaces, "i", dataInterfaces, "Network interfaces to receive multicast data on")

	flag.Parse()
}

func (subs *Subscribers) Get(key string) *Subscriber {
	subs.RLock()
	ret := subs.subsMap[key]
	subs.RUnlock()
	return ret
}

func (subs *Subscribers) Set(key string, sub *Subscriber) {
	subs.Lock()
	subs.subsMap[key] = sub
	subs.Unlock()
}

func (subs *Subscribers) Delete(key string) {
	subs.Lock()
	delete(subs.subsMap, key)
	subs.Unlock()
}

func getExecDir() string {
	dir, _ := filepath.Split(os.Args[0])
	if dir != "" {
		strconv.AppendQuoteRune([]byte(dir), filepath.Separator)
	}

	return dir
}

func discoveryLoop() error {
	var iface *net.Interface
	var err error
	for _, dataInterface := range strings.Split(dataInterfaces, ",") {
		if !interfaceExists(dataInterface) {
			log.Println("interface", dataInterface, "not found")
		} else {
			iface, err = net.InterfaceByName(dataInterface)
			if err != nil {
				log.Fatalln(err)
			}
		}
	}

	advertAddrHostPort := strings.Split(advertAddr, ":")
	advertAddrHost := advertAddrHostPort[0]
	advertAddrPort, err := strconv.Atoi(advertAddrHostPort[1])
	if err != nil {
		log.Fatalln("cannot parse advert address: ", advertAddr, ": ", err)
	}

	addr := &net.UDPAddr{IP: net.ParseIP(advertAddrHost), Port: advertAddrPort}
	log.Println("listening for adverts on: ", addr)

	sock, err := net.ListenMulticastUDP("udp", iface, addr)
	if err != nil {
		log.Fatalln(err)
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
			log.Fatalln(err)
		}

		jsbin := data[:n]
		jsstr := string(jsbin)
		if ignore[jsstr] {
			continue
		}

		ignore[jsstr] = true
		var disc Discovery
		err = json.Unmarshal(jsbin, &disc)

		if err != nil {
			log.Println("invalid discovery format: ", err, " from: ", from, " ["+jsstr+"]")
		} else if wireProtocolVersion != "*" && disc.Version != wireProtocolVersion {
			log.Fatalln("wire version mismatch, expected ", wireProtocolVersion,
				" but got version", disc.Version, " from ", from, " ["+jsstr+"]")
		} else if len(disc.Data) != 1 {
			log.Println("unsupported discovery message, wrong number of data elements [" + jsstr + "]")
		} else {
			desc := disc.Data[0].Description
			if env != disc.Env {
				log.Println("no match on environment: ", env, " ["+jsstr+"]")
			} else if !hp.MatchString(disc.Hostname) {
				log.Println("no match on hostname: ", hostPattern, " ["+jsstr+"]")
			} else if !fp.MatchString(desc) {
				log.Println("no match on description: ", descPattern, " ["+jsstr+"]")
			} else if subscribers.Get(desc) == nil {
				if !restartOnExit {
					if _, exists := hasRun[desc]; exists {
						log.Println("spent subscriber: ", desc, " from: ", from, " ["+jsstr+"]")
						continue
					}
				}

				sub := &Subscriber{name: desc, disc: disc}
				subscribers.Set(desc, sub)
				hasRun[desc] = true

				log.Println("new subscriber: ", desc, " from: ", from, " ["+jsstr+"]")
				go sub.run()

				time.Sleep(time.Duration(pauseSecs) * time.Second)
			}
		}
	}

	return nil
}

func (sub *Subscriber) String() string {
	return sub.cmd.String()
}

func (sub *Subscriber) run() {
	addr, err := net.LookupHost(sub.disc.Hostname)
	if err != nil {
		log.Fatalln(err)
	}

	storePath := "shm:/client." + sub.disc.Data[0].Description

	opts := []string{
		"-p", sub.disc.Data[0].Description,
		"-T", fmt.Sprint(touchPeriodMS * 1000),
		"-L",
		"-j"}

	if queueSize != -1 {
		opts = append(opts, "-q", strconv.FormatInt(queueSize, 10))
	}

	if udpStatsAddr != "" {
		opts = append(opts, "-S", udpStatsAddr)
	}

	if maxMissedHB != -1 {
		opts = append(opts, "-H", fmt.Sprint(maxMissedHB))
	}

	sub.cmd, err = commander.New(execPath+"subscriber", nil, nil, nil)
	if err != nil {
		log.Fatalln("cannot create commander for: ", sub, ": ", err)
	}

	sub.cmd.Name = "subscriber(" + sub.name + ")"
	sub.cmd.Args = append(opts, storePath, addr[0]+":"+strconv.Itoa(sub.disc.Data[0].Port))

	if deleteOld {
		sub.cmd.BeforeStart = func(*commander.Command) error {
			removeFileCommand := exec.Command(execPath+"deleter", "-f", storePath)
			err := removeFileCommand.Run()
			if err != nil {
				log.Fatalln("cannot delete storage file: ", storePath)
			}

			return err
		}
	}

	err = sub.cmd.Run()
	log.Println("commander for: ", sub, " exited: ", err)

	subscribers.Delete(sub.name)
	ignore = make(map[string]bool)
}

func interfaceExists(interfaceName string) bool {
	interfaces, err := net.Interfaces()
	if err != nil {
		log.Fatalln(err)
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
		err = nil
	}

	return false, err
}

func main() {
	var err error
	if _, err := os.Stat(execPath + "subscriber"); err != nil {
		log.Fatalln(err)
	}

	if maxMissedHB < -1 {
		log.Fatalln("invalid value for allowed missed heartbeats")
	}

	err = discoveryLoop()
	if err != nil {
		log.Fatalln(err)
	}

	log.Println("done")
}
