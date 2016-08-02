package cachester

type RecordLocation struct {
	store *Store
	id    int64
}

func (rl RecordLocation) GetRecord(buff []byte) (revision int64, err error) {
	return rl.store.GetRecord(rl.id, buff)
}