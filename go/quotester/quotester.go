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
	"sync"
)

var tail bool

type filters []string

var find filters
var verbose bool

func (f *filters) String() string {
	return fmt.Sprint([]string(*f))
}
func (f *filters) Set(val string) error {
	*f = append(*f, val)
	return nil
}

func init() {
	flag.BoolVar(&tail, "t", false, "Tail feed")
	flag.BoolVar(&verbose, "v", false, "Verbose")
	flag.Var(&find, "f", "Filter keys")
}
func main() {
	flag.Parse()
	args := flag.Args()
	if len(args) == 0 {
		flag.Usage()
		fmt.Fprintln(os.Stderr, "Must specify a file")
		os.Exit(1)
	}
	if len(find) > 0 {
		if verbose {
			log.Println("Filtering for:", find)
		}
	}
	var wg sync.WaitGroup
	for _, fn := range args {
		if verbose {
			log.Println("Loading:", fn)
		}
		cs, err := NewCachesterSource(fn)
		if err != nil {
			log.Fatal(err)
		}
		wg.Add(1)
		defer cs.Destroy()
		if tail {
			go func() {
				cs.tailQuotes([]string(find))
				wg.Done()
			}()
		} else {
			go func() {
				cs.findQuotes([]string(find))
				wg.Done()
			}()
		}
	}
	wg.Wait()
}

func chkStatus(status C.status) error {
	if status == 0 {
		return nil
	}
	str := C.GoString(C.error_last_desc())
	return fmt.Errorf("%d: %s", status, errors.New(str))
}
