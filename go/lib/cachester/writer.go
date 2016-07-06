package cachester

// #include "storage.h"
// #include "batch.h"
// #include "stdlib.h"
import "C"

import (
	"syscall"
	"unsafe"
)

// WritableStore is what it sounds like
type WritableStore struct {
	Store Store
}

// CreateFile constructs a writable store, overwriting the existing file
func CreateFile(file string, description string, recSz int64, maxRecs int64, changeQSz int64) (*WritableStore, error) {
	var ws WritableStore
	name := C.CString(file)
	defer C.free(unsafe.Pointer(name))
	desc := C.CString(description)
	defer C.free(unsafe.Pointer(desc))
	if err := call(C.storage_create(&ws.Store.store, name,
		syscall.O_CREAT|syscall.O_RDWR, 0644, C.FALSE,
		0, C.identifier(maxRecs), // base id and max id
		C.size_t(recSz), 0, // size_t value_size, size_t property_size,
		C.size_t(changeQSz), desc)); err != nil {
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

type BulkWriter struct {
	ids   []int64
	raw   []byte
	recs  [][]byte
	cur   int64
	recSz C.size_t
	store *WritableStore
}

func (ws *WritableStore) NewBulkWriter(numRecs int64) *BulkWriter {
	recSz := ws.Store.GetRecordSize()
	raw, recs := createRecBuff(recSz, numRecs)
	return &BulkWriter{
		store: ws,
		raw:   raw,
		recs:  recs,
		recSz: C.size_t(recSz),
		ids:   make([]int64, numRecs),
	}
}
func (bw *BulkWriter) HasRemaining() bool {
	return bw.cur < int64(len(bw.ids))
}
func (bw *BulkWriter) Next(slot int64) []byte {
	ret := bw.recs[bw.cur]
	bw.ids[bw.cur] = slot
	bw.cur++
	return ret
}
func (bw *BulkWriter) Write() (int64, error) {
	num := bw.cur
	if num == 0 {
		return 0, nil
	}
	err := call(
		C.batch_write_records(
			bw.store.Store.store,
			bw.recSz,
			(*C.identifier)(&bw.ids[0]),
			unsafe.Pointer(&bw.raw[0]),
			C.size_t(bw.cur)))
	bw.cur = 0
	return num, err
}
