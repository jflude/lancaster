package com.peak6.cachester.mapped;

import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.ByteOrder;
import java.nio.MappedByteBuffer;
import java.nio.channels.FileChannel;
import java.nio.channels.FileChannel.MapMode;

import com.peak6.cachester.CachesterStorage;

public class CachesterMapppedFile {

    static final int MAGIC_NUMBER = 0x0C0FFEE0;
    static final int MAGIC_NUMBER_INDEX = 0;
    static final ByteOrder BYTE_ORDER = ByteOrder.LITTLE_ENDIAN;

    private final String path;

    private final int mappedFileSize = 48033792;
    private final Header header;
    private final boolean verbose;

    private final MappedByteBuffer mappedByteBuffer;
    private final

    class Header {
        int magic;
        int owner_pid;
        long mmap_size;
        long hdr_size;
        long rec_size;
        long val_size;
        long base_id;
        long max_id;
        volatile long high_water_id;
        volatile int high_water_lock;
        volatile int time_lock;
        long send_recv_time;
        int q_mask;
        int q_head_index;
        int q_start_index;
    };
    // change_q
    // values

    public Header readHeader() throws IOException {
        mappedByteBuffer.rewind();
        Header header = new Header();
        header.magic = mappedByteBuffer.getInt();
        if(header.magic != MAGIC_NUMBER) {
            throw new IOException("Problem reading magic number from file at " + this.path + ". Read magic number of " + header.magic + " at index " + MAGIC_NUMBER_INDEX);
        }
        header.owner_pid = mappedByteBuffer.getInt();
        header.mmap_size = mappedByteBuffer.getLong();
        header.hdr_size = mappedByteBuffer.getLong();
        header.rec_size = mappedByteBuffer.getLong();
        header.val_size = mappedByteBuffer.getLong();
        header.base_id = mappedByteBuffer.getLong();
        header.max_id = mappedByteBuffer.getLong();
        header.high_water_id = mappedByteBuffer.getLong();
        header.high_water_lock  = mappedByteBuffer.getInt();
        header.time_lock = mappedByteBuffer.getInt();
        header.send_recv_time  = mappedByteBuffer.getLong();
        header.q_mask = mappedByteBuffer.getInt();
        header.q_head_index = mappedByteBuffer.position();
        header.q_start_index = mappedByteBuffer.position() + 4;
        return header;
    }

    private CachesterMapppedFile(String path, boolean verbose) throws IOException {
        this.path = path;
        this.verbose = verbose;
        mappedByteBuffer = getMappedByteBuffer(path);
        header = readHeader();

        verbosePrint("Using path '%s'%n", path);
        addShutdownHookToDestroyStore();
    }

    public int getQueueCapacity() {
        return header.q_mask + 1;
    }

    public long getQueueHead() {
        return mappedByteBuffer.getLong(header.q_head_index);
    }

    public int getChangeQueueId(long changeQueueIndex) {
        return mappedByteBuffer.get((int)changeQueueIndex);
    }

    private MappedByteBuffer getMappedByteBuffer(String path) throws IOException {
        try(RandomAccessFile file = new RandomAccessFile(path, "rw")) {
            FileChannel fileChannel = file.getChannel();
            return (MappedByteBuffer)fileChannel.map(MapMode.READ_WRITE, 0, mappedFileSize).order(BYTE_ORDER);
        }
    }

    public static CachesterMapppedFile create(String path) throws IOException {
        return new CachesterMapppedFile(path, false);
    }

    public static CachesterMapppedFile createVerbose(String path) throws IOException {
        return new CachesterMapppedFile(path, true);
    }

    public static CachesterMapppedFile create(String path, boolean verbose) throws IOException {
        return new CachesterMapppedFile(path, verbose);
    }

    private void addShutdownHookToDestroyStore() {
        verbosePrint("Adding shutdown hook to destroy store at '%s'%n", path);
        Runtime.getRuntime().addShutdownHook(new Thread() {
            @Override
            public void run() {
                verbosePrint("Shutdown hook running! Destroying store at '%s'%n", path);
                //TODO: Destroy the store here
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
        System.err.printf("Error: %s %n", CachesterStorage.INSTANCE.error_last_desc());
        System.err.flush();
        System.exit(1);
        return status;
    }

}
