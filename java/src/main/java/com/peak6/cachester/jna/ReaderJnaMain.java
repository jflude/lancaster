package com.peak6.cachester.jna;

import com.peak6.cachester.CachesterStorageLoader;

/**
 * Created on 7/23/14 @ 4:51 PM by dbudworth.
 */
public class ReaderJnaMain {

    public static void main(String[] args) throws InterruptedException {
        CachesterStorageLoader.initializeWithLibraryPath(System.getenv("HOME") + "/cachester/libcachester.so");
        String path = "/tmp/cachester.store";
        if (args.length > 0) {
            path = args[0];
        }
        CachesterJnaReader.createVerbose(path).readStoreUpdates();
    }
}
