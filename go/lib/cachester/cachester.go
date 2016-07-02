package cachester

// #cgo pkg-config: cachester
// #include "status.h"
// #include "storage.h"
// #include "error.h"
// #include "datum.h"
// #include "batch.h"
// #include <string.h>
// #include <stdlib.h>
// #include <fcntl.h>
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

// WritableStore is what it sounds like
type WritableStore struct {
	Store Store
}

// CreateFile constructs a writable store, overwriting the existing file
func CreateFile(file string, description string) (*WritableStore, error) {
	var ws WritableStore
	name := C.CString(file)
	defer C.free(unsafe.Pointer(name))
	desc := C.CString(description)
	defer C.free(unsafe.Pointer(desc))
	if err := call(C.storage_create(&ws.Store.store, name,
		syscall.O_CREAT|syscall.O_RDWR, 0644, C.FALSE,
		0, 1e6, // base id and max id
		256, 0, // size_t value_size, size_t property_size,
		1024, desc)); err != nil {
		return nil, err
	}
	return &ws, nil
}

// WriteRecord writes the given buffer at the given index
func (ws *WritableStore) WriteRecord(idx int64, data []byte) error {
	sz := C.size_t(len(data))
	cid := (*C.identifier)(&idx)
	d := unsafe.Pointer(&data[0])
	return call(C.batch_write_records(ws.Store.store, sz, cid, d, 1))
}

// GetRecord copies the data from the supplied record index to the supplied buffer
func (cs *Store) GetRecord(idx int64, buff []byte) (revision int64, err error) {
	var rev C.revision
	err = call(C.batch_read_records(cs.store, C.size_t(len(buff)),
		(*C.identifier)(&idx), unsafe.Pointer(&buff[0]), &rev,
		nil, 1))
	return int64(rev), err
}

// GetRecordSize returns the size of an individual cachester slot
func (cs *Store) GetRecordSize() int64 {
	return int64(C.storage_get_record_size(cs.store))
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

// ChangeWatcherFunc is a simple convenience wrapper
type ChangeWatcherFunc func(identifiers []int64, revs []int64, records [][]byte) error

// OnChange calls self
func (cwf ChangeWatcherFunc) OnChange(identifiers []int64, revs []int64, records [][]byte) error {
	return cwf(identifiers, revs, records)
}

// ChangeWatcher watches the ChangeQueue for updates
type ChangeWatcher interface {
	OnChange(identifiers []int64, revs []int64, records [][]byte) error
}

func (cs Store) String() string {
	return fmt.Sprintf("Store[%s]", cs.Name)
}

// Close closes the cachester store
func (cs *Store) Close() {
	C.storage_destroy(&cs.store)
}

// Watch loops over the ChangeQueue calling the supplied callback
func (cs *Store) Watch(recordSize int, cw ChangeWatcher) {
	const numRecs = 1024
	ids := make([]int64, numRecs)
	rawBuff := make([]byte, numRecs*recordSize)
	buffs := make([][]byte, numRecs)
	revs := make([]int64, numRecs)
	for i := 0; i < len(buffs); i++ {
		start := i * recordSize
		buffs[i] = rawBuff[start : start+recordSize]
	}
	var head C.q_index = -1
	for {
		status := C.batch_read_changed_records(cs.store, C.size_t(recordSize),
			(*C.identifier)(&ids[0]), unsafe.Pointer(&rawBuff[0]), (*C.revision)(&revs[0]),
			nil, numRecs,
			0, &head)
		if status < 0 {
			err := call(status)
			log.Fatal("Error reading change queue:", err)
		} else if status == 0 {
			time.Sleep(time.Millisecond)
		} else {
			num := int(status)
			cw.OnChange(ids[:num], revs, buffs[:num])
		}
	}
}

// GetRecordFromQSlot Retrieves contents of a record
func (cs *Store) GetRecordFromQSlot(qIdx int64, buff []byte) (recslot int64, rev int64, err error) {
	var recid C.identifier
	if err := call(C.storage_read_queue(cs.store, C.q_index(qIdx), &recid)); err != nil {
		return -1, -1, err
	}
	rev, err = cs.GetRecord(int64(recid), buff)
	return int64(recid), rev, err
	// record_get_value_ref(&record);
}

func call(status C.status) error {
	if status == 0 {
		return nil
	}

	str := C.GoString(C.error_last_msg())
	return fmt.Errorf("%d: %s", status, errors.New(str))
}
