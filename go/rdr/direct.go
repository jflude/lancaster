package main

// #include "../../status.h"
// #include "../../storage.h"
// #include "../../error.h"
// #include "../../datum.h"
import "C"
import (
	"fmt"
	"log"
	"syscall"
	"time"
	"unsafe"
)

func runDirect() {
	var storeOwner = C.storage_get_owner_pid(store)
	var qSize = int(qCapacity)
	var qMask = qSize - 1
	var qHeadPtr = (*C.long)((unsafe.Pointer(C.storage_get_queue_head_ref(store))))
	var qBasePtr = unsafe.Pointer(C.storage_get_queue_base_ref(store))
	var qArr = (*[1 << 30]C.identifier)(qBasePtr)
	// Used to grab the record itself
	var rBaseAddr = (unsafe.Pointer(C.storage_get_array(store)))
	// var rArr = (*[1 << 30]C.record_handle)(rBaseAddr)
	var rSize = C.storage_get_record_size(store)
	var vOffset = uintptr(C.storage_get_value_offset(store))
	var rBaseId = C.storage_get_base_id(store)
	var old_head = int((*(qHeadPtr)))
	var new_head = old_head
	c := '/'
	var lastXyz = C.long(0)
	x := 0
	for {
		new_head = int(*(qHeadPtr))
		if new_head == old_head {
			// time.Sleep(time.Microsecond)
			time.Sleep(time.Nanosecond)
			if err := syscall.Kill(int(storeOwner), 0); err != nil {
				errno := int(err.(syscall.Errno))
				if err == syscall.ESRCH {
					log.Println("Subscriber (", storeOwner, ") died")
				}
				if err != syscall.ESRCH {
					log.Println("Error checkpid:", storeOwner, errno, err)
				}
				return
			}
			// fmt.Println("sleep", new_head)
			continue
		} else if old_head > new_head {
			log.Println(old_head, ">", new_head)
			continue
		}
		if (new_head - old_head) > qSize {
			old_head = new_head - qSize
			c = '*'
		}
		var xyz C.long
		for j := old_head; j < new_head; j++ {
			id := qArr[j&qMask]
			if id == -1 {
				fmt.Println("-1")
				continue
			}
			uraddr := (uintptr(rBaseAddr) + (uintptr(rSize) * uintptr((id - rBaseId))))
			raddr := (unsafe.Pointer)(uraddr)
			vaddr := (unsafe.Pointer)(uraddr + vOffset)
			d := (*C.datum)(vaddr)
			var seq C.uint
			for {
				seq = *((*C.uint)(raddr))
				if seq < 0 {
					fmt.Println("locked")
					continue
				}
				xyz = d.xyz
				nseq := *((*C.uint)(raddr))
				if seq == nseq {
					break
				}
				fmt.Println("Version stomp", seq, "!=", nseq)
			}

			if xyz != lastXyz+1 {
				if c == '.' {
					c = '!'
				}
				log.Println("\nid", id, "xyz", lastXyz, xyz, "diff:", (xyz - lastXyz), "qpos:", j, "masked:", j&qMask, "seq", seq)
			}
			lastXyz = xyz
		}
		old_head = new_head
		x++
		if x&1023 == 0 {
			fmt.Print(string(c)) //, "pass", x, "good", good, "old", old_head, "new", new_head, "new-old", new_head-old_head)
			c = '.'
		}
	}
}
