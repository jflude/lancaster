package main

import (
	"fmt"
	"os"
	"strconv"

	"github.peak6.net/platform/cachester/go/lib/cachester"
)

func stderr(args ...interface{}) {
	fmt.Fprintln(os.Stderr, args...)
}
func die(args ...interface{}) {
	stderr(args...)
	stderr()
	os.Exit(1)
}
func main() {
	if len(os.Args) < 3 {
		die("Usage:", os.Args[0], "FILE ID [ID...]")
	}
	if cs, err := cachester.OpenFile(os.Args[1]); err != nil {
		die("Failed to open:", os.Args[1], "reason", err)
	} else {
		defer cs.Close()
		buff := make([]byte, cs.GetRecordSize())
		for _, idxStr := range os.Args[2:] {
			if idx, err := strconv.ParseInt(idxStr, 10, 64); err != nil {
				stderr("Invalid index:", idxStr)
			} else {
				if _, err := cs.GetRecord(idx, buff); err != nil {
					stderr("Failed to read record:", idx, "reason:", err)
				} else {
					os.Stdout.Write(buff)
				}
			}
		}
	}

}
