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

var tail bool
var description string
var usePrints bool

func init() {
	flag.BoolVar(&tail, "t", false, "Tail feed")
}
func main() {
	flag.Parse()
	args := flag.Args()
	if len(args) == 0 {
		flag.Usage()
		fmt.Fprintln(os.Stderr, "Must specify a file")
		os.Exit(1)
	}
	cs, err := NewCachesterSource(args[0])
	if err != nil {
		log.Fatal(err)
	}
	// if err := chkStatus(C.storage_open(&store, cs)); err != nil {
	// 	log.Fatal("Failed to open store", err)
	// }
	defer cs.Destroy()
	// defer func() { C.storage_destroy(&store) }()
	// description = C.GoString(C.storage_get_description(store))
	usePrints = cs.Description == "PRINTS"
	log.Println("Description:", description)
	err = startFS(cs)
	if err != nil {
		log.Fatal("Failed to start QuoteFS:", err)
	}
	args = args[1:]

	if tail {
		cs.tailQuotes(args)
	} else {
		if len(args) < 1 {
			flag.Usage()
		} else {
			cs.findQuotes(args)
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
