SRCS = \
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
xalloc.c \
yield.c

CFLAGS = -ansi -pedantic -Wall -Wextra -g \
	-D_POSIX_C_SOURCE=200112L -D_BSD_SOURCE
LDLIBS = -lm
OBJS = $(SRCS:.c=.o)

DEPFLAGS = \
-I/usr/include/linux \
-I/usr/include/x86_64-linux-gnu \
-I/usr/lib/gcc/x86_64-linux-gnu/4.6/include \
-I/usr/lib/gcc/x86_64-pc-cygwin/4.8.3/include

ifneq (,$(findstring Darwin,$(shell uname -s)))
CFLAGS += -D_DARWIN_C_SOURCE
else
CFLAGS += -pthread
LDFLAGS += -pthread
LDLIBS += -lrt
endif

ifneq (,$(findstring CYGWIN,$(shell uname -s)))
SO_EXT = .dll
else
CFLAGS += -fPIC
SO_EXT = .so
endif

all: writer publisher subscriber reader libcachester$(SO_EXT)

release: CFLAGS += -DNDEBUG -O3
release: all

writer: libcachester.a

publisher: libcachester.a

subscriber: libcachester.a

reader: libcachester.a

libcachester.a: $(OBJS)
	ar -r libcachester.a $(OBJS)

libcachester$(SO_EXT): $(OBJS)
	$(CC) -shared $(LDFLAGS) $^ $(LDLIBS) -o $@

DEPEND.mk:
	touch DEPEND.mk

depend: DEPEND.mk
	makedepend -f DEPEND.mk $(DEPFLAGS) -DMAKE_DEPEND -- $(CFLAGS) -- \
	    writer.c publisher.c subscriber.c reader.c \
	    $(SRCS)

clean:
	rm -rf libcachester.a libcachester$(SO_EXT) \
	    writer writer.o publisher publisher.o \
	    subscriber subscriber.o reader reader.o \
		reader.dSYM writer.dSYM publisher.dSYM subscriber.dSYM \
	    $(OBJS)

distclean: clean
	rm -f DEPEND.mk *~ *.bak core core.* *.stackdump

.PHONY: all release depend clean distclean

include DEPEND.mk
