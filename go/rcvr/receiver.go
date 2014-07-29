package main

// #include "../../status.h"
// #include "../../storage.h"
// #include "../../error.h"
// #include "../../receiver.h"
// #cgo LDFLAGS: ../../libcachester.a -lrt -lm -w
import "C"

import (
	"errors"
	"fmt"
	"log"
	"net"
	"strconv"
	"strings"
	"sync"
	"time"
)

type Receiver struct {
	Address string
	Host    string
	rcvr    C.receiver_handle
	Stats   Stats
	Alive   bool
	Status  string
	lock    sync.Mutex
}

type Stats struct {
	GapCount           uint64
	TcpBytesRecv       uint64
	TcpBytesSec        uint64
	MCastBytesRecv     uint64
	MCastBytesSec      uint64
	MCastPacketsRecv   uint64
	MCastPacketsSec    uint64
	MCastMinLatency    float64
	MCastMaxLatency    float64
	MCastMeanLatency   float64
	MCastStdDevLatency float64
	lastUpdate         time.Time
}

func (r *Receiver) reset() error {
	r.lock.Lock()
	defer r.lock.Unlock()
	r.Status = "Resetting"
	r.Alive = false
	if err := chkStatus(C.receiver_stop(r.rcvr)); err != nil {
		r.Status = "Stop Failed, reason: " + err.Error()
		return err
	}
	C.receiver_destroy(&r.rcvr)
	nr, err := startReceiver(r.Address)
	if err != nil {
		r.Status = "Start Failed, reason: " + err.Error()
		return err
	}
	*r = *nr
	return nil
}

func (r *Receiver) updateStats() {
	r.lock.Lock()
	defer r.lock.Unlock()

	if !r.Alive {
		return
	}
	r.Alive = C.receiver_is_running(r.rcvr) != 0
	if !r.Alive {
		r.Stats = Stats{}
		r.Status = "Not Running"
		return
	}
	var s Stats
	s.GapCount = uint64(C.receiver_get_tcp_gap_count(r.rcvr))
	s.TcpBytesRecv = uint64(C.receiver_get_tcp_bytes_recv(r.rcvr))
	s.MCastBytesRecv = uint64(C.receiver_get_mcast_bytes_recv(r.rcvr))
	s.MCastPacketsRecv = uint64(C.receiver_get_mcast_packets_recv(r.rcvr))
	s.MCastMinLatency = float64(C.receiver_get_mcast_min_latency(r.rcvr))
	s.MCastMaxLatency = float64(C.receiver_get_mcast_max_latency(r.rcvr))
	s.MCastMeanLatency = float64(C.receiver_get_mcast_mean_latency(r.rcvr))
	s.MCastStdDevLatency = float64(C.receiver_get_mcast_stddev_latency(r.rcvr))
	s.lastUpdate = time.Now()
	dur := s.lastUpdate.Sub(r.Stats.lastUpdate).Seconds()
	s.TcpBytesSec = uint64(float64(s.TcpBytesRecv-r.Stats.TcpBytesRecv) / dur)
	s.MCastPacketsSec = uint64(float64(s.MCastPacketsRecv-r.Stats.MCastPacketsRecv) / dur)
	s.MCastBytesSec = uint64(float64(s.MCastBytesRecv-r.Stats.MCastBytesRecv) / dur)
	r.Stats = s
}

func startReceiver(addr string) (*Receiver, error) {
	parts := strings.Split(addr, ":")
	if len(parts) != 2 {
		return nil, fmt.Errorf("Expected host:port, but got: %s", addr)
	}
	port, err := strconv.Atoi(parts[1])
	if err != nil {
		return nil, err
	}

	host := parts[0]
	if host == "" {
		host = "localhost"
	}

	var rcvr C.receiver_handle
	mapName := fmt.Sprintf("%s-%s", file, addr)
	fn := C.CString(mapName)
	ip, err := net.ResolveIPAddr("ip", host)
	if err != nil {
		return nil, err
	}
	tcp_addr := C.CString(ip.String())
	if err := chkStatus(C.receiver_create(&rcvr, fn, C.uint(qSize), tcp_addr, C.int(port))); err != nil {
		return nil, err
	}
	log.Println("Started receiver:", addr, " file:", mapName)
	return &Receiver{rcvr: rcvr, Host: host, Address: addr, Alive: true}, nil
}

func chkStatus(status C.status) error {
	if status == 0 {
		return nil
	}
	str := C.GoString(C.error_last_desc())
	return fmt.Errorf("%d: %s", status, errors.New(str))
}
