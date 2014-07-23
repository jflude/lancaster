package com.peak6.cachester;

import com.sun.jna.Library;
import com.sun.jna.Native;
import com.sun.jna.ptr.PointerByReference;

/**
 * Created on 7/23/14 @ 4:51 PM by dbudworth.
 */
public class Test {
    public interface Cachester extends Library {
        Cachester INSTANCE = (Cachester)
                Native.loadLibrary("/home/dbudworth/gits/cachester/cachester",
                        Cachester.class);

        int storage_create(PointerByReference store, String path, int queue, int dunno, int records, int recordSz);
    }

    public static void main(String[] args) {
        System.out.println(System.getProperty("user.dir"));
        PointerByReference pref = new PointerByReference();
        System.out.println("Result: " + Cachester.INSTANCE.storage_create(pref, "./foo.store", 128, 0, 1000, 1));
    }
}
