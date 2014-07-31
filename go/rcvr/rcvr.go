package main

import (
	_ "expvar"
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
		nr, err := r.reset()
		if err != nil {
			log.Println("Failed to restart", err)
		} else {
			State.Receivers[addr] = nr
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
	a, err := Asset("index.html")
	if err != nil {
		http.Error(w, err.Error(), 500)
		return
	}
	rootTemplate := template.New("root").Funcs(funcMap)
	t, err := rootTemplate.Parse(string(a))
	if err != nil {
		http.Error(w, err.Error(), 500)
		return
	}
	err = t.Execute(w, &State)
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
	"micros": func(f float64) float64 { return f },
}