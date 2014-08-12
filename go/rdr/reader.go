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
	"log"
	"os"
	"strings"
)

var store C.storage_handle
var qCapacity C.size_t
var rSize C.uint

func main() {
	var cs *C.char
	if len(os.Args) > 1 {
		cs = C.CString(os.Args[1])
	} else {
		cs = nil
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
	if strings.ToUpper(os.Getenv("TYPE")) == "API" {
		log.Println("Using API")
		runWithAPI()
	} else {
		log.Println("Using direct")
		runDirect()
	}
}

func err(status C.status) error {
	if status == 0 {
		return nil
	}
	str := C.GoString(C.error_last_desc())
	return fmt.Errorf("%d: %s", status, errors.New(str))
}
