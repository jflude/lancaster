package main

import (
	"encoding/json"
	_ "expvar"
	"flag"
	"fmt"
	"github.com/dustin/go-humanize"
	"html/template"
	"log"
	"net/http"
	"net/url"
	"os"
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
		r, err := newReceiver(host)
		if err != nil {
			fail(err)
		}
		go r.Start()
		State.Receivers[r.Address] = r
	}
	http.HandleFunc("/status", statusHandler)
	http.HandleFunc("/add", addHandler)
	http.HandleFunc("/remove", removeHandler)
	http.HandleFunc("/s", httpHandler)
	http.Handle("/", http.FileServer(http.Dir("assets")))
	if enableMMD {
		initMMD()
	}
	// This blocks forever
	http.ListenAndServe(httpAddr, nil)
}

func reset(addr string) error {
	r, ok := State.Receivers[addr]
	if ok {
		err := r.reset(true)
		if err != nil {
			log.Println("Failed to restart", err)
			return err
		}
	}
	return fmt.Errorf("No such connection: %s", addr)
}
func removeHandler(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	h := q["host"]
	if len(h) == 0 {
		http.Error(w, "Must specify 'host' parameter", 400)
		return
	}
	host := h[0]
	rec, ok := State.Receivers[host]
	if !ok {
		http.Error(w, "Not found", 404)
		return
	}
	err := rec.Stop()
	if err != nil {
		log.Println("Failed to remove host:", w, err)
		http.Error(w, err.Error(), 400)
		return
	}
	delete(State.Receivers, host)
	fmt.Fprint(w, "ok")
	log.Println("Removed:", host)
}
func addHandler(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	h := q["host"]
	if len(h) == 0 {
		http.Error(w, "Must specify 'host' parameter", 400)
		return
	}
	host := h[0]
	if _, ok := State.Receivers[host]; ok {
		http.Error(w, "Already exists", 400)
		return
	}

	rec, err := newReceiver(host)
	if err != nil {
		log.Println("Failed to add host:", w, err)
		http.Error(w, err.Error(), 400)
		return
	}
	go rec.Start()
	State.Receivers[rec.Address] = rec
	fmt.Fprint(w, "ok")
	log.Println("Added:", host)
}
func statusHandler(w http.ResponseWriter, r *http.Request) {
	json.NewEncoder(w).Encode(State.Receivers)
}
func httpHandler(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	resetAddr := arg(q, "reset")
	if resetAddr != "" {
		reset(resetAddr)
	}
	a, err := Asset("s.html")
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
