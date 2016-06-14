package cachester

// #cgo pkg-config: cachester
// #include "status.h"
// #include "storage.h"
// #include "error.h"
// #include "datum.h"
// #include <string.h>
// #include <stdlib.h>
import "C"
import (
	"errors"
	"fmt"
	"log"
	"syscall"
	"time"
	"unsafe"
)

//Store represents a single cachester file
type Store struct {
	Name  string
	File  string
	store C.storage_handle
}

// ChangeWatcherFunc is a simple convenience wrapper
type ChangeWatcherFunc func(from, to int64) error

// OnChange calls self
func (cwf ChangeWatcherFunc) OnChange(from, to int64) error {
	return cwf(from, to)
}

// ChangeWatcher watches the ChangeQueue for updates
type ChangeWatcher interface {
	OnChange(from, to int64) error
}

func (cs Store) String() string {
	return fmt.Sprintf("Store[%s]", cs.Name)
}

// Close closes the cachester store
func (cs *Store) Close() {
	C.storage_destroy(&cs.store)
}

// Watch loops over the ChangeQueue calling the supplied callback
func (cs *Store) Watch(cw ChangeWatcher) {
	qCapacity := C.q_index(C.storage_get_queue_capacity(cs.store))
	newHead := C.storage_get_queue_head(cs.store)
	oldHead := newHead
	for {
		newHead = C.storage_get_queue_head(cs.store)
		if newHead == oldHead {
			time.Sleep(time.Millisecond)
			continue
		}
		if (newHead - oldHead) > qCapacity {
			log.Println("Overrun, qSize:", qCapacity, " < ", (newHead - oldHead))
			oldHead = newHead - qCapacity
		}
		if err := cw.OnChange(int64(oldHead), int64(newHead)); err != nil {
			log.Println("Error in change watcher for:", oldHead, "->", newHead, "error:", err)
		}
		oldHead = newHead
	}
}

// GetRecord copies the data from the supplied record index to the supplied buffer
func (cs *Store) GetRecord(idx int64, buff []byte) (revision int, err error) {
	recid := C.identifier(idx)
	var rec C.record_handle
	if err := call(C.storage_get_record(cs.store, recid, &rec)); err != nil {
		return 0, err
	}

	var rev C.revision
	for {
		if err := call(C.record_read_lock(rec, &rev)); err != nil || rev == 0 {
			return 0, err
		}
		recBuf := (*[1 << 30]byte)(C.record_get_value_ref(rec))
		copy(buff, recBuf[:])
		if C.record_get_revision(rec) == rev {
			return int(rev), nil
		}
		log.Println("record stomp:", recid)
	}
}

// GetRecordFromQSlot Retrieves contents of a record
func (cs *Store) GetRecordFromQSlot(qIdx int64, buff []byte) (recslot int, rev int, err error) {
	var recid C.identifier
	if err := call(C.storage_read_queue(cs.store, C.q_index(qIdx), &recid)); err != nil {
		return -1, -1, err
	}
	rev, err = cs.GetRecord(int64(recid), buff)
	return int(recid), rev, err
	// record_get_value_ref(&record);
}

// OpenFile opens a cachester file
func OpenFile(file string) (*Store, error) {
	var cs Store
	name := C.CString(file)
	defer C.free(unsafe.Pointer(name))

	if err := call(C.storage_open(&cs.store, name, syscall.O_RDONLY)); err != nil {
		return nil, err
	}
	cs.Name = C.GoString(C.storage_get_description(cs.store))
	cs.File = file
	return &cs, nil
}

func call(status C.status) error {
	if status == 0 {
		return nil
	}

	str := C.GoString(C.error_last_msg())
	return fmt.Errorf("%d: %s", status, errors.New(str))
}
