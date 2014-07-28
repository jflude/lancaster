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
)

var qSize = flag.Uint("qsize", 1<<20, "Queue size for ring buffer")
var file = flag.String("file", "", "File to write to")
var httpAddr = flag.String("http", ":8080", "Http listener")

type Receiver struct {
	Host string
	rcvr C.receiver_handle
}

func (r *Receiver) Stats() map[string]interface{} {
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

var State struct {
	Receivers []Receiver
}

func main() {
	flag.Parse()
	for _, host := range flag.Args() {
		parts := strings.Split(host, ":")
		fmt.Println("parts", parts)
		if len(parts) != 2 {
			fail("Expected host:port, but got:", host)
		}
		var addr string
		var port int
		if parts[0] != "" {
			addr = parts[0]
		}
		port, err := strconv.Atoi(parts[1])
		if err != nil {
			fail(err)
		}
		var rcvr C.receiver_handle
		if *file == "" {
			fail("Expected filename")
		}
		mapName := fmt.Sprintf("%s-%s", file, host)
		fn := C.CString(mapName)
		ip, err := net.ResolveIPAddr("ip", addr)
		if err != nil {
			fail(err.Error())
		}
		tcp_addr := C.CString(ip.String())
		if err := chkStatus(C.receiver_create(&rcvr, fn, C.uint(*qSize), tcp_addr, C.int(port))); err != nil {
			fail("Failed to start: %s", err)
		}
		State.Receivers = append(State.Receivers, Receiver{rcvr: rcvr, Host: host})
		log.Println("Started receiver:", host, " file:", mapName)
	}
	http.HandleFunc("/", httpHandler)
	http.ListenAndServe(*httpAddr, nil)
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
		  <th>TCP Gaps</th>
		  <th>TCP Bytes Rec</th> 
		  <th>MCAST Bytes Rec</th>
		  <th>MCAST Packets Rec</th>
		  <th>MCAST Min Latency</th>
		  <th>MCAST Max Latency</th>
		  <th>MCAST Mean Latency</th>
		  <th>MCAST StdDev Latency</th>
		</tr>
		{{range .Receivers}}  
		{{ $s := .Stats }}
		<tr>
			<td>{{.Host}}</td>
			<td>{{$s.tcp_gap_count}}</td>
			<td>{{$s.tcp_bytes_recv  | bytes}}</td> 
			<td>{{$s.mcast_bytes_recv  | bytes}}</td>
			<td>{{$s.mcast_packets_recv}}</td>
			<td>{{$s.mcast_min_latency}}</td>
			<td>{{$s.mcast_max_latency}}</td>
			<td>{{$s.mcast_mean_latency}}</td>
			<td>{{$s.mcast_stddev_latency | _4Dec }}</td>
		</tr>
		{{end}}
	</table>
	</body>
</html>
`
