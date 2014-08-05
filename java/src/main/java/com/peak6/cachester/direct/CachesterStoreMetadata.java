package com.peak6.cachester.direct;

import com.peak6.cachester.CachesterStorage;
import com.peak6.cachester.CachesterStorageLoader;
import com.sun.jna.Pointer;

public class CachesterStoreMetadata {

    private final int queueCapacity;
    private final int queueMask;
    private final int recordSize;
    private final int valueSize;
    private final int valueOffset;
    private final long baseId;
    private final long maxId;
    private final long changeQueueHeadAddr;
    private final long changeQueueAddr;
    private final long recordArrayAddr;

    public CachesterStoreMetadata(Pointer store) {
        CachesterStorage storage = CachesterStorageLoader.getInstance();
        this.queueCapacity = storage.storage_get_queue_capacity(store);
        this.queueMask = queueCapacity - 1;
        this.recordSize = storage.storage_get_record_size(store);
        this.valueSize = storage.storage_get_value_size(store);
        this.valueOffset = storage.storage_get_value_offset(store);
        this.baseId = storage.storage_get_base_id(store);
        this.maxId = storage.storage_get_max_id(store);
        this.changeQueueHeadAddr = storage.storage_get_queue_head_ref(store);
        this.changeQueueAddr = storage.storage_get_queue_base_ref(store);
        this.recordArrayAddr = storage.storage_get_array(store);
    }

    public final int getQueueCapacity() {
        return queueCapacity;
    }

    public final int getQueueMask() {
        return queueMask;
    }

    public final int getRecordSize() {
        return recordSize;
    }

    public final int getValueSize() {
        return valueSize;
    }

    public final int getValueOffset() {
        return valueOffset;
    }

    public final long getBaseId() {
        return baseId;
    }

    public final long getMaxId() {
        return maxId;
    }

    public final long getChangeQueueHeadAddr() {
        return changeQueueHeadAddr;
    }

    public final long getChangeQueueAddr() {
        return changeQueueAddr;
    }

    public final long getRecordArrayAddr() {
        return recordArrayAddr;
    }

    @Override
    public String toString() {
        return "RecordStoreMetadata [queueCapacity=" + queueCapacity + ", queueMask=" + queueMask + ", recordSize="
                + recordSize + ", valueSize=" + valueSize + ", valueOffset=" + valueOffset + ", baseId=" + baseId
                + ", maxId=" + maxId + ", changeQueueHeadAddr=" + changeQueueHeadAddr + ", changeQueueAddr="
                + changeQueueAddr + ", recordArrayAddr=" + recordArrayAddr + "]";
    }

}