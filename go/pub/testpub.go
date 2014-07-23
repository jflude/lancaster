package main

// #include "../../status.h"
// #include "../../storage.h"
// #include "../../error.h"
// #cgo LDFLAGS: -L../.. -lcachester -lrt
import "C"
import (
	"errors"
	"fmt"
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
	if err := err(C.storage_create(&store, cs, 128, 0, 1000, 100)); err != nil {
		log.Panic(err)
	}
	if err := err(C.storage_reset(store)); err != nil {
		log.Panic(err)
	}
	log.Println("Success!", store)
}

func err(status C.status) error {
	if status == 0 {
		return nil
	}
	str := C.GoString(C.error_last_desc())
	if str == "" {
		return nil
	}
	return fmt.Errorf("%d: %s", status, errors.New(str))
}
