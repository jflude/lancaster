package quotester

// #include "../../../status.h"
// #include "../../../storage.h"
// #include "../../../error.h"
// #include "../../../datum.h"
// #cgo linux LDFLAGS: ../../../libcachester.a -lrt -lm -w
// #cgo darwin LDFLAGS: ../../../libcachester.a -lm -w
import "C"
import (
	"errors"
	"fmt"
	"syscall"
	"time"
	"unsafe"
)

type CachesterSource struct {
	Name        string
	Description string
	store       C.storage_handle
	rBaseId     int
	tsOffset    uintptr
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
	SkipNulls   bool
}

type RecordHandler func(slot int, version uint, recordAddr unsafe.Pointer) error

type Record struct {
	ptr unsafe.Pointer
	cs  *CachesterSource
}

type Data unsafe.Pointer

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
	cs.tsOffset = 8
	return cs, nil
}
func (cs *CachesterSource) Destroy() {
	C.storage_destroy(&cs.store)
}
func (cs *CachesterSource) GetRecord(record int) *Record {
	r := &Record{cs: cs}
	r.Select(record)
	return r
}
func (cs *CachesterSource) GetRecordPtr(record int) unsafe.Pointer {
	return unsafe.Pointer((uintptr(cs.rBaseAddr) + (uintptr(cs.rSize) * uintptr((record - cs.rBaseId)))))
}

func (r *Record) Select(record int) {
	r.ptr = r.cs.GetRecordPtr(record)
}

func (r *Record) GetData() Data {
	return Data(uintptr(r.ptr) + r.cs.vOffset)
}
func (r *Record) GetTimeStamp() time.Time {
	tsPtr := unsafe.Pointer(uintptr(r.ptr) + r.cs.tsOffset)
	tsval := *((*C.long)(tsPtr))
	return usToTime(uint64(tsval))
}
func (r *Record) GetRevision() uint {
	return uint(*((*C.uint)(r.ptr)))
}

/*func (cs *CachesterSource) WalkRecords(handler RecordHandler) error {
	for record := 0; record < cs.maxRecords; record++ {
		rPtr := cs.GetRecord(record)
		ver := uint(*((*C.uint)(rPtr)))
		if ver == 0 && cs.SkipNulls {
			continue
		}
		err := handler(record, ver, rPtr)
		if err != nil {
			return err
		}
	}
	return nil
}

func (cs *CachesterSource) TailRecords(handler RecordHandler) error {
	var new_head = int(*(cs.qHeadPtr))
	var old_head = new_head
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
			id := int(cs.qArr[j&cs.qMask])
			if id == -1 {
				fmt.Println("-1")
				continue
			}
			rPtr := cs.GetRecord(int(id))
			ver := uint(*((*C.uint)(rPtr)))
			err := handler(id, ver, rPtr)
			if err != nil {
				return err
			}
		}
	}
	log.Fatal("Die")
	return nil
}
*/
func chkStatus(status C.status) error {
	if status == 0 {
		return nil
	}
	str := C.GoString(C.error_last_msg())
	return fmt.Errorf("%d: %s", status, errors.New(str))
}

func usToTime(us uint64) time.Time {
	sec := us / 1000000
	nanos := (us - (sec * 1000000)) * 1000
	return time.Unix(int64(sec), int64(nanos))
}
