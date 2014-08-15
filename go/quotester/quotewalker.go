package main

// #include "../../status.h"
// #include "../../storage.h"
// #include "../../error.h"
// #include "../../datum.h"
import "C"
import (
	"fmt"
	"github.com/willf/bitset"
	"log"
	"strings"
	"syscall"
	"time"
	"unsafe"
)

type CachesterSource struct {
	Name        string
	Description string
	store       C.storage_handle
	rBaseId     int
	vOffset     uintptr
	rSize       C.size_t
	rBaseAddr   unsafe.Pointer
	maxRecords  int
	qCapacity   C.size_t
	qSize       int
	qMask       int
	qHeadPtr    *C.uint
	qBasePtr    unsafe.Pointer
	qArr        *[1 << 30]C.identifier
}

func NewCachesterSource(name string) (*CachesterSource, error) {
	cs := &CachesterSource{Name: name}

	if err := chkStatus(C.storage_open(&cs.store, C.CString(name), syscall.O_RDONLY)); err != nil {
		log.Println("err", err)
		return nil, err
	}
	cs.maxRecords = int(C.storage_get_max_id(cs.store))
	cs.rBaseAddr = (unsafe.Pointer(C.storage_get_array(cs.store)))
	cs.rSize = C.storage_get_record_size(cs.store)
	cs.vOffset = uintptr(C.storage_get_value_offset(cs.store))
	cs.rBaseId = int(C.storage_get_base_id(cs.store))
	cs.qCapacity = C.storage_get_queue_capacity(cs.store)
	cs.qSize = int(cs.qCapacity)
	cs.qMask = cs.qSize - 1
	cs.qHeadPtr = (*C.uint)((unsafe.Pointer(C.storage_get_queue_head_ref(cs.store))))
	cs.qBasePtr = unsafe.Pointer(C.storage_get_queue_base_ref(cs.store))
	cs.qArr = (*[1 << 30]C.identifier)(cs.qBasePtr)
	cs.Description = C.GoString(C.storage_get_description(cs.store))
	return cs, nil
}
func (cs *CachesterSource) Destroy() {
	C.storage_destroy(&cs.store)
}
func (cs *CachesterSource) getPointer(record int) unsafe.Pointer {
	return unsafe.Pointer((uintptr(cs.rBaseAddr) + (uintptr(cs.rSize) * uintptr((record - cs.rBaseId)))))
}

// Load q with a clean copy of the record
func (cs *CachesterSource) getQuote(record int, q *Quote) error {
	// var rBaseAddr = (unsafe.Pointer(C.storage_get_array(store)))
	// var rSize = C.storage_get_record_size(store)
	// var vOffset = uintptr(C.storage_get_value_offset(store))
	// var rBaseId = int(C.storage_get_base_id(store))
	// uraddr := (uintptr(cs.rBaseAddr) + (uintptr(cs.rSize) * uintptr((record - cs.rBaseId))))
	// raddr := (unsafe.Pointer)(uraddr)
	raddr := cs.getPointer(record)
	for {
		seq := *((*C.uint)(raddr))
		if seq == 0 {
			return fmt.Errorf("No such record: %d", record)
		} else if seq < 0 {
			continue
		}
		vaddr := (unsafe.Pointer)(uintptr(raddr) + cs.vOffset)
		d := (*Quote)(vaddr)
		*q = *d
		nseq := *((*C.uint)(raddr))
		if seq == nseq {
			return nil
		}
		log.Println("Version stomp:", nseq, "!=", seq, "for record:", record)
	}
}

func (cs *CachesterSource) getKeys(start int) (map[string]int, int) {
	// var maxRecords = int(C.storage_get_max_id(store))
	// var rBaseAddr = (unsafe.Pointer(C.storage_get_array(store)))
	// var rSize = C.storage_get_record_size(store)
	// var vOffset = uintptr(C.storage_get_value_offset(store))
	// var rBaseId = int(C.storage_get_base_id(store))
	ret := make(map[string]int)
	x := start
	for ; x < cs.maxRecords; x++ {
		// uraddr := (uintptr(rBaseAddr) + (uintptr(rSize) * uintptr((x - rBaseId))))
		// raddr := (unsafe.Pointer)(uraddr)
		raddr := cs.getPointer(x)
		seq := *((*C.uint)(raddr))
		if seq < 1 {
			return ret, x
		}
		vaddr := (unsafe.Pointer)(uintptr(raddr) + cs.vOffset)
		d := (*Quote)(vaddr)
		ret[d.key()] = x
	}
	return ret, x
}

