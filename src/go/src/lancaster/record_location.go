/*
   Copyright (c)2014-2017 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the LICENSE file.
*/

package lancaster

import "fmt"

type RecordLocation struct {
	store *Store
	id    int64
}

func (rl RecordLocation) GetRecord(buff []byte) (revision int64, err error) {
	return rl.store.GetRecord(rl.id, buff)
}

func (rl RecordLocation) String() string {
	return fmt.Sprintf("[%v %d]", rl.store, rl.id)
}