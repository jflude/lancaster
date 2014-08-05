package main

// #include "../../status.h"
// #include "../../storage.h"
// #include "../../error.h"
// #include "../../datum.h"
// #cgo LDFLAGS: ../../libcachester.a -lrt -w
import "C"
import (
	"errors"
	"flag"
	"fmt"
	"log"
	"os"
)

var store C.storage_handle
var tail bool

func init() {
	flag.BoolVar(&tail, "t", false, "Tail feed")
}
func main() {
	flag.Parse()
	args := flag.Args()
	var cs *C.char
	if len(args) == 0 {
		flag.Usage()
		fmt.Fprintln(os.Stderr, "Must specify a file")
		os.Exit(1)
	}
	cs = C.CString(args[0])
	if err := chkStatus(C.storage_open(&store, cs)); err != nil {
		log.Fatal("Failed to open store", err)
	}
	defer func() { C.storage_destroy(&store) }()
	err := startFS()
	if err != nil {
		log.Fatal("Failed to start QuoteFS:", err)
	}
	args = args[1:]

	if tail {
		tailQuotes(args)
	} else {
		if len(args) < 1 {
			flag.Usage()
		} else {
			findQuotes(args)
		}
	}
	<-fsComplete
}

func chkStatus(status C.status) error {
	if status == 0 {
		return nil
	}
	str := C.GoString(C.error_last_desc())
	return fmt.Errorf("%d: %s", status, errors.New(str))
}
