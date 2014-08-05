package com.peak6.cachester.direct.update;

import sun.misc.Unsafe;

import com.peak6.cachester.direct.CachesterStoreMetadata;
import com.peak6.cachester.direct.RecordDecoder;
import com.peak6.util.UnsafeUtil;

public class RecordAccessor {

    private static final Unsafe UNSAFE = UnsafeUtil.UNSAFE;

    private long recordId;
    private long recordAddr;
    private final CachesterStoreMetadata storeMetadata;

    public RecordAccessor(long recordId, CachesterStoreMetadata storeMetadata) {
        this.storeMetadata = storeMetadata;
        setRecordId(recordId);
    }

    public long getRecordId() {
        return this.recordId;
    }

    public void setRecordId(long recordId) {
        this.recordId = recordId;
        this.recordAddr = calculateRecordAddr(recordId);
    }

    public <T> T decodeConsistently(RecordDecoder<T> decoder) {
        long lastSequenceNumber;
        T decodedValue = null;
        long sequenceNumber = UNSAFE.getLong(recordAddr);
        do {
            lastSequenceNumber = sequenceNumber;
            decodedValue = decoder.decode(this);
        } while((sequenceNumber = UNSAFE.getLong(recordAddr)) != lastSequenceNumber);
        return decodedValue;
    }

    public byte[] readConsistently() {
        long recordAddr = calculateRecordAddr(recordId);
        long recordValueAddr = recordAddr + storeMetadata.getValueOffset();
        long lastSequenceNumber;
        long sequenceNumber = UNSAFE.getLong(recordAddr);
        byte[] recordValue;
        do {
            lastSequenceNumber = sequenceNumber;
            recordValue = readValueAt(recordValueAddr);
        } while((sequenceNumber = UNSAFE.getLong(recordAddr)) != lastSequenceNumber);
        return recordValue;
    }

    private byte[] readValueAt(long recordValueAddr) {
        byte[] recordValue = new byte[storeMetadata.getValueSize()];
        for(int i = 0; i < storeMetadata.getValueSize(); i++) {
            recordValue[i] = UNSAFE.getByte(i+recordValueAddr);
        }
        return recordValue;
    }

    public byte getByte(int offset) {
        return UNSAFE.getByte(calculateValueOffsetAddr(offset));
    }

    public char getChar(int offset) {
        return UNSAFE.getChar(calculateValueOffsetAddr(offset));
    }

    public short getShort(int offset) {
        return UNSAFE.getShort(calculateValueOffsetAddr(offset));
    }

    public int getInt(int offset) {
        return UNSAFE.getInt(calculateValueOffsetAddr(offset));
    }

    public long getLong(int offset) {
        return UNSAFE.getLong(calculateValueOffsetAddr(offset));
    }

    public float getFloat(int offset) {
        return UNSAFE.getFloat(calculateValueOffsetAddr(offset));
    }

    public double getDouble(int offset) {
        return UNSAFE.getDouble(calculateValueOffsetAddr(offset));
    }

    private long calculateValueOffsetAddr(int offset) {
        return recordAddr + storeMetadata.getValueOffset() + offset;
    }

    private long calculateRecordAddr(long recordIndex) {
        return storeMetadata.getRecordArrayAddr() + storeMetadata.getRecordSize() * recordIndex;
    }

}
