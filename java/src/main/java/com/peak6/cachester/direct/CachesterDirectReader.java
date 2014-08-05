package com.peak6.cachester.direct;

import java.util.Arrays;
import java.util.Collection;

import sun.misc.Unsafe;

import com.peak6.cachester.CachesterStorageLoader;
import com.peak6.cachester.direct.update.NewRecordListener;
import com.peak6.cachester.direct.update.RecordAccessor;
import com.peak6.cachester.direct.update.RecordUpdateListener;
import com.peak6.util.UnsafeUtil;
import com.sun.jna.Pointer;
import com.sun.jna.ptr.PointerByReference;

public class CachesterDirectReader {
    private static final Unsafe UNSAFE = UnsafeUtil.UNSAFE;
    private static final int IDENTIFIER_SIZE = 8;
    private static final long CHECK_FOR_NEW_RECORDS_INTERVAL= 500L;

    static final char GAP_CHARACTER = '!';
    static final char CHANGES_DROPPED_CHARACTER = '*';
    static final char CHANGES_READ_CHARACTER = '.';

    private final String path;

    private final Pointer store;
    private final CachesterStoreMetadata storeMetadata;

    private NewRecordListener[] newRecordListeners = new NewRecordListener[0];
    private RecordUpdateListener[] updateListeners = new RecordUpdateListener[0];

    private final boolean verbose;
    private final boolean useThreadLocalRecordUpdates = false;

    private Thread newRecordCheckingThread;

    private final ThreadLocal<RecordAccessor> tlRecordAccessor;

    private CachesterDirectReader(String path, boolean verbose) {
        verbosePrint("Using path '%s'%n", path);
        this.path = path;
        this.verbose = verbose;
        this.store = initializeStore();
        this.storeMetadata = new CachesterStoreMetadata(store);
        this.tlRecordAccessor = makeThreadLocalRecordAccessor();
        verbosePrint("Initialized metadata: " + this.storeMetadata);
        addShutdownHookToDestroyStore();
    }

    private ThreadLocal<RecordAccessor> makeThreadLocalRecordAccessor() {
        return new ThreadLocal<RecordAccessor>() {
            @Override
            public RecordAccessor initialValue() {
                return new RecordAccessor(0, storeMetadata);
            }
        };
    }

    public synchronized CachesterDirectReader addUpdateListener(RecordUpdateListener updateListener) {
        RecordUpdateListener[] listeners = Arrays.copyOf(updateListeners, updateListeners.length + 1);
        listeners[updateListeners.length] = updateListener;
        this.updateListeners = listeners;
        return this;
    }

    public synchronized CachesterDirectReader addNewRecordListener(NewRecordListener newRecordListener) {
        NewRecordListener[] listeners = Arrays.copyOf(newRecordListeners, newRecordListeners.length + 1);
        listeners[newRecordListeners.length] = newRecordListener;
        this.newRecordListeners = listeners;
        return this;
    }

    public static CachesterDirectReader create(String path) {
        return new CachesterDirectReader(path, false);
    }

    public static CachesterDirectReader createVerbose(String path) {
        return new CachesterDirectReader(path, true);
    }

    public class NewRecordDiscoveryThread extends Thread {
        private long highestRecordIdSeen = -1;
        private final CachesterStoreMetadata storeMetadata;

        public NewRecordDiscoveryThread(CachesterStoreMetadata storeMetadata) {
            this.storeMetadata = storeMetadata;
        }

        @Override
        public void run() {
            while(true) {
                verbosePrint("Checking to see if next record with id %d has appeared yet", highestRecordIdSeen + 1);
                long numNewRecordsFound = 0;
                while((numNewRecordsFound = lookForNewRecords()) > 0) {
                    verbosePrint("%d new records found. highest now is %d.%n", numNewRecordsFound, highestRecordIdSeen);
                    sleepFor(1); // Sleep for 1 milli if we're reading faster than the file is being written to avoid sleeping for too long
                }
                sleepFor(CHECK_FOR_NEW_RECORDS_INTERVAL);
            }

        }

        private void sleepFor(long sleepForMillis) {
            try {
                Thread.sleep(sleepForMillis);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }

        private long lookForNewRecords() {
            long numNewRecordsFound = 0;
            long nextRecordAddr = getRecordAddress(highestRecordIdSeen + 1);
            while(UNSAFE.getLong(nextRecordAddr) != 0) {
                highestRecordIdSeen = highestRecordIdSeen + 1;
                notifyNewRecordListeners(highestRecordIdSeen);
                nextRecordAddr = getRecordAddress(highestRecordIdSeen + 1);
                numNewRecordsFound++;
            }
            return numNewRecordsFound;
        }

        private long getRecordAddress(long recordId) {
            return storeMetadata.getRecordArrayAddr() + ((recordId) * storeMetadata.getRecordSize());
        }
        private void notifyNewRecordListeners(long recordId) {
            for(NewRecordListener newRecordListener : newRecordListeners) {
                newRecordListener.added(getRecordAccessor(recordId));
            }
        }
    }

    public void readStoreUpdates() throws InterruptedException {
        newRecordCheckingThread = new NewRecordDiscoveryThread(storeMetadata);
        newRecordCheckingThread.setDaemon(true);
        newRecordCheckingThread.setName("CachesterNewRecordThread");
        newRecordCheckingThread.start();

        long oldHead = 0;
        int numChangeReads = 0;
        int numChangesDropped = 0;
        int numGapsDetected = 0;
        int queueCapacity = storeMetadata.getQueueCapacity();

        verbosePrint("Reading store updates%n");

        while (true) {
            verbosePrint("Reading change queue head at address %d%n", storeMetadata.getChangeQueueHeadAddr());

            long newHead = UNSAFE.getLong(storeMetadata.getChangeQueueHeadAddr());

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
            long id = readChangeQueueIdAt(i);
            if (id == -1) {
                continue;
            }
            RecordAccessor recordAccessor = getRecordAccessor(id);
            for (RecordUpdateListener updateListener : updateListeners) {
                updateListener.processUpdate(recordAccessor);
            }
        }
        return numGapsDetected;
    }

    private RecordAccessor getRecordAccessor(long recordId) {
        if(useThreadLocalRecordUpdates) {
            RecordAccessor recordAccessor = tlRecordAccessor.get();
            recordAccessor.setRecordId(recordId);
            return recordAccessor;
        }
        return new RecordAccessor(recordId, storeMetadata);
    }

    private long readChangeQueueIdAt(long changeQueueHead) {
        int changeQueueIndex = (int)(changeQueueHead & storeMetadata.getQueueMask());
        verbosePrint("Reading at %d (%d)%n", changeQueueHead, changeQueueIndex);
        long id = UNSAFE.getLong(storeMetadata.getChangeQueueAddr() + (changeQueueIndex * IDENTIFIER_SIZE));
        verbosePrint("Read %d from %d%n", id, changeQueueHead);
        return id;
    }

    private Pointer initializeStore() {
        PointerByReference storeReference = new PointerByReference();
        verbosePrint("Opening store at '%s'%n", path);
        exitOnError(CachesterStorageLoader.getInstance().storage_open(storeReference, path));
        return storeReference.getValue();
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

    public void setUpdateListeners(Collection<RecordUpdateListener> updateListeners) {
        for(RecordUpdateListener updateListener : updateListeners) {
            addUpdateListener(updateListener);
        }
    }
}
