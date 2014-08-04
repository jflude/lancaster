package main

// #include "../../status.h"
// #include "../../storage.h"
// #include "../../error.h"
// #include "../../datum.h"
import "C"
import (
	"fmt"
	"log"
	"time"
)

func runWithAPI() {
	var qSize = qCapacity
	var old_head C.uint
	c := '/'
	var lastBidSize = C.int(0)
	for x := 0; ; x++ {
		new_head := C.storage_get_queue_head(store)
		if new_head == old_head {
			time.Sleep(time.Nanosecond)
			// fmt.Println("sleep", new_head)
			continue
		}
		if (new_head - old_head) > qSize {
			old_head = new_head - qSize
			c = '*'
		}
		var bidSz C.int
		for j := old_head; j < new_head; j++ {
			var rec C.record_handle
			id := C.storage_read_queue(store, j)
			if id == -1 {
				fmt.Println("-1")
				continue
			}

			if err := err(C.storage_get_record(store, id, &rec)); err != nil {
				log.Println("Error looking up", id, err)
				return
			}

			d := (*C.datum)(C.record_get_value_ref(rec))

			for {
				seq := C.record_read_lock(rec)
				if seq < 0 {
					fmt.Println("locked")
					continue
				}
				bidSz = d.bidSize
				nseq := C.record_read_lock(rec)
				if seq == nseq {
					break
				}
				fmt.Println("Version stomp", seq, "!=", nseq)
			}

			if bidSz != lastBidSize+2 {
				if c == '.' {
					c = '!'
				}
				// fmt.Println(bidSz, lastBidSize, bidSz-lastBidSize)
			}
			lastBidSize = bidSz
		}
		old_head = new_head
		if x&1023 == 0 {
			fmt.Print(string(c)) //, "pass", x, "good", good, "old", old_head, "new", new_head, "new-old", new_head-old_head)
			c = '.'
		}
	}
}
