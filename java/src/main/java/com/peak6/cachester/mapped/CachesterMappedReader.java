package com.peak6.cachester.mapped;

import java.io.IOException;

import com.peak6.cachester.CachesterStorageLoader;
import com.peak6.cachester.QuoteRecord;

public class CachesterMappedReader {

    static final char GAP_CHARACTER = '!';
    static final char CHANGES_DROPPED_CHARACTER = '*';
    static final char CHANGES_READ_CHARACTER = '.';

    private final int queueCapacity;
    private final boolean verbose;

    private int oldN = 0;

    private final CachesterMapppedFile cachesterFile;

    private CachesterMappedReader(String path, boolean verbose) throws IOException {
        this.cachesterFile = CachesterMapppedFile.create(path, verbose);
        this.verbose = verbose;
        this.queueCapacity = this.cachesterFile.getQueueCapacity();
    }

    public static CachesterMappedReader create(String path) throws IOException {
        return new CachesterMappedReader(path, false);
    }

    public static CachesterMappedReader createVerbose(String path) throws IOException {
        return new CachesterMappedReader(path, true);
    }

    public void readStoreUpdates() throws InterruptedException {
        long oldHead = 0;
        long newHead = 0;
        int numChangeReads = 0;
        int numChangesDropped = 0;
        int numGapsDetected = 0;
        while (true) {
            newHead = cachesterFile.getQueueHead();

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

    private boolean shouldPrintStatusInterval(int numChangeReads) {
        return (numChangeReads & 1023) == 0;
    }

    private int readChangeQueueRange(long oldHead, long newHead) {
        int numGapsDetected = 0;
        for (long i = oldHead; i < newHead; ++i) {
            int id = readChangeQueueIdAt(i);
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

    private int readChangeQueueIdAt(long changeQueueAddr) {
        verbosePrint("Reading at %d%n", changeQueueAddr);
        int id = cachesterFile.getChangeQueueId(changeQueueAddr);
        verbosePrint("Read %d from %d%n", id, changeQueueAddr);
        return id;
    }

    private QuoteRecord readRecord(int index) {
        verbosePrint("Looking up record at %d%n", index);
        QuoteRecord quoteRecord = new QuoteRecord();
        quoteRecord.bidQuantity = index * 2;
        quoteRecord.askQuantity = index * 2 + 1;
        return quoteRecord;
//        exitOnError(CachesterStorage.INSTANCE.storage_get_record(store, index, recordReference));
//
//        QuoteRecord quoteRecord = new QuoteRecord();
//
//        long recordSequence = CachesterStorage.INSTANCE.record_read_lock(record);
//        while(recordSequence < 0) {
//            recordSequence = CachesterStorage.INSTANCE.record_read_lock(record);
//        }
//        do {
//            //          quoteRecord.bid = swapEndiannessDouble(record.getDouble(8));
//            //          quoteRecord.ask = swapEndiannessDouble(record.getDouble(16));
//            quoteRecord.bidQuantity = record.getInt(32);
//            quoteRecord.askQuantity = record.getInt(36);
//        } while(CachesterStorage.INSTANCE.record_read_lock(record) != recordSequence);
//        return quoteRecord;
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
