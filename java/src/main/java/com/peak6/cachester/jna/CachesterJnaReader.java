package com.peak6.cachester.jna;

import com.peak6.cachester.CachesterStorageLoader;
import com.peak6.cachester.QuoteRecord;
import com.sun.jna.Pointer;
import com.sun.jna.ptr.PointerByReference;

public class CachesterJnaReader {

    static final char GAP_CHARACTER = '!';
    static final char CHANGES_DROPPED_CHARACTER = '*';
    static final char CHANGES_READ_CHARACTER = '.';

    private final String path;

    private final Pointer store;
    private final long queueCapacity;

    private final boolean verbose;

    private final PointerByReference recordReference = new PointerByReference();

    private int oldN = 0;

    private CachesterJnaReader(String path, boolean verbose) {
        this.path = path;
        this.verbose = verbose;
        this.store = initializeStore();
        this.queueCapacity = readQueueCapacity();
        verbosePrint("Using path '%s'%n", path);
        addShutdownHookToDestroyStore();
    }

    public static CachesterJnaReader create(String path) {
        return new CachesterJnaReader(path, false);
    }

    public static CachesterJnaReader createVerbose(String path) {
        return new CachesterJnaReader(path, true);
    }

    public void readStoreUpdates() throws InterruptedException {
        long oldHead = 0;
        int numChangeReads = 0;
        int numChangesDropped = 0;
        int numGapsDetected = 0;

        while (true) {
            long newHead = CachesterStorageLoader.getInstance().storage_get_queue_head(store);

            verbosePrint("Read new head %d%n", newHead);

            if (newHead == oldHead) {
                snooze();
                continue;
            }

            if ((newHead - oldHead) > queueCapacity) {
                verbosePrint("Detected changes dropped because %d - %d = %d, which is greater than queue capacity of %d%n", newHead, oldHead, (newHead - oldHead), queueCapacity);
                numChangesDropped += ((newHead - oldHead) - queueCapacity);
                oldHead = newHead - queueCapacity;
            }

            numGapsDetected += readChangeQueueRange(oldHead, newHead);

            oldHead = newHead;
            numChangeReads++;
            if (shouldPrintStatusInterval(numChangeReads)) {
                if(numChangesDropped > 0) {
                    print("%c%d", CHANGES_DROPPED_CHARACTER, numChangesDropped);
                } else if(numGapsDetected > 0) {
                    print("%c%d", GAP_CHARACTER, numGapsDetected);
                } else {
                    print("%c", CHANGES_READ_CHARACTER);
                }
                System.out.flush();
                numChangesDropped = 0;
                numGapsDetected = 0;
            }
        }
    }

    public QuoteRecord getRecordAtIndex(long index) {
        return readRecord(index);
    }

    private boolean shouldPrintStatusInterval(int numChangeReads) {
        return (numChangeReads & 1023) == 0;
    }

    private int readChangeQueueRange(long oldHead, long newHead) {
        int numGapsDetected = 0;
        for (long i = oldHead; i < newHead; ++i) {
            long id = readChangeQueueIdAt(i);
            if (id == -1) {
                continue;
            }

            QuoteRecord quoteRecord = readRecord(id);
            verbosePrint("%d: %s%n", id, quoteRecord);
            int newN = quoteRecord.bidQuantity;
            if (newN != (oldN + 2)) {
                verbosePrint("Detected gap because %d != (%d + 1)%n", newN, oldN);
                numGapsDetected += ((newN - oldN) - 1);
            }

            oldN = newN;
        }
        return numGapsDetected;
    }

    private long readChangeQueueIdAt(long changeQueueAddr) {
        verbosePrint("Reading at %d%n", changeQueueAddr);
        long id = CachesterStorageLoader.getInstance().storage_read_queue(store, changeQueueAddr);
        verbosePrint("Read %d from %d%n", id, changeQueueAddr);
        return id;
    }

    private Pointer initializeStore() {
        PointerByReference storeReference = new PointerByReference();
        verbosePrint("Opening store at '%s'%n", path);
        exitOnError(CachesterStorageLoader.getInstance().storage_open(storeReference, path));
        return storeReference.getValue();
    }

    private long readQueueCapacity() {
        verbosePrint("Reading queue capacity");
        long queueCapacity = CachesterStorageLoader.getInstance().storage_get_queue_capacity(store);
        verbosePrint("Queue capacity is %d%n", queueCapacity);
        return queueCapacity;
    }

    private QuoteRecord readRecord(long index) {
        verbosePrint("Looking up record at %d%n", index);
        exitOnError(CachesterStorageLoader.getInstance().storage_get_record(store, index, recordReference));
        Pointer record = recordReference.getValue();

        QuoteRecord quoteRecord = new QuoteRecord();

        long recordSequence = CachesterStorageLoader.getInstance().record_read_lock(record);
        while(recordSequence < 0) {
            recordSequence = CachesterStorageLoader.getInstance().record_read_lock(record);
        }
        do {
//          quoteRecord.bid = swapEndiannessDouble(record.getDouble(8));
//          quoteRecord.ask = swapEndiannessDouble(record.getDouble(16));
          quoteRecord.bidQuantity = record.getInt(32);
          quoteRecord.askQuantity = record.getInt(36);
        } while(CachesterStorageLoader.getInstance().record_read_lock(record) != recordSequence);
        return quoteRecord;
    }

    private void addShutdownHookToDestroyStore() {
        verbosePrint("Adding shutdown hook to destroy store at '%s'%n", path);
        Runtime.getRuntime().addShutdownHook(new Thread() {
            @Override
            public void run() {
                verbosePrint("Shutdown hook running! Destroying store at '%s'%n", path);
                CachesterStorageLoader.getInstance().storage_destroy(store);
            }
        });
    }

    private void verbosePrint(String format, Object arg) {
        if(verbose) {
            print(format, arg);
        }
    }

    private void verbosePrint(String format, Object arg, Object arg2) {
        if(verbose) {
            print(format, arg, arg2);
        }
    }

    private void verbosePrint(String format, Object... args) {
        if(verbose) {
            print(format, args);
        }
    }

    private void print(String format, Object arg) {
        System.out.printf(format, arg);
    }

    private void print(String format, Object arg, Object arg2) {
        System.out.printf(format, arg, arg2);
    }

    private void print(String format, Object... args) {
        System.out.printf(format, args);
    }

    void snooze() throws InterruptedException {
        Thread.sleep(0, 1000);
    }

    int exitOnError(int status) {
        if (status == 0) {
            return status;
        }
        System.err.printf("Error: %s %n", CachesterStorageLoader.getInstance().error_last_desc());
        System.err.flush();
        System.exit(1);
        return status;
    }
}
