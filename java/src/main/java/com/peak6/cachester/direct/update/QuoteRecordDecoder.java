package com.peak6.cachester.direct.update;

import com.peak6.cachester.QuoteRecord;
import com.peak6.cachester.direct.RecordDecoder;

public class QuoteRecordDecoder implements RecordDecoder<QuoteRecord> {
    @Override
    public QuoteRecord decode(RecordAccessor record) {
        QuoteRecord quoteRecord = new QuoteRecord();
        quoteRecord.bid = record.getDouble(0);
        quoteRecord.ask = record.getDouble(8);
        quoteRecord.bidQuantity = record.getInt(16);
        quoteRecord.askQuantity = record.getInt(20);
        return quoteRecord;
    }
}