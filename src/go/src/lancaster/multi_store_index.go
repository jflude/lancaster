/*
   Copyright (c)2014-2018 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the COPYING file.
*/

package lancaster

import (
	"fmt"
	"sync"
	"errors"
)

// Index tracks key->record location
type MultiStoreIndex struct {
	index   map[string]RecordLocation
	lock    sync.Mutex
	stores  []indexedStore
	keyer   Keyer
	keySize int
}

type indexedStore struct {
	*Store
	maxID int64
}

// NewIndexer creates an indexer for a given store with a given keyer
func NewMultiStoreIndexer(stores []*Store, k Keyer) *MultiStoreIndex {
	var indexedStores []indexedStore
	for _, s := range stores {
		indexedStores = append(indexedStores, indexedStore{Store: s, maxID: 0})
	}
	return &MultiStoreIndex{
		index:  make(map[string]RecordLocation),
		keyer:  k,
		stores: indexedStores,
	}
}

// GetIndex Returns an already computed index for a given key
func (i *MultiStoreIndex) GetLocation(k string) (RecordLocation, error) {
	i.lock.Lock()
	ret, ok := i.index[k]
	i.lock.Unlock()
	if ok {
		return ret, nil
	}
	return RecordLocation{}, ErrNotFound
}

// Update looks for new records, it assumes that already indexed records haven't changed their keys
func (i *MultiStoreIndex) Update() error {
	for _, s := range i.stores {
		if err := i.updateStore(s); err != nil {
			return err
		}
	}
	return nil
}

func (i *MultiStoreIndex) updateStore(s indexedStore) error {
	var currID = s.maxID
	var buff = make([]byte, i.keyer.KeySize())
	for {
		rev, err := s.GetRecord(currID, buff)
		if err != nil {
			return fmt.Errorf("Failed to read record: %v, reason: %v", currID, err)
		} else if rev == 0 {
			break
		} else {
			k := i.keyer.GetKey(buff)
			ks := string(k)
			i.lock.Lock()
			rl := RecordLocation{store: s.Store, id: currID}
			if orig, ok := i.index[ks]; ok {
				return errors.New(fmt.Sprintf("index: duplicate key %v. original: %v new: %v", ks, orig, rl))
			} else {
				i.index[ks] = rl
				currID++
			}
			i.lock.Unlock()
		}
	}
	s.maxID = currID
	return nil
}
