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
	usePrints   bool
}

func NewCachesterSource(name string) (*CachesterSource, error) {
	cs := &CachesterSource{Name: name}

	if err := chkStatus(C.storage_open(&cs.store, C.CString(name), syscall.O_RDONLY)); err != nil {
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
	if strings.Index(cs.Description, "PRINTS") != -1 {
		cs.usePrints = true
	}
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
		if verbose {
			log.Println("Version stomp:", nseq, "!=", seq, "for record:", record)
		}
	}
}

func (cs *CachesterSource) getKeys(start int) (map[string]int, int) {
	ret := make(map[string]int)
	x := start
	for ; x < cs.maxRecords; x++ {
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
	var x int = 0

	for ; x < cs.maxRecords; x++ {
		raddr := cs.getPointer(x)
		seq := *((*C.uint)(raddr))
		if seq < 1 {
			return
		}
		vaddr := (unsafe.Pointer)(uintptr(raddr) + cs.vOffset)
		recKey := cs.getkey(vaddr)
		for _, k := range keys {
			if strings.HasPrefix(recKey[2:], k) {
				fmt.Println(cs.getstring(vaddr))
				break
			}
		}
	}
}

func (cs *CachesterSource) getstring(vaddr unsafe.Pointer) string {
	if cs.usePrints {
		return ((*Print)(vaddr)).String()
	} else {
		return ((*Quote)(vaddr)).String()
	}
}
func (cs *CachesterSource) getkey(vaddr unsafe.Pointer) string {
	if cs.usePrints {
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
	if verbose {
		log.Println("Queue size:", cs.qCapacity)
	}
	if hasWatch && verbose {
		log.Println("Filtering for:", watchKeys)
	}
	keys := make([]*string, cs.maxRecords)
	for {
		new_head = int(*(cs.qHeadPtr))
		if new_head == old_head {
			time.Sleep(time.Millisecond)
			continue
		}
		if (new_head - old_head) > cs.qSize {
			log.Println("Overrun, qSize:", cs.qSize, " < ", (new_head - old_head))
			old_head = new_head - cs.qSize

		}
		for j := old_head; j < new_head; j++ {
			id := cs.qArr[j&cs.qMask]
			if id == -1 {
				fmt.Println("-1")
				continue
			}
			raddr := cs.getPointer(int(id))
			vaddr := (unsafe.Pointer)(uintptr(raddr) + cs.vOffset)
			key := keys[id]
			useStr := !hasWatch
			if key == nil {
				s := cs.getkey(vaddr)
				key = &s
				keys[id] = key
				if hasWatch {
					for _, k := range watchKeys {
						if strings.HasPrefix(s[2:], k) {
							watchBits.Set(uint(id))
							useStr = true
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
					if verbose {
						fmt.Println("locked")
					}
					continue
				}
				if useStr {
					str = cs.getstring(vaddr)
				}
				nseq := *((*C.uint)(raddr))
				if seq == nseq {
					break
				}
				if verbose {
					fmt.Println("Version stomp, index", id, "key", cs.getkey(vaddr), seq, "!=", nseq, raddr)
				}
			}
			if useStr {
				fmt.Println(str)
			}
		}
		old_head = new_head
	}
}
