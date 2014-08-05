package com.peak6.cachester;

public class QuoteRecord {
    public double bid;
    public double ask;
    public int bidQuantity;
    public int askQuantity;

    @Override
    public String toString() {
        return "QuoteRecord [bid=" + bid + ", ask=" + ask + ", bidQuantity=" + bidQuantity + ", askQuantity="
                + askQuantity + "]";
    }

}
