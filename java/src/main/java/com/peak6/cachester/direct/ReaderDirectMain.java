package com.peak6.cachester.direct;

import java.io.IOException;

import com.peak6.cachester.direct.update.PrintingNewRecordListener;
import com.peak6.cachester.direct.update.QuotePrintingUpdateListener;


public class ReaderDirectMain {
    public static void main(String[] args) throws InterruptedException, IOException {
        String path = "/tmp/cachester.store";
        if (args.length > 0) {
            path = args[0];
        }
        CachesterDirectReader.createVerbose(path)
                             .addUpdateListener(new QuotePrintingUpdateListener())
                             .addNewRecordListener(new PrintingNewRecordListener())
                             .readStoreUpdates();
    }
}
