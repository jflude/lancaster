package com.peak6.util;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

public class EndiannessUtil {

    public static double swapEndiannessDouble(double aDouble) {
        ByteBuffer buffer = ByteBuffer.allocate(8).order(ByteOrder.LITTLE_ENDIAN).putDouble(aDouble);
        buffer.rewind();
        buffer.order(ByteOrder.BIG_ENDIAN);
        return buffer.getDouble();
    }

    public static long swapEndiannessLong(long aLong) {
        ByteBuffer buffer = ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putLong(aLong);
        buffer.rewind();
        buffer.order(ByteOrder.BIG_ENDIAN);
        return buffer.getLong();
    }

    public static int swapEndiannessInt(int anInt) {
        ByteBuffer buffer = ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt(anInt);
        buffer.rewind();
        buffer.order(ByteOrder.BIG_ENDIAN);
        return buffer.getInt();
    }

}
