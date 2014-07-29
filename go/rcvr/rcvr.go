package main

import (
	"flag"
	"fmt"
	"github.com/dustin/go-humanize"
	"html/template"
	"log"
	"net/http"
	"net/url"
	"os"
	"time"
)

var qSize uint
var file string
var httpAddr string

var State struct {
	Receivers map[string]*Receiver
}

func init() {
	State.Receivers = make(map[string]*Receiver)
	flag.StringVar(&file, "file", "shm:cachester", "Base filename for streams (host and port is appended)")
	flag.UintVar(&qSize, "qsize", 1<<20, "Size of update queue")
	flag.StringVar(&httpAddr, "http", ":8080", "HTTP Listen Address")
}

func main() {
	flag.Parse()
	if file == "" {
		fail("Expected -file")
	}
	for _, host := range flag.Args() {
		r, err := startReceiver(host)
		if err != nil {
			fail(err)
		}
		State.Receivers[r.Address] = r
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

func reset(addr string) error {
	r, ok := State.Receivers[addr]
	if ok {
		err := r.reset()
		if err != nil {
			log.Println("Failed to restart", err)
		}
	}
	return fmt.Errorf("No such connection: %s", addr)
}

func httpHandler(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	resetAddr := arg(q, "reset")
	if resetAddr != "" {
		reset(resetAddr)
	}
	err := statusTemplate.Execute(w, &State)
	if err != nil {
		log.Println(err)
		// http.Error(w, err.Error(), 500)
	}
}

func arg(v url.Values, name string) string {
	a, ok := v[name]
	if ok {
		return a[0]
	}
	return ""
}

func fail(args ...interface{}) {
	fmt.Fprintln(os.Stderr, args...)
	os.Exit(1)
}

var funcMap = template.FuncMap{
	"_2dec":  func(f float64) string { return fmt.Sprintf("%0.2f", f) },
	"bytes":  humanize.IBytes,
	"commas": humanize.Comma,
	"int64":  func(i uint64) int64 { return int64(i) },
	"f2i":    func(f float64) int64 { return int64(f) },
	"micros": func(f float64) float64 { return f / 1000.0 },
}
var statusTemplate = template.Must(template.New("status").Funcs(funcMap).Parse(statusTemplateText))
var statusTemplateText = `
<html>
	<head>
		<title>Receiver Status</title>
		<meta http-equiv="refresh" content="1;url=/">
		<style>
			table, th,td {
				border:1px solid black;
				border-collapse:collapse;
				padding:15px;
				border-spacing:5px;
			}
		</style>
	</head>
	<body>
	<table style="width:300px">
		<tr>
		  <th>Source</th>
		  <th>Gaps</th>
		  <th>TCP Bytes Rec</th> 
		  <th>TCP Bytes / sec</th> 
		  <th>MCAST Bytes Rec</th>
		  <th>MCAST Bytes / sec</th>
		  <th>MCAST Packets Rec</th>
		  <th>MCAST Packets / sec</th>
		  <th>MCAST Min Latency µ</th>
		  <th>MCAST Max Latency µ</th>
		  <th>MCAST Mean Latency µ</th>
		  <th>MCAST StdDev Latency µ</th>
		</tr>
		{{range $addr, $rec := .Receivers}}  
		{{ $s := $rec.Stats }}
		<tr>
			<td>{{$addr}}<br/><a href="/?reset={{$addr}}">reset</a></td>
			{{ if $rec.Alive }}
			<td>{{$s.GapCount}}</td>
			<td>{{$s.TcpBytesRecv | bytes}}</td> 
			<td>{{$s.TcpBytesSec | bytes}}</td> 
			<td>{{$s.MCastBytesRecv | bytes}}</td>
			<td>{{$s.MCastBytesSec | bytes}}</td>
			<td>{{$s.MCastPacketsRecv | int64 | commas}}</td>
			<td>{{$s.MCastPacketsSec | int64 | commas}}</td>
			<td>{{$s.MCastMinLatency | micros}}</td>
			<td>{{$s.MCastMaxLatency | micros}}</td>
			<td>{{$s.MCastMeanLatency | micros}}</td>
			<td>{{$s.MCastStdDevLatency | micros | _2dec }}</td>
			{{ else }}
			<td colspan="11" align="center">{{$rec.Status}}</td>
			{{ end }}
		</tr>
		{{end}}
	</table>
	</body>
</html>
`
