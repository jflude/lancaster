SRCS := \
	advert.c \
	clock.c \
	dump.c \
	dict.c \
	error.c \
	latency.c \
	poller.c \
	receiver.c \
	sender.c \
	signals.c \
	sock.c \
	spin.c \
	storage.c \
	table.c \
	udp.c \
	thread.c \
	twist.c \
	version.c \
	xalloc.c

COMPONENTS := \
	go/src/submgr \
	go/src/pubmgr \
	go/src/rdr

include VERSION.mk

CFLAGS := \
	-ansi -pedantic -Wall -Wextra -Werror -g \
	-D_POSIX_C_SOURCE=200112L -D_BSD_SOURCE \
	-DCACHESTER_SOURCE_VERSION='$(CACHESTER_SOURCE_VERSION)' \
	-DCACHESTER_FILE_MAJOR_VERSION=$(CACHESTER_FILE_MAJOR_VERSION) \
	-DCACHESTER_FILE_MINOR_VERSION=$(CACHESTER_FILE_MINOR_VERSION) \
	-DCACHESTER_WIRE_MAJOR_VERSION=$(CACHESTER_WIRE_MAJOR_VERSION) \
	-DCACHESTER_WIRE_MINOR_VERSION=$(CACHESTER_WIRE_MINOR_VERSION)

LDLIBS := -lm
OBJS := $(SRCS:.c=.o)

BIN_DIR := "bin"

ifneq (,$(findstring Darwin,$(shell uname -s)))
CFLAGS += -D_DARWIN_C_SOURCE
else
CFLAGS += -pthread
LDFLAGS += -pthread
LDLIBS += -lrt
endif

ifneq (,$(findstring CYGWIN,$(shell uname -s)))
SO_EXT := .dll
DEPFLAGS += \
-I/usr/lib/gcc/x86_64-pc-cygwin/4.8.3/include
else
SO_EXT := .so
CFLAGS += -fPIC
DEPFLAGS += \
-I/usr/include/linux \
-I/usr/include/x86_64-linux-gnu \
-I/usr/lib/gcc/x86_64-linux-gnu/4.6/include
endif

all: libcachester$(SO_EXT) publisher subscriber reader writer inspector grower
all: 
	for dir in $(COMPONENTS); do \
	    $(MAKE) -C $$dir all; \
	done

release: CFLAGS += -DNDEBUG -O3
release: deps all
release: 
	for dir in $(COMPONENTS); do \
	    $(MAKE) -C $$dir release; \
	done

debug: CFLAGS += -DDEBUG_PROTOCOL
debug: all

profile: CFLAGS += -pg
profile: LDFLAGS += -pg
profile: all

publisher: publisher.o libcachester.a

subscriber: subscriber.o libcachester.a

reader: reader.o libcachester.a

writer: writer.o libcachester.a

inspector: inspector.o libcachester.a

grower: grower.o libcachester.a

libcachester.a: $(OBJS)
	ar -r libcachester.a $(OBJS)

libcachester$(SO_EXT): $(OBJS)
	$(CC) -shared $(LDFLAGS) $^ $(LDLIBS) -o $@

clean: 
	rm -rf libcachester.a libcachester$(SO_EXT) \
	    writer writer.o publisher publisher.o \
	    subscriber subscriber.o reader reader.o \
		inspector.o inspector grower.o grower \
		reader.dSYM writer.dSYM \
		publisher.dSYM subscriber.dSYM \
		inspector.dSYM grower.dSYM \
	    $(OBJS) \
            $(BIN_DIR)/publisher $(BIN_DIR)/subscriber
	for dir in $(COMPONENTS); do \
	    $(MAKE) -C $$dir clean; \
	done

distclean: clean
	rm -f DEPEND.mk *~ *.bak core core.* *.stackdump

deps: fetch depend

DEPEND.mk:
	touch DEPEND.mk

depend: DEPEND.mk
	makedepend -f DEPEND.mk $(DEPFLAGS) -DMAKE_DEPEND -- $(CFLAGS) -- \
	    writer.c publisher.c subscriber.c reader.c inspector.c grower.c \
	    $(SRCS)

fetch:
	for dir in $(COMPONENTS); do \
	   $(MAKE) -C $$dir fetch; \
	done

.PHONY: all release debug profile depend clean distclean
.PHONY: deps fetch components $(COMPONENTS)

include DEPEND.mk
