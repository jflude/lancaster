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
var mcastInterfacesToTry = "bond0,eth0"
var mcastInterface string
var listenAddress string
var ifaceToIp = make(map[string]string)

var portPicker = struct {
	rangeStart int
	rangeEnd   int
	rangeSize  int
	counter    int
	inUse      map[int]bool
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

	flag.Var(&baseMCastGroup, "bg", "Base multicast group (each feed increments the 3rd octet)")
	flag.IntVar(&portPicker.rangeStart, "ps", portPicker.rangeStart, "Port range start")
	flag.IntVar(&portPicker.rangeEnd, "pe", portPicker.rangeEnd, "Port range end")
	flag.StringVar(&mcastInterfacesToTry, "i", mcastInterfacesToTry, "Comma-separated list of multicast interfaces to try")
	flag.StringVar(&listenAddress, "listen", listenAddress, "TCP/IP listen address")
}

func initializePostFlagsParsed() error {
	err := setMcastInterface()
	if err != nil {
		return err
	}
	return setListenAddress()
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

	return 0, fmt.Errorf("Port range leaked or too narrow. Ports in use: %d, range: %d - %d",
		len(portPicker.inUse), portPicker.rangeStart, portPicker.rangeEnd)
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
		if err != nil || len(addrs) == 0 || (iface.Flags & net.FlagMulticast) == 0 {
			continue
		}

		ifaceToIp[iface.Name] = strings.Split(addrs[0].String(), "/")[0]
	}

	return nil
}

func setMcastInterface() error {
	if mcastInterface != "" {
		return nil
	}

	for _, mcastInterfaceToTry := range strings.Split(mcastInterfacesToTry, ",") {
		if _, ok := ifaceToIp[mcastInterfaceToTry]; ok {
			mcastInterface = mcastInterfaceToTry
			log.Println("Setting mcastInterface to ", mcastInterface)
			return nil
		} else {
			log.Println("No valid interface named '", mcastInterfaceToTry, "' was found")
		}
	}

	return errors.New("No valid interface found. Tried " + mcastInterfacesToTry)
}

func setListenAddress() error {
	if listenAddress != "" {
		log.Println("Listen address already defined to ", listenAddress)
		return nil
	}

	err := setMcastInterface()
	if err != nil {
		return err
	}

	listenAddress = ifaceToIp[mcastInterface]
	log.Println("Setting listenAddress to", listenAddress)
	return nil
}
