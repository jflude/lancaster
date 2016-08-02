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

func (cs *Store) GetTouchTime() (time.Time, error) {
	var when C.microsec
	if err := call(C.storage_get_touched_time(cs.store, &when)); err != nil {
		return time.Time{}, err
	}
	return time.Unix(0, int64(when)*int64(time.Microsecond)), nil
}

// WatchTouchTime checks if the store has been touched within 'threshold', every 'checkInterval.' If it has been
// touched, it will call badTouchTime.
func (cs *Store) WatchTouchTime(checkInterval time.Duration, badTouchTime func(c *Store, lastTouch time.Time)) {
	for {
		touchTime, err := cs.GetTouchTime()
		if err != nil {
			badTouchTime(cs, touchTime)
		}
		if touchTime.Add(time.Microsecond * touchPeriodUSec).Before(time.Now()) {
			badTouchTime(cs, touchTime)
		}
		time.Sleep(checkInterval)
	}
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

// GetBaseID returns the id of the 0 slot of this cachester file
func (cs *Store) GetBaseID() int64 {
	return int64(C.storage_get_base_id(cs.store))
}

// GetMaxID Returns max identifier in his store
func (cs *Store) GetMaxID() int64 {
	return int64(C.storage_get_max_id(cs.store))
}

func (cs *Store) GetChangeQSize() int64 {
	return int64(C.storage_get_queue_capacity(cs.store))
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
	const maxRecs = 1024
	cr := cs.NewChangeReader(int64(recordSize), maxRecs)
	for {
		ids, recs, revs, err := cr.Next()
		if err != nil {
			log.Fatalln("Error watching change queue:", err)
		}
		cw.OnChange(ids, revs, recs)
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
