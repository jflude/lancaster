package main

import (
	"errors"
	"flag"
	"fmt"
	"log"
	"net"
	"os"
	"strings"
	"sync"
)

var baseMCastGroup IP
var advertAddr = "227.1.1.227:11227"
var addrLock sync.Mutex
var addrsAssigned = make(map[string]bool)
var hostName string
var clientInterface = "bond0"
var listenAddress string
var ifaceToIp = make(map[string]string)

var portPicker = struct {
	rangeStart int
	rangeEnd   int

	rangeSize int
	counter   int
	inUse     map[int]bool
}{rangeStart: 26000, rangeEnd: 26999, inUse: make(map[int]bool)}

type IP net.IP

func (ip *IP) String() string {
	return fmt.Sprint((*net.IP)(ip))
}
func (ip *IP) Set(val string) error {
	*ip = IP(net.ParseIP(val).To4())
	return nil
}

func init() {
	err := initAddrs()
	if err != nil {
		log.Fatal(err)
	}
	listenAddress = ifaceToIp[clientInterface]
	flag.Var(&baseMCastGroup, "bg", "Base Multicast group (each feed increments the 3rd octet)")
	flag.IntVar(&portPicker.rangeStart, "ps", portPicker.rangeStart, "Port range start")
	flag.IntVar(&portPicker.rangeEnd, "pe", portPicker.rangeEnd, "Port range end")
	flag.StringVar(&clientInterface, "i", clientInterface, "Client side interface")
	flag.StringVar(&listenAddress, "la", listenAddress, "Listen address")

}

func mcastAddrFor(name string) (string, error) {
	addrLock.Lock()
	defer addrLock.Unlock()
	addr := ""
	for x := 0; x < 256; x++ {
		if !addrsAssigned[baseMCastGroup.String()] {
			addr = baseMCastGroup.String()
			break
		}
		baseMCastGroup[2]++
	}
	if addr == "" {
		return "", errors.New("Out of multicast addresses")
	}
	addrsAssigned[addr] = true
	log.Println("Assigned:", addr, "to:", name)
	return addr, nil
}

func reservePort() (int, error) {
	addrLock.Lock()
	defer addrLock.Unlock()
	if portPicker.rangeSize == 0 {
		portPicker.rangeSize = (portPicker.rangeEnd - portPicker.rangeStart) + 1
		portPicker.counter = portPicker.rangeStart
	}

	for end := portPicker.counter + portPicker.rangeSize; portPicker.counter < end; portPicker.counter++ {
		e := portPicker.rangeStart + (portPicker.counter % portPicker.rangeSize)
		if !portPicker.inUse[e] {
			portPicker.inUse[e] = true
			return e, nil
		}
	}
	return 0, fmt.Errorf("Either ports have been leaked or port range is too small. Ports in use: %d, range: %d - %d", len(portPicker.inUse), portPicker.rangeStart, portPicker.rangeEnd)
}

func releasePort(portNumber int) {
	addrLock.Lock()
	defer addrLock.Unlock()
	log.Println("Releasing:", portNumber)
	delete(portPicker.inUse, portNumber)
}

func initAddrs() error {
	var err error
	hostName, err = os.Hostname()
	if err != nil {
		return err
	}
	addrs, err := net.LookupHost(hostName)
	if err != nil {
		return err
	}
	addr := net.ParseIP(addrs[0]).To4()
	addr[0] = 227
	addr[1] = 227
	addr[2] = 0
	baseMCastGroup = IP(addr)
	ifaces, err := net.Interfaces()
	if err != nil {
		return err
	}
	for _, iface := range ifaces {
		if iface.Flags&net.FlagLoopback != 0 {
			continue
		}
		addrs, err := iface.Addrs()
		if err != nil || len(addrs) == 0 || iface.Flags&net.FlagMulticast == 0 {
			continue
		}
		ifaceToIp[iface.Name] = strings.Split(addrs[0].String(), "/")[0]
	}

	return nil
}
