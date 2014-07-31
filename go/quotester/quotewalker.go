package main

// #include "../../status.h"
// #include "../../storage.h"
// #include "../../error.h"
// #include "../../datum.h"
import "C"
import (
	"bytes"
	"fmt"
	"log"
	"syscall"
	"time"
	"unsafe"
)

type Quote struct {
	keyBytes   [32]byte
	bidPrice   int64
	askPrice   int64
	bidSize    int32
	askSize    int32
	exchangeTS uint64
	opraSeq    uint32
}

func (q *Quote) key() string {
	i := bytes.IndexByte(q.keyBytes[:], 0)
	if i < 0 {
		return string(q.keyBytes[:])
	}
	return string(q.keyBytes[:i])
}
func (q *Quote) bid() float64 {
	return float64(q.bidPrice) / 10000.0
}
func (q *Quote) ask() float64 {
	return float64(q.askPrice) / 10000.0
}
func (q *Quote) String() string {

	t := time.Unix(int64(q.exchangeTS/1000000), int64((q.exchangeTS%1000000)*1000))

	return fmt.Sprintf("%-28s %-15s  %9d %4d x %03.2f @ %0.2f x %d", q.key(), t.Format("15:04:05.999999"), q.opraSeq, q.bidSize, q.bid(), q.ask(), q.askSize)
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
			time.Sleep(time.Microsecond)
			// time.Sleep(time.Nanosecond)
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
			d := (*Quote)(vaddr) //(*C.datum)(vaddr)
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
				s := d.key()
				key = &s
				keys[id] = key
				if watchKeys != nil && watchKeys[s] {
					watchBits.Set(uint(id))
				}
			}
			// fmt.Println(*key, float64(d.bidPrice)/10000.0, d.bidSize, float64(d.askPrice)/10000.0, d.askSize, d.exchangeTS, d.opraSeq)
			if watchKeys != nil {
				if watchBits.Test(uint(id)) {
					fmt.Println(j, j&qMask, d)
				}
			} else {
				fmt.Println(j, j&qMask, d)
			}
		}
		old_head = new_head
	}
}
