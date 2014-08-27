SRCS = \
accum.c \
advert.c \
circ.c \
clock.c \
dump.c \
dict.c \
error.c \
poller.c \
receiver.c \
sender.c \
signals.c \
sock.c \
storage.c \
table.c \
thread.c \
twist.c \
uudict.c \
xalloc.c \
yield.c

OBJS = $(SRCS:.c=.o)

ifneq (,$(findstring CYGWIN,$(shell uname -s)))
SO_EXT = .dll
else
CFLAGS += -fPIC
SO_EXT = .so
endif

all: libcachester.a libcachester$(SO_EXT)

release: all

libcachester.a: $(OBJS)
	ar -r libcachester.a $(OBJS)

libcachester$(SO_EXT): $(OBJS)
	$(CC) -shared $(LDFLAGS) $^ $(LDLIBS) -o $@

DEPEND.mk:
	touch DEPEND.mk

.PHONY: depend clean distclean

depend: DEPEND.mk
	makedepend -f DEPEND.mk $(DEPFLAGS) -DMAKE_DEPEND -- $(CFLAGS) -- \
	    $(SRCS)

clean:
	rm -f libcachester.a libcachester$(SO_EXT) \
	    $(OBJS)

distclean: clean
	rm -f DEPEND.mk *~ *.bak core core.* *.stackdump

include DEPEND.mk
