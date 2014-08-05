package com.peak6.cachester;

import com.sun.jna.Library;
import com.sun.jna.Pointer;
import com.sun.jna.ptr.PointerByReference;

public interface CachesterStorage extends Library {

    int storage_create(PointerByReference storeReferece, String path, int queue, int dunno, int records, int recordSz);

    int storage_open(PointerByReference storeReference, String path);

    int storage_reset(Pointer p);

    void storage_destroy(Pointer p);

    int storage_read_queue(Pointer store, long index);

    int storage_get_queue_capacity(Pointer store);

    long storage_get_queue_head(Pointer store);

    long storage_get_queue_head_ref(Pointer store);

    int storage_get_record(Pointer store, long id, PointerByReference recordReference);

    long record_read_lock(Pointer record);

    long storage_get_array(Pointer store);

    long storage_get_queue_base_ref(Pointer store);

    String error_last_desc();

    int storage_get_record_size(Pointer store);
    int storage_get_value_size(Pointer store);
    int storage_get_value_offset(Pointer store);

    long storage_get_base_id(Pointer store);
    long storage_get_max_id(Pointer store);
}