export CFLAGS = -ansi -pedantic -Wall -Wextra -pthread -D_POSIX_C_SOURCE=200112L -D_BSD_SOURCE -g -Ilib
export LDFLAGS = -pthread
export LDLIBS = -lrt -lm

export DEPFLAGS = \
-I/usr/include/linux \
-I/usr/include/x86_64-linux-gnu \
-I/usr/lib/gcc/x86_64-linux-gnu/4.6/include \
-I/usr/lib/gcc/x86_64-pc-cygwin/4.8.3/include

LIBS = lib/libcachester.a

all: writer publisher subscriber reader

release: CFLAGS += -DNDEBUG -O3
release: all

writer: $(LIBS)

publisher: $(LIBS)

subscriber: $(LIBS)

reader: $(LIBS)

$(LIBS):
	$(MAKE) -C lib

DEPEND.mk:
	touch DEPEND.mk && \
	$(MAKE) -C lib DEPEND.mk

.PHONY: depend clean distclean

depend: DEPEND.mk
	makedepend -f DEPEND.mk $(DEPFLAGS) -DMAKE_DEPEND -- $(CFLAGS) -- \
	    writer.c publisher.c subscriber.c reader.c \
	    $(SRCS) && \
	$(MAKE) -C lib depend

clean:
	rm -f libcachester.a libcachester$(SO_EXT) \
	    writer writer.o publisher publisher.o subscriber subscriber.o reader reader.o && \
	$(MAKE) -C lib clean

distclean: clean
	rm -f DEPEND.mk *~ *.bak core core.* *.stackdump && \
	$(MAKE) -C lib distclean

include DEPEND.mk
