package com.peak6.cachester.direct.update;

import com.peak6.cachester.QuoteRecord;
import com.peak6.cachester.direct.RecordDecoder;


public class QuotePrintingUpdateListener implements RecordUpdateListener {

    private int oldN;

    private final RecordDecoder<QuoteRecord> quoteRecordDecoder = new QuoteRecordDecoder();

    @Override
    public void processUpdate(RecordAccessor record) {
        decodeUpdate(record);
//        readUpdate(record);
    }

    public void decodeUpdate(RecordAccessor record) {
        QuoteRecord quoteRecord = record.decodeConsistently(quoteRecordDecoder);
        if((record.getRecordId() % 1000) == 0) {
            System.out.printf("Read quote for record %d with sizes %d x %d%n", + record.getRecordId(), quoteRecord.bidQuantity, quoteRecord.askQuantity);
        }
        if(oldN + 2 != quoteRecord.bidQuantity) {
            System.err.printf("Detected gap on record %d because %d + 2 != %d %n",record.getRecordId(), oldN, quoteRecord.bidQuantity);
        }
        oldN = quoteRecord.bidQuantity;
    }

    public void readUpdate(RecordAccessor record) {
        byte[] quoteValues = record.readConsistently();
        int bidQuantity = quoteValues[16];
        int askQuantity = quoteValues[20];
        if((record.getRecordId() % 1000) == 0) {
            System.out.printf("Read quote for record %d with sizes %d x %d%n", + record.getRecordId(), bidQuantity, askQuantity);
        }
        if(oldN + 2 != bidQuantity) {
            System.err.printf("Detected gap on record %d because %d + 2 != %d %n",record.getRecordId(), oldN, bidQuantity);
        }
        oldN = bidQuantity;
    }
}
