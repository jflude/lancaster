package cachester

// #include "../../status.h"
// #include "../../storage.h"
// #include "../../error.h"
// #include "../../datum.h"
// #cgo LDFLAGS: ../../libcachester.a -lrt -w
import "C"
import (
	"fmt"
	"syscall"
	"unsafe"
)

type Store struct {
	Name           string
	Description    string
	store          C.storage_handle
	MaxRecords     int
	QCapacity      C.size_t
	QSize          int
	QMask          int
	QHeadPtr       *C.uint
	QBasePtr       unsafe.Pointer
	QArr           *[1 << 30]C.identifier
	RecordBaseId   int
	RecordSize     C.size_t
	RecordBaseAddr unsafe.Pointer
	ValueOffset    uintptr
}

func NewStore(name string) (*Store, error) {
	cs := &Store{Name: name}

	if err := chkStatus(C.storage_open(&cs.store, C.CString(name), syscall.O_RDONLY)); err != nil {
		return nil, err
	}
	cs.MaxRecords = int(C.storage_get_max_id(cs.store))
	cs.RecordBaseAddr = (unsafe.Pointer(C.storage_get_array(cs.store)))
	cs.RecordSize = C.storage_get_record_size(cs.store)
	cs.ValueOffset = uintptr(C.storage_get_value_offset(cs.store))
	cs.RecordBaseId = int(C.storage_get_base_id(cs.store))
	cs.QCapacity = C.storage_get_queue_capacity(cs.store)
	cs.QSize = int(cs.QCapacity)
	cs.QMask = cs.QSize - 1
	cs.QHeadPtr = (*C.uint)((unsafe.Pointer(C.storage_get_queue_head_ref(cs.store))))
	cs.QBasePtr = unsafe.Pointer(C.storage_get_queue_base_ref(cs.store))
	cs.QArr = (*[1 << 30]C.identifier)(cs.QBasePtr)
	cs.Description = C.GoString(C.storage_get_description(cs.store))
	return cs, nil
}

func (cs *Store) Destroy() {
	C.storage_destroy(&cs.store)
}

func chkStatus(status C.status) error {
	if status == 0 {
		return nil
	}
	str := C.GoString(C.error_last_msg())
	return fmt.Errorf("%d: %s", status, str)
}
