package com.peak6.cachester;

import com.sun.jna.Native;

public class CachesterStorageLoader {
    private static final String LIB_PATH_SYSTEM_PROPERTY = "cachcester.lib.path";
    private static CachesterStorage INSTANCE;

    public static CachesterStorage initializeWithLibraryPath(String path) {
        if(INSTANCE == null) {
            INSTANCE = (CachesterStorage) Native.loadLibrary(path, CachesterStorage.class);
        }
        return INSTANCE;
    }

    public static CachesterStorage getInstance() {
        if(INSTANCE != null) {
            return INSTANCE;
        }
        if(System.getProperty(LIB_PATH_SYSTEM_PROPERTY) != null) {
            return initializeWithLibraryPath(System.getProperty(LIB_PATH_SYSTEM_PROPERTY));
        }
        throw new RuntimeException("Must first initialize storage with path to cachester library. Please specify system property " + LIB_PATH_SYSTEM_PROPERTY );
    }
}
