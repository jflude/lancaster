package main

import (
	"encoding/json"
	_ "expvar"
	"flag"
	"fmt"
	"log"
	"net/http"
	"net/url"
	"os"
	"os/signal"
	"strconv"
	"strings"
	"sync"
	"syscall"
)

var qSize uint
var file string
var httpAddr string
var STOP_RUNNING = make(chan struct{})
var waitFor sync.WaitGroup

var State struct {
	Receivers map[string]*Receiver
}

func init() {
	State.Receivers = make(map[string]*Receiver)
	flag.StringVar(&file, "file", "shm:cachester", "Base filename for streams (host and port is appended)")
	flag.UintVar(&qSize, "qsize", 1<<20, "Size of update queue")
	flag.StringVar(&httpAddr, "http", ":8080", "HTTP Listen Address")
}

func stopper() {
	ch := make(chan os.Signal, 1)
	signal.Notify(ch, syscall.SIGINT, syscall.SIGTERM)
	sig := <-ch
	close(STOP_RUNNING)
	log.Println("Shutting down due to signal:", sig)
	waitFor.Done()
}

func main() {
	waitFor.Add(1)
	flag.Parse()
	if file == "" {
		fail("Expected -file")
	}
	for _, host := range flag.Args() {
		if err := addReceivers(host); err != nil {
			log.Fatal(err)
		}

	}
	http.HandleFunc("/status", statusHandler)
	http.HandleFunc("/add", addHandler)
	http.HandleFunc("/remove", removeHandler)
	http.HandleFunc("/reset", resetHandler)
	http.Handle("/", http.FileServer(http.Dir("assets")))
	if enableMMD {
		initMMD()
	}
	go func() {
		if err := http.ListenAndServe(httpAddr, nil); err != nil {
			log.Fatal(err)
		}
	}()
	go stopper()
	waitFor.Wait()
	log.Println("Shutdown complete")
}

func addReceivers(addr string) error {
	parts := strings.Split(addr, ":")
	if len(parts) != 2 {
		return fmt.Errorf("Expected host:port0[-portN], but got: %s", addr)
	}

	var begin int
	var end int
	var err error
	prange := strings.SplitN(parts[1], "-", 2)
	if len(prange) == 2 {
		begin, err = strconv.Atoi(prange[0])
		if err != nil {
			return err
		}
		end, err = strconv.Atoi(prange[1])
		if err != nil {
			return err
		}

	} else {
		begin, err := strconv.Atoi(prange[0])
		if err != nil {
			return err
		}
		end = begin
	}
	for x := begin; x <= end; x++ {
		r, err := newReceiver(fmt.Sprintf("%s:%d", parts[0], x))
		if err != nil {
			fail(err)
		}
		go r.Start()
		State.Receivers[r.Address] = r
	}
	return nil
}
func reset(addr string) error {
	r, ok := State.Receivers[addr]
	if ok {
		r.Reset()
	}
	return fmt.Errorf("No such connection: %s", addr)
}
func resetHandler(w http.ResponseWriter, r *http.Request) {
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
	err := rec.Reset()
	if err != nil {
		http.Error(w, err.Error(), 500)
		return
	}
	fmt.Fprint(w, "ok")
	return
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
