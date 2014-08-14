SRCS = \
accum.c \
advert.c \
circ.c \
clock.c \
dump.c \
dict.c \
error.c \
poll.c \
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

CFLAGS = -ansi -pedantic -Wall -Wextra -pthread -D_POSIX_C_SOURCE=200112L -D_BSD_SOURCE -g
LDFLAGS = -pthread
LDLIBS = -lrt -lm
OBJS = $(SRCS:.c=.o)

DEPFLAGS = \
-I/usr/include/linux \
-I/usr/include/x86_64-linux-gnu \
-I/usr/lib/gcc/x86_64-linux-gnu/4.6/include \
-I/usr/lib/gcc/x86_64-pc-cygwin/4.8.3/include

ifneq (,$(findstring CYGWIN,$(shell uname -s)))
SO_EXT = .dll
else
CFLAGS += -fPIC
SO_EXT = .so
endif

all: publisher subscriber reader libcachester$(SO_EXT)

release: CFLAGS += -DNDEBUG -O3
release: all

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
	makedepend -f DEPEND.mk $(DEPFLAGS) -DMAKE_DEPEND -- $(CFLAGS) -- $(SRCS) publisher.c subscriber.c reader.c

clean:
	rm -f libcachester.a libcachester$(SO_EXT) publisher publisher.o subscriber subscriber.o reader reader.o $(OBJS)

distclean: clean
	rm -f DEPEND.mk *~ *.bak core core.* *.stackdump

include DEPEND.mk
