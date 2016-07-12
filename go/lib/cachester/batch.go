package cachester

// #include "batch.h"
import "C"
import "unsafe"

type BatchReader struct {
	revs    []int64
	recs    [][]byte
	rawBuff []byte
	revPtr  *C.revision
	rawPtr  unsafe.Pointer
	recSz   C.size_t
	store   *Store
}

// NewBatchReader creates a batch reader
func (cs *Store) NewBatchReader(recordSize int64) *BatchReader {
	return &BatchReader{
		recSz: C.size_t(recordSize),
		store: cs,
	}
}
func (br *BatchReader) ensureSize(numRecs int64) {
	if int64(len(br.revs)) < numRecs {
		br.revs = make([]int64, numRecs)
		br.rawBuff, br.recs = createRecBuff(int64(br.recSz), numRecs)
		br.revPtr = (*C.revision)(&br.revs[0])
		br.rawPtr = unsafe.Pointer(&br.rawBuff[0])
	}
}

// Load copies data from the cachester storage to this buffer
func (br *BatchReader) Read(ids []int64) (recs [][]byte, revs []int64, err error) {
	numRecs := int64(len(ids))
	br.ensureSize(numRecs)
	idptr := (*C.identifier)(&ids[0])
	status := C.batch_read_records(
		br.store.store,    // store
		br.recSz,          // copy_size
		idptr,             // ids
		br.rawPtr,         // values
		br.revPtr,         // revs
		nil,               // times
		C.size_t(numRecs)) // count
	if status < 0 {
		err = call(status)
	} else {
		revs = br.revs[:numRecs]
		recs = br.recs[:numRecs]
	}
	return
}

func (cs *Store) GetRecords(recordSize int, ids []int64) ([][]byte, error) {
	numRecs := len(ids)
	rawBuff := make([]byte, numRecs*recordSize)
	buffs := make([][]byte, numRecs)
	for i := 0; i < len(buffs); i++ {
		start := i * recordSize
		buffs[i] = rawBuff[start : start+recordSize]
	}
	status := C.batch_read_records(cs.store, C.size_t(recordSize),
		(*C.identifier)(&ids[0]), unsafe.Pointer(&rawBuff[0]), nil,
		nil, C.size_t(numRecs))

	if status < 0 {
		return nil, call(status)
	} else {
		return buffs, nil
	}
}

// ChangeReader encapsulates the buffers and state for calling batch_read_changed_records
type ChangeReader struct {
	ids     []int64
	revs    []int64
	recs    [][]byte
	rawBuff []byte
	last    C.q_index
	idPtr   *C.identifier
	revPtr  *C.revision
	rawPtr  unsafe.Pointer
	recSz   C.size_t
	numRecs C.size_t
	store   *Store
}

func (cs *Store) NewChangeReader(recordSize int64, numRecs int64) *ChangeReader {
	ids := make([]int64, numRecs)
	revs := make([]int64, numRecs)
	rawBuff, recs := createRecBuff(recordSize, numRecs)
	var rawPtr unsafe.Pointer
	if recordSize == 0 {
		rawPtr = nil
	} else {
		rawPtr = unsafe.Pointer(&revs[0])
	}

	return &ChangeReader{
		ids:     ids,
		revs:    revs,
		recs:    recs,
		rawBuff: rawBuff,
		last:    -1,
		idPtr:   (*C.identifier)(&ids[0]),
		revPtr:  (*C.revision)(&revs[0]),
		rawPtr:  rawPtr,
		recSz:   C.size_t(recordSize),
		numRecs: C.size_t(numRecs),
		store:   cs,
	}
}

func (cr *ChangeReader) Next() (ids []int64, recs [][]byte, revs []int64, err error) {
	status := C.batch_read_changed_records(
		cr.store.store, // store
		cr.recSz,       // copy_size
		cr.idPtr,       // ids
		cr.rawPtr,      // values
		cr.revPtr,      // revs
		nil,            // times
		cr.numRecs,     // count
		0,              // timeout
		&cr.last)       // head
	if status < 0 {
		err = call(status)
	} else if status > 0 {
		num := int64(status)
		ids = cr.ids[:num]
		revs = cr.revs[:num]
		if cr.rawPtr != nil {
			recs = cr.recs[:num]
		}
	}
	return
}

func createRecBuff(recSz int64, numRecs int64) (rawBuff []byte, recs [][]byte) {
	if recSz == 0 {
		return nil, nil
	}
	rawBuff = make([]byte, numRecs*recSz)
	recs = make([][]byte, numRecs)
	for i := int64(0); i < numRecs; i++ {
		start := i * recSz
		recs[i] = rawBuff[start : start+recSz]
	}
	return
}
