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
	rcvr     C.receiver_handle
	store    C.storage_handle
	lock     sync.Mutex
	Address  string
	IP       *net.IPAddr
	Host     string
	Port     int
	Stats    Stats
	Alive    bool
	Status   string
	FileName string
}

type Stats struct {
	GapCount           uint64
	HighWaterId        int64
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

func (r *Receiver) Start() {
	for {
		if !r.Alive {
			r.reset(false)
		} else {
			r.Alive = C.receiver_is_running(r.rcvr) != 0
			if !r.Alive {
				r.Stats = Stats{}
				r.Status = "Not Running"
				str := C.GoString(C.error_last_desc())
				log.Println(r.Address, "died, last error:", str)
			} else {
				r.updateStats()
			}
		}
		time.Sleep(time.Second)
	}
}
func (r *Receiver) reset(doLock bool) error {
	if doLock {
		r.lock.Lock()
		defer r.lock.Unlock()
	}
	r.Status = "Resetting"
	if r.rcvr != nil {
		r.Alive = C.receiver_is_running(r.rcvr) != 0
		if r.Alive {
			if err := chkStatus(C.receiver_stop(r.rcvr)); err != nil {
				r.Status = "Stop Failed, reason: " + err.Error()
				return err
			}
			r.Alive = false
		}
		C.receiver_destroy(&r.rcvr)
	}

	err := r.start()
	if err != nil {
		r.Status = "Start Failed, reason: " + err.Error()
		return err
	}
	return nil
}

func (r *Receiver) updateStats() {
	var s Stats
	s.HighWaterId = int64(C.storage_get_high_water_id(r.store))
	s.GapCount = uint64(C.receiver_get_tcp_gap_count(r.rcvr))
	s.TcpBytesRecv = uint64(C.receiver_get_tcp_bytes_recv(r.rcvr))
	s.MCastBytesRecv = uint64(C.receiver_get_mcast_bytes_recv(r.rcvr))
	s.MCastPacketsRecv = uint64(C.receiver_get_mcast_packets_recv(r.rcvr))
	s.MCastMinLatency = float64(C.receiver_get_mcast_min_latency(r.rcvr))
	s.MCastMaxLatency = float64(C.receiver_get_mcast_max_latency(r.rcvr))
	s.MCastMeanLatency = float64(C.receiver_get_mcast_mean_latency(r.rcvr))
	s.MCastStdDevLatency = float64(C.receiver_get_mcast_stddev_latency(r.rcvr))
	s.lastUpdate = time.Now()
	dur := uint64(s.lastUpdate.Sub(r.Stats.lastUpdate).Seconds())
	s.TcpBytesSec = (s.TcpBytesRecv - r.Stats.TcpBytesRecv) / dur
	s.MCastPacketsSec = (s.MCastPacketsRecv - r.Stats.MCastPacketsRecv) / dur
	s.MCastBytesSec = (s.MCastBytesRecv - r.Stats.MCastBytesRecv) / dur
	r.Stats = s
}

func newReceiver(addr string) (*Receiver, error) {
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
	ip, err := net.ResolveIPAddr("ip", host)
	if err != nil {
		return nil, err
	}
	return &Receiver{
		Host:     host,
		Address:  addr,
		IP:       ip,
		Port:     port,
		Alive:    false,
		FileName: fmt.Sprintf("%s-%s", file, addr),
	}, nil
}

func (r *Receiver) start() error {
	fn := C.CString(r.FileName)
	tcp_addr := C.CString(r.IP.String())
	log.Println("Connecting to:", r.Address)
	err := chkStatus(C.receiver_create(&(r.rcvr), fn, C.uint(qSize), tcp_addr, C.int(r.Port)))
	if err != nil {
		log.Println("Failed to connect to:", r.Address, "reason:", err)
		return err
	}
	r.store = C.receiver_get_storage(r.rcvr)
	log.Println("Started receiver:", r.Address, " file:", r.FileName)
	r.Alive = true
	return nil
}

func chkStatus(status C.status) error {
	if status == 0 {
		return nil
	}
	str := strings.TrimSpace(C.GoString(C.error_last_desc()))
	return fmt.Errorf("%d: %s", status, errors.New(str))
}
