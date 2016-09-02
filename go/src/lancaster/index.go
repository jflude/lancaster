package lancaster

import (
	"errors"
	"fmt"
	"sync"
)

// ErrNotFound is returned whenever a key can not be resolved
var ErrNotFound = errors.New("not found")

// Keyer takes a buffer and returns the string key
type Keyer interface {
	KeySize() int
	GetKey(r []byte) []byte
}

// Index tracks key->record index
type Index struct {
	index     map[string]int64
	lock      sync.Mutex
	store     *Store
	keyer     Keyer
	keySize   int
	maxRecord int64
}

// NewIndexer creates an indexer for a given store with a given keyer
func NewIndexer(s *Store, k Keyer) *Index {
	return &Index{
		index: make(map[string]int64),
		keyer: k,
		store: s,
	}
}

// GetIndex Returns an alreadty computed index for a given key
func (i *Index) GetIndex(k string) (int64, error) {
	i.lock.Lock()
	ret, ok := i.index[k]
	i.lock.Unlock()
	if ok {
		return ret, nil
	}
	return 0, ErrNotFound
}

// GetKey computes the key for a given index
func (i *Index) GetKey(idx int64) ([]byte, error) {
	buff := make([]byte, i.keyer.KeySize())
	rev, err := i.store.GetRecord(idx, buff)
	if err != nil {
		return nil, err
	} else if rev == 0 {
		return nil, ErrNotFound
	} else {
		return i.keyer.GetKey(buff), nil
	}
}

// Update looks for new records, it assumes that already indexed records haven't changed their keys
func (i *Index) Update() error {
	var cur = i.maxRecord
	var buff = make([]byte, i.keyer.KeySize())
	for {
		rev, err := i.store.GetRecord(cur, buff)
		if err != nil {
			return fmt.Errorf("Failed to read record: %v, reason: %v", cur, err)
		} else if rev == 0 {
			break
		} else {
			k := i.keyer.GetKey(buff)
			ks := string(k)
			i.lock.Lock()
			if orig, ok := i.index[ks]; ok {
				return errors.New(fmt.Sprintf("index: duplicate key %v. original: %v new %v", ks, orig, cur))
			} else {
				i.index[ks] = cur
				cur++
			}
			i.lock.Unlock()
		}
	}
	// Not thread safe, but it's ok if we index more than once
	i.maxRecord = cur
	return nil
}
