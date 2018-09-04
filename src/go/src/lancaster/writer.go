/*
   Copyright (c)2014-2018 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the COPYING file.
*/

package lancaster

/*
#include <lancaster/batch.h>
#include <lancaster/storage.h>
#include <stdlib.h>
*/
import "C"

import (
	"errors"
	"os"
	"syscall"
	"time"
	"unsafe"
)

type WriterConfig struct {
	Name        string
	Description string
	RecordSize  int64
	MaxRecords  int64
	ChangeQSize int64
}

// Create constructs a WritableStore from the supplied config
func (wc WriterConfig) Create() (ws *WritableStore, err error) {
	if wc.Description == "" {
		wc.Description = wc.Name
	}

	if wc.Name == "" {
		err = errors.New("Must set Name")
	} else if wc.RecordSize == 0 {
		err = errors.New("Must set RecordSize")
	} else if wc.ChangeQSize == 0 {
		err = errors.New("Must set ChangeQSize")
	} else if wc.MaxRecords < 1 {
		err = errors.New("Must set MaxRecords")
	} else {
		ws, err = CreateFile(wc.Name, wc.Description, wc.RecordSize, wc.MaxRecords, wc.ChangeQSize)
	}
	return
}

// WritableStore is what it sounds like
type WritableStore struct {
	Store
}

// CreateFile constructs a writable store, overwriting the existing file
func CreateFile(file string, description string, recSz int64, maxRecs int64, changeQSz int64) (*WritableStore, error) {
	var ws WritableStore
	name := C.CString(file)
	defer C.free(unsafe.Pointer(name))
	desc := C.CString(description)
	defer C.free(unsafe.Pointer(desc))
	if fi, err := os.Stat(file); err == nil {
		if fi.IsDir() {
			return nil, errors.New("file exists and is a directory")
		}
		if err := os.Remove(file); err != nil {
			return nil, err
		}
	}
	if err := call(C.storage_create(&ws.store, name,
		syscall.O_CREAT|syscall.O_RDWR, 0644, C.FALSE,
		0, C.identifier(maxRecs), // base id and max id
		C.size_t(recSz), 0, // size_t value_size, size_t property_size,
		C.size_t(changeQSz), desc)); err != nil {
		return nil, err
	}
	if err := ws.Touch(); err != nil {
		return nil, err
	}
	ws.Name = C.GoString(C.storage_get_description(ws.store))
	ws.File = file
	go func() {
		for range time.Tick(time.Second) {
			ws.Touch()
		}
	}()
	return &ws, nil
}

func (ws *WritableStore) Touch() error {
	return call(C.storage_touch(ws.store, C.microsec(time.Now().UnixNano()/1000)))
}

// WriteRecord writes the given buffer at the given index
func (ws *WritableStore) WriteRecord(idx int64, data []byte) error {
	sz := C.size_t(len(data))
	cid := (*C.identifier)(&idx)
	d := unsafe.Pointer(&data[0])
	return call(C.batch_write_records(ws.store, sz, cid, d, 1))
}

type BulkWriter struct {
	*WritableStore
	ids   []int64
	raw   []byte
	recs  [][]byte
	cur   int64
	recSz C.size_t
}

func (ws *WritableStore) NewBulkWriter(numRecs int64) *BulkWriter {
	recSz := ws.GetRecordSize()
	raw, recs := createRecBuffer(recSz, numRecs)
	return &BulkWriter{
		WritableStore: ws,
		raw:           raw,
		recs:          recs,
		recSz:         C.size_t(recSz),
		ids:           make([]int64, numRecs),
	}
}

// IsEmpty returns true if the writer has no pending data
func (bw *BulkWriter) IsEmpty() bool {
	return bw.cur == 0
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
			bw.WritableStore.store,
			bw.recSz,
			(*C.identifier)(&bw.ids[0]),
			unsafe.Pointer(&bw.raw[0]),
			C.size_t(bw.cur)))

	bw.cur = 0
	return num, err
}