func (cs *CachesterSource) findQuotes(keys []string) {
	// Used to grab the record itself
	// var maxRecords = int(C.storage_get_max_id(store))
	// var rBaseAddr = (unsafe.Pointer(C.storage_get_array(store)))
	// var rSize = C.storage_get_record_size(store)
	// var vOffset = uintptr(C.storage_get_value_offset(store))
	// var rBaseId = int(C.storage_get_base_id(store))
	log.Println("Searching for", keys)
	var x int = 0
	for ; x < cs.maxRecords; x++ {
		raddr := cs.getPointer(x)
		// *((*C.uint)(raddr))
		// var q Quote
		// if err := cs.getQuote(record, &q); err != nil {
		// 	log.Println("getQuote:", x, err)
		// 	return
		// }
		// uraddr := (uintptr(rBaseAddr) + (uintptr(rSize) * uintptr((x - rBaseId))))
		// raddr := (unsafe.Pointer)(uraddr)
		seq := *((*C.uint)(raddr))
		if seq < 1 {
			return
		}
		vaddr := (unsafe.Pointer)(uintptr(raddr) + cs.vOffset)

		// d := (*Quote)(vaddr)
		// recKey := d.key()
		recKey := getkey(vaddr)
		for _, k := range keys {
			if strings.HasPrefix(recKey, k) {
				fmt.Println(getstring(vaddr))
				break
			}
		}
	}

}

func getstring(vaddr unsafe.Pointer) string {
	if usePrints {
		return ((*Print)(vaddr)).String()
	} else {
		return ((*Quote)(vaddr)).String()
	}
}
func getkey(vaddr unsafe.Pointer) string {
	if usePrints {
		return ((*Print)(vaddr)).key()
	} else {
		return ((*Quote)(vaddr)).key()
	}
}

func (cs *CachesterSource) tailQuotes(watchKeys []string) {
	var hasWatch = len(watchKeys) > 0
	var watchBits bitset.BitSet
	var new_head = int(*(cs.qHeadPtr))
	var old_head = new_head
	// Used to grab the record itself
	// var maxRecords = int(C.storage_get_max_id(store))
	// var rBaseAddr = (unsafe.Pointer(C.storage_get_array(store)))
	// var rSize = C.storage_get_record_size(store)
	// var vOffset = uintptr(C.storage_get_value_offset(store))
	// var rBaseId = C.storage_get_base_id(store)
	log.Println("Queue size:", cs.qCapacity)
	if hasWatch {
		log.Println("Filtering for:", watchKeys)
	}
	keys := make([]*string, cs.maxRecords)
	for {
		new_head = int(*(cs.qHeadPtr))
		if new_head == old_head {
			time.Sleep(time.Microsecond)
			continue
		}
		if (new_head - old_head) > cs.qSize {
			old_head = new_head - cs.qSize
		}
		for j := old_head; j < new_head; j++ {
			id := cs.qArr[j&cs.qMask]
			if id == -1 {
				fmt.Println("-1")
				continue
			}
			// uraddr := (uintptr(rBaseAddr) + (uintptr(rSize) * uintptr((id - rBaseId))))
			// raddr := (unsafe.Pointer)(uraddr)
			raddr := cs.getPointer(int(id))
			vaddr := (unsafe.Pointer)(uintptr(raddr) + cs.vOffset)
			// d := (*Quote)(vaddr)
			// Not checking for lock contention on this part as the ID is known to only be written once per slot
			// if we've been told about the record, it must have been written at least once.
			key := keys[id]
			useStr := !hasWatch
			if key == nil {
				s := getkey(vaddr) //d.key()
				key = &s
				keys[id] = key
				// fmt.Fprintln(os.Stderr, "newKey :", id, s)
				if hasWatch {
					for _, k := range watchKeys {
						if strings.HasPrefix(s, k) {
							// log.Println("Found key:", s, k)
							watchBits.Set(uint(id))
							useStr = true
							// fmt.Fprintln(os.Stderr, "Found:", id, s)
							break
						}
					}
				}
			} else if hasWatch {
				if watchBits.Test(uint(id)) {
					useStr = true
				} else {
					continue
				}
			}

			str := ""
			var seq C.uint
			for {
				seq = *((*C.uint)(raddr))
				if seq < 0 {
					fmt.Println("locked")
					continue
				}
				if useStr {
					str = getstring(vaddr) //d.String()
				}
				nseq := *((*C.uint)(raddr))
				if seq == nseq {
					break
				}
				fmt.Println("Version stomp, index", id, "key", getkey(vaddr), seq, "!=", nseq, raddr)
			}
			if useStr {
				fmt.Println(str)
				// fmt.Println(id, seq, raddr, j, j&qMask, str)
			}
		}
		old_head = new_head
	}
}
