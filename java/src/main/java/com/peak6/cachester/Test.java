package com.peak6.cachester;

import com.sun.jna.Library;
import com.sun.jna.Native;
import com.sun.jna.Pointer;
import com.sun.jna.ptr.PointerByReference;

/**
 * Created on 7/23/14 @ 4:51 PM by dbudworth.
 */
public class Test {
    public interface RT extends Library{
        RT INSTANCE = (RT)
                Native.loadLibrary("rt",
                        RT.class);

    }
    public interface Cachester extends Library {
        Cachester INSTANCE = (Cachester)
                Native.loadLibrary("/home/dbudworth/gits/cachester/libcachester.so",
                        Cachester.class);

        int storage_create(PointerByReference store, String path, int queue, int dunno, int records, int recordSz);

        int storage_reset(Pointer p);

        String error_last_desc();
    }

    public static void main(String[] args) {
        String path = "";
        if (args.length > 0) {
            path = args[0];
        }
        System.out.println("RT: "+RT.INSTANCE);
        System.out.println("Using path: " + path);
        PointerByReference pref = new PointerByReference();
        System.out.println("Create");
        exitOnError(Cachester.INSTANCE.storage_create(pref, path, 128, 0, 1000, 100));
        System.out.println("Reset");
        exitOnError(Cachester.INSTANCE.storage_reset(pref.getValue()));
    }

    static void exitOnError(int status) {
        if (status == 0) {
            return;
        }
        System.err.println("Error: " + Cachester.INSTANCE.error_last_desc());
        System.exit(1);
    }
}
