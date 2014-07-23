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
	log.Println("stuff")
	cs := C.CString(os.Args[1])
	var store C.storage_handle
	status := C.storage_create(&store, cs, 128, 0, 1000, 100)
	log.Println(store, status)
	C.error_report_fatal()
	status = C.storage_reset(store)
	log.Println(store, status)
}
