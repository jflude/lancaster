/*
   Copyright (c)2014-2017 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the LICENSE file.
*/

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

type IP net.IP

var (
	baseMCastGroup        IP
	advertAddr            = "227.1.1.227:11227"
	addrLock              sync.Mutex
	addrsAssigned         = make(map[string]bool)
	hostName              string
	dataInterfacesToTry   = "bond0,eth0"
	dataInterface         string
	advertInterfacesToTry = "bond0,eth0"
	advertInterface       string
	listenAddress         string
	ifaceToIp             = make(map[string]string)

	portPicker = struct {
		rangeStart int
		rangeEnd   int
		rangeSize  int
		counter    int
		inUse      map[int]bool
	}{rangeStart: 26000, rangeEnd: 26999, inUse: make(map[int]bool)}
)

func init() {
	err := initInterfaceResolver()
	if err != nil {
		log.Fatalln(err)
	}

	flag.Var(&baseMCastGroup, "base", "Base multicast group (each feed increments the 3rd octet)")
	flag.StringVar(&listenAddress, "listen", listenAddress, "TCP/IP listen address")
	flag.IntVar(&portPicker.rangeStart, "start", portPicker.rangeStart, "Port range start")
	flag.IntVar(&portPicker.rangeEnd, "end", portPicker.rangeEnd, "Port range end")
	flag.StringVar(&dataInterfacesToTry, "i", dataInterfacesToTry,
		"Comma-separated list of network interfaces to try for multicasting data")
	flag.StringVar(&advertInterfacesToTry, "I", advertInterfacesToTry,
		"Comma-separated list of network interfaces to try for multicasting adverts")
}

func (ip *IP) String() string {
	return fmt.Sprint((*net.IP)(ip))
}

func (ip *IP) Set(val string) error {
	*ip = IP(net.ParseIP(val).To4())
	return nil
}

func initializePostFlagsParsed() error {
	err := setDataInterface()
	if err != nil {
		return err
	}

	err = setAdvertInterface()
	if err != nil {
		return err
	}

	return setListenAddress()
}

func getMcastAddrFor(name string) (string, error) {
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
		return "", errors.New("error: exhausted multicast addresses")
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

	return 0, fmt.Errorf("error: port range leaked or too narrow - in use: %d, range: %d-%d",
		len(portPicker.inUse), portPicker.rangeStart, portPicker.rangeEnd)
}

func releasePort(portNumber int) {
	addrLock.Lock()
	defer addrLock.Unlock()

	log.Println("releasing port: ", portNumber)
	delete(portPicker.inUse, portNumber)
}

func initInterfaceResolver() error {
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
		if (iface.Flags & net.FlagLoopback) != 0 {
			continue
		}

		addrs, err := iface.Addrs()
		if err != nil || len(addrs) == 0 || (iface.Flags&net.FlagMulticast) == 0 {
			continue
		}

		ifaceToIp[iface.Name] = strings.Split(addrs[0].String(), "/")[0]
	}

	return nil
}

func setDataInterface() error {
	if dataInterface != "" {
		return nil
	}

	for _, dataInterfaceToTry := range strings.Split(dataInterfacesToTry, ",") {
		if _, ok := ifaceToIp[dataInterfaceToTry]; ok {
			dataInterface = dataInterfaceToTry
			log.Println("setting data interface to: ", dataInterface)
			return nil
		} else {
			log.Println("unknown data interface: ", dataInterfaceToTry)
		}
	}

	return errors.New("error: no valid data interface, tried: " + dataInterfacesToTry)
}

func setAdvertInterface() error {
	if advertInterface != "" {
		return nil
	}

	for _, advertInterfaceToTry := range strings.Split(advertInterfacesToTry, ",") {
		if _, ok := ifaceToIp[advertInterfaceToTry]; ok {
			advertInterface = advertInterfaceToTry
			log.Println("setting advert interface to: ", advertInterface)
			return nil
		} else {
			log.Println("unknown advert interface: ", advertInterfaceToTry)
		}
	}

	return errors.New("error: no valid advert interface, tried: " + advertInterfacesToTry)
}

func setListenAddress() error {
	if listenAddress != "" {
		log.Println("listen address already defined as: ", listenAddress)
		return nil
	}

	err := setDataInterface()
	if err != nil {
		return err
	}

	listenAddress = ifaceToIp[dataInterface]
	log.Println("setting listen address to: ", listenAddress)
	return nil
}
