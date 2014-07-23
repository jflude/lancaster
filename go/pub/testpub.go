package main

// #include "../../status.h"
// #include "../../storage.h"
// #cgo LDFLAGS: -L../.. -lcachester -lrt
import "C"
import (
	"log"
)

func main() {
	log.Println("stuff")
	cs := C.CString("./foo.store")
	var store C.storage_handle
	status := C.storage_create(&store, cs, 0, 0, 1000, 100)
	log.Println(store, status)
}
