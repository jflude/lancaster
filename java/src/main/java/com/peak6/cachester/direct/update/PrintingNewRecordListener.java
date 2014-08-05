package com.peak6.cachester.direct.update;

import java.util.HashSet;
import java.util.Set;

public class PrintingNewRecordListener implements NewRecordListener {

    private final Set<Long> recordIdsSeen = new HashSet<Long>();

    @Override
    public void added(RecordAccessor record) {
        recordIdsSeen.add(record.getRecordId());
        if((record.getRecordId() % 1000) == 0) {
            System.out.printf("Discovered record %d. Seen %d records so far.%n", record.getRecordId(), recordIdsSeen.size());
        }
    }

}
