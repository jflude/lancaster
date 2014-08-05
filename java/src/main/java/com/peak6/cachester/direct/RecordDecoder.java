package com.peak6.cachester.direct;

import com.peak6.cachester.direct.update.RecordAccessor;

public interface RecordDecoder<T> {

    public T decode(RecordAccessor record);

}
