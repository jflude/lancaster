package main

// #include "../../status.h"
// #include "../../storage.h"
// #include "../../error.h"
// #include "../../receiver.h"
// #cgo LDFLAGS: ../../libcachester.a -lrt -lm -w
import "C"

import (
	"errors"
	"flag"
	"fmt"
	"github.com/dustin/go-humanize"
	"html/template"
	"log"
	"net"
	"net/http"
	"os"
	"strconv"
	"strings"
	"time"
)

var qSize uint
var file string
var httpAddr string

var State struct {
	Receivers []*Receiver
}

func init() {
	flag.StringVar(&file, "file", "shm:cachester", "Base filename for streams (host and port is appended)")
	flag.UintVar(&qSize, "qsize", 1<<20, "Size of update queue")
	flag.StringVar(&httpAddr, "http", ":8080", "HTTP Listen Address")
}

type Receiver struct {
	Host  string
	rcvr  C.receiver_handle
	Stats Stats
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

func (r *Receiver) updateStats() {
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

/*func (r *Receiver) Stats() map[string]interface{} {
	ret := make(map[string]interface{})
	ret["tcp_gap_count"] = uint64(C.receiver_get_tcp_gap_count(r.rcvr))
	ret["tcp_bytes_recv"] = uint64(C.receiver_get_tcp_bytes_recv(r.rcvr))
	ret["mcast_bytes_recv"] = uint64(C.receiver_get_mcast_bytes_recv(r.rcvr))
	ret["mcast_packets_recv"] = uint64(C.receiver_get_mcast_packets_recv(r.rcvr))
	ret["mcast_min_latency"] = float64(C.receiver_get_mcast_min_latency(r.rcvr))
	ret["mcast_max_latency"] = float64(C.receiver_get_mcast_max_latency(r.rcvr))
	ret["mcast_mean_latency"] = float64(C.receiver_get_mcast_mean_latency(r.rcvr))
	ret["mcast_stddev_latency"] = float64(C.receiver_get_mcast_stddev_latency(r.rcvr))
	return ret
}
*/
func main() {
	flag.Parse()
	if file == "" {
		fail("Expected -file")
	}
	for _, host := range flag.Args() {
		if err := startReceiver(host); err != nil {
			fail(err)
		}
	}

	http.HandleFunc("/", httpHandler)
	go http.ListenAndServe(httpAddr, nil)
	for {
		time.Sleep(time.Second)
		for _, r := range State.Receivers {
			r.updateStats()
		}
	}
}

func startReceiver(addr string) error {
	parts := strings.Split(addr, ":")
	if len(parts) != 2 {
		return fmt.Errorf("Expected host:port, but got: %s", addr)
	}
	port, err := strconv.Atoi(parts[1])
	if err != nil {
		return err
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
		return err
	}
	tcp_addr := C.CString(ip.String())
	if err := chkStatus(C.receiver_create(&rcvr, fn, C.uint(qSize), tcp_addr, C.int(port))); err != nil {
		return err
	}
	State.Receivers = append(State.Receivers, &Receiver{rcvr: rcvr, Host: host})
	log.Println("Started receiver:", host, " file:", mapName)
	return nil
}

func httpHandler(w http.ResponseWriter, r *http.Request) {
	err := statusTemplate.Execute(w, &State)
	if err != nil {
		log.Println(err)
		// http.Error(w, err.Error(), 500)
	}
}
func fail(args ...interface{}) {
	fmt.Fprintln(os.Stderr, args...)
	os.Exit(1)
}
func chkStatus(status C.status) error {
	if status == 0 {
		return nil
	}
	str := C.GoString(C.error_last_desc())
	return fmt.Errorf("%d: %s", status, errors.New(str))
}

func _4Dec(v float64) string {
	return fmt.Sprintf("%0.4f", v)
}

var funcMap = template.FuncMap{
	"_4Dec": _4Dec,
	"bytes": humanize.Bytes,
}
var statusTemplate = template.Must(template.New("status").Funcs(funcMap).Parse(statusTemplateText))
var statusTemplateText = `
<html>
	<head>
		<title>Receiver Status</title>
		<meta http-equiv="refresh" content="1">
		<style>
			table,th,td {
				border:1px solid black;
			}
		</style>
	</head>
	<body>
	<table style="width:300px">
		<tr>
		  <th>Host</th>
		  <th>Gaps</th>
		  <th>TCP Bytes Rec</th> 
		  <th>TCP Bytes / sec</th> 
		  <th>MCAST Bytes Rec</th>
		  <th>MCAST Bytes / sec</th>
		  <th>MCAST Packets Rec</th>
		  <th>MCAST Packets / sec</th>
		  <th>MCAST Min Latency</th>
		  <th>MCAST Max Latency</th>
		  <th>MCAST Mean Latency</th>
		  <th>MCAST StdDev Latency</th>
		</tr>
		{{range .Receivers}}  
		{{ $s := .Stats }}
		<tr>
			<td>{{.Host}}</td>
			<td>{{$s.GapCount}}</td>
			<td>{{$s.TcpBytesRecv  | bytes}}</td> 
			<td>{{$s.TcpBytesSec  | bytes}}</td> 
			<td>{{$s.MCastBytesRecv  | bytes}}</td>
			<td>{{$s.MCastBytesSec  | bytes}}</td>
			<td>{{$s.MCastPacketsRecv}}</td>
			<td>{{$s.MCastPacketsSec}}</td>
			<td>{{$s.MCastMinLatency}}</td>
			<td>{{$s.MCastMaxLatency}}</td>
			<td>{{$s.MCastMeanLatency}}</td>
			<td>{{$s.MCastStdDevLatency | _4Dec }}</td>
		</tr>
		{{end}}
	</table>
	</body>
</html>
`
