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

type TQUOTE struct {
	name     [32]byte
	bidPrice int64
	askPrice int64
	bidSize  int32
	askSize  int32
	opraSeq  uint32
}

func runDirect() {
	var storeOwner = C.storage_get_owner_pid(store)
	var qSize = int(qCapacity)
	var qMask = qSize - 1
	var qHeadPtr = (*C.uint)((unsafe.Pointer(C.storage_get_queue_head_address(store))))
	var qBasePtr = unsafe.Pointer(C.storage_get_queue_base_address(store))
	var qArr = (*[1 << 30]C.identifier)(qBasePtr)
	// Used to grab the record itself
	var rBaseAddr = (unsafe.Pointer(C.storage_get_array(store)))
	// var rArr = (*[1 << 30]C.record_handle)(rBaseAddr)
	var rSize = C.storage_get_record_size(store)
	var vOffset = uintptr(C.storage_get_value_offset(store))
	var rBaseId = C.storage_get_base_id(store)
	var new_head, old_head int
	keys := make([]*string, qSize)
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
		}
		if (new_head - old_head) > qSize {
			old_head = new_head - qSize
		}
		for j := old_head; j < new_head; j++ {
			id := qArr[j&qMask]
			if id == -1 {
				fmt.Println("-1")
				continue
			}
			uraddr := (uintptr(rBaseAddr) + (uintptr(rSize) * uintptr((id - rBaseId))))
			raddr := (unsafe.Pointer)(uraddr)
			vaddr := (unsafe.Pointer)(uraddr + vOffset)
			d := (*TQUOTE)(vaddr) //(*C.datum)(vaddr)
			for {
				seq := *((*C.uint)(raddr))
				if seq < 0 {
					fmt.Println("locked")
					continue
				}
				nseq := *((*C.uint)(raddr))
				if seq == nseq {
					break
				}
				fmt.Println("Version stomp", seq, "!=", nseq)
			}
			key := keys[id]
			if key == nil {
				s := string(d.name[:])
				key = &s
				keys[id] = key
			}
			fmt.Println(*key, float64(d.bidPrice)/10000.0, d.bidSize, float64(d.askPrice)/10000.0, d.askSize, d.opraSeq)
		}
		old_head = new_head
	}
}
