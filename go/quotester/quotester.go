package main

// #include "../../status.h"
// #include "../../storage.h"
// #include "../../error.h"
// #include "../../datum.h"
// #cgo LDFLAGS: ../../libcachester.a -lrt -w
import "C"
import (
	"errors"
	"fmt"
	"github.com/willf/bitset"
	"log"
	"os"
)

var store C.storage_handle
var qCapacity C.uint
var rSize C.uint
var watchKeys map[string]bool
var watchBits bitset.BitSet

func main() {
	var cs *C.char
	if len(os.Args) > 1 {
		cs = C.CString(os.Args[1])
	} else {
		log.Fatal("Must specify file")
	}
	if len(os.Args) > 2 {
		watchKeys = make(map[string]bool)
		for _, a := range os.Args[2:] {
			watchKeys[a] = true
		}
	}
	if err := err(C.storage_open(&store, cs)); err != nil {
		log.Fatal("Failed to open store", err)
	}
	defer func() { C.storage_destroy(&store) }()
	qCapacity = C.storage_get_queue_capacity(store)
	a := C.storage_get_record_size(store)
	b := C.storage_get_value_size(store)
	c := C.storage_get_value_offset(store)
	log.Println("rs", a, "vs", b, "vo", c)
	log.Println("Queue size:", qCapacity)
	if watchKeys != nil {
		log.Println("watchkeys:", watchKeys)
	}
	runDirect()
}

func err(status C.status) error {
	if status == 0 {
		return nil
	}
	str := C.GoString(C.error_last_desc())
	return fmt.Errorf("%d: %s", status, errors.New(str))
}
