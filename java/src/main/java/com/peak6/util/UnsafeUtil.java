package com.peak6.util;

import java.lang.reflect.Field;

import sun.misc.Unsafe;

public class UnsafeUtil {

    public static final Unsafe UNSAFE;

    static {
        try {
            @SuppressWarnings("ALL")
            Field theUnsafe = Unsafe.class.getDeclaredField("theUnsafe");
            theUnsafe.setAccessible(true);
            UNSAFE = (Unsafe) theUnsafe.get(null);
        }
        catch (Exception e) {
            throw new AssertionError(e);
        }
    }

}
