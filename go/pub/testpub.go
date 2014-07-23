package main

// #include "../../status.h"
// #include "../../storage.h"
// #include "../../error.h"
// #cgo LDFLAGS: -L../.. -lcachester -lrt
import "C"
import (
	"log"
	"os"
)

func main() {
	var cs *C.char
	if len(os.Args) > 1 {
		cs = C.CString(os.Args[1])
	} else {
		cs = nil
	}
	var store C.storage_handle
	if status := C.storage_create(&store, cs, 100, 0, 1000, 100); status != 0 {
		failError()
	}
	if status := C.storage_reset(store); status != 0 {
		failError()
	}
	log.Println("Success!", store)
}

func failError() {
	str := C.GoString(C.error_last_desc())
	if str == "" {
		return
	}
	log.Panic(str) // exits with stack
}
