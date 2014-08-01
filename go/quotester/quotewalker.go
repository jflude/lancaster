package main

// #include "../../status.h"
// #include "../../storage.h"
// #include "../../error.h"
// #include "../../datum.h"
import "C"
import (
	"bytes"
	"fmt"
	"github.com/willf/bitset"
	"log"
	"strings"
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
func findQuotes(keys []string) {
	// Used to grab the record itself
	var maxRecords = int(C.storage_get_max_id(store))
	var rBaseAddr = (unsafe.Pointer(C.storage_get_array(store)))
	var rSize = C.storage_get_record_size(store)
	var vOffset = uintptr(C.storage_get_value_offset(store))
	var rBaseId = int(C.storage_get_base_id(store))

	for x := 0; x < maxRecords; x++ {
		uraddr := (uintptr(rBaseAddr) + (uintptr(rSize) * uintptr((x - rBaseId))))
		raddr := (unsafe.Pointer)(uraddr)
		seq := *((*C.uint)(raddr))
		if seq < 1 {
			return
		}
		vaddr := (unsafe.Pointer)(uraddr + vOffset)
		d := (*Quote)(vaddr)
		recKey := d.key()
		for _, k := range keys {
			if strings.HasPrefix(recKey, k) {
				fmt.Println(d)
				break
			}
		}
	}

}
func tailQuotes(watchKeys []string) {
	var hasWatch = len(watchKeys) > 0
	var watchBits bitset.BitSet
	var qCapacity = C.storage_get_queue_capacity(store)
	var storeOwner = C.storage_get_owner_pid(store)
	var qSize = int(qCapacity)
	var qMask = qSize - 1
	var qHeadPtr = (*C.uint)((unsafe.Pointer(C.storage_get_queue_head_ref(store))))
	var qBasePtr = unsafe.Pointer(C.storage_get_queue_base_ref(store))
	var qArr = (*[1 << 30]C.identifier)(qBasePtr)
	var new_head = int(*(qHeadPtr))
	var old_head = new_head
	// Used to grab the record itself
	var maxRecords = int(C.storage_get_max_id(store))
	var rBaseAddr = (unsafe.Pointer(C.storage_get_array(store)))
	var rSize = C.storage_get_record_size(store)
	var vOffset = uintptr(C.storage_get_value_offset(store))
	var rBaseId = C.storage_get_base_id(store)
	// log.Println(C.storage_get_high_water_id(store))
	log.Println("Queue size:", qCapacity)
	if hasWatch {
		log.Println("Filtering for:", watchKeys)
	}
	keys := make([]*string, maxRecords)
	for {
		new_head = int(*(qHeadPtr))
		if new_head == old_head {
			time.Sleep(time.Microsecond)
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
			d := (*Quote)(vaddr)
			// Not checking for lock contention on this part as the ID is known to only be written once per slot
			// if we've been told about the record, it must have been written at least once.
			key := keys[id]
			useStr := !hasWatch
			if key == nil {
				s := d.key()
				key = &s
				keys[id] = key
				// fmt.Fprintln(os.Stderr, "newKey :", id, s)
				if hasWatch {
					for _, k := range watchKeys {
						if strings.HasPrefix(s, k) {
							watchBits.Set(uint(id))
							useStr = true
							// fmt.Fprintln(os.Stderr, "Found:", id, s)
							break
						}
					}
				}
			} else if hasWatch && watchBits.Test(uint(id)) {
				useStr = true
			}

			str := ""
			for {
				seq := *((*C.uint)(raddr))
				if seq < 0 {
					fmt.Println("locked")
					continue
				}
				if useStr {
					str = d.String()
				}
				nseq := *((*C.uint)(raddr))
				if seq == nseq {
					break
				}
				fmt.Println("Version stomp", seq, "!=", nseq)
			}
			if useStr {
				fmt.Println(j, j&qMask, str)
			}
		}
		old_head = new_head
	}
}
