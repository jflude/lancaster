package cachester

// #include "toucher.h"
import "C"
import "log"

const touchPeriodUSec = 1000 * 1000

var toucher C.toucher_handle = createToucher()

func createToucher() C.toucher_handle {
	var toucher C.toucher_handle
	if err := call(C.toucher_create(&toucher, touchPeriodUSec)); err != nil {
		log.Fatalln("Could not create toucher:", err)
	}
	return toucher
}

func toucherAddStorage(ws *WritableStore) error {
	return call(C.toucher_add_storage(toucher, ws.Store.store))
}

func toucherRemoveStorage(ws *WritableStore) error {
	return call(C.toucher_remove_storage(toucher, ws.Store.store))
}
