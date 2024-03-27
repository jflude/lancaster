/*
   Copyright (c)2014-2017 Peak6 Investments, LP.
   Copyright (c)2018-2024 Justin Flude.
   Use of this source code is governed by the COPYING file.
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
