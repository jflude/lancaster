SRCS = \
	advert.c \
	clock.c \
	dump.c \
	dict.c \
	error.c \
	latency.c \
	poller.c \
	receiver.c \
	reporter.c \
	sender.c \
	signals.c \
	sock.c \
	spin.c \
	storage.c \
	table.c \
	thread.c \
	twist.c \
	version.c \
	xalloc.c

COMPONENTS = \
	go/src/submgr \
	go/src/pubmgr \
	go/src/rdr

BIN_DIR = bin

include VERSION.mk

CFLAGS = \
	-ansi -pedantic -Wall -Wextra -Werror -g \
	-D_POSIX_C_SOURCE=200112L -D_BSD_SOURCE \
	-DCACHESTER_SOURCE_VERSION='$(CACHESTER_SOURCE_VERSION)' \
	-DCACHESTER_FILE_MAJOR_VERSION=$(CACHESTER_FILE_MAJOR_VERSION) \
	-DCACHESTER_FILE_MINOR_VERSION=$(CACHESTER_FILE_MINOR_VERSION) \
	-DCACHESTER_WIRE_MAJOR_VERSION=$(CACHESTER_WIRE_MAJOR_VERSION) \
	-DCACHESTER_WIRE_MINOR_VERSION=$(CACHESTER_WIRE_MINOR_VERSION)

LDLIBS = -lm
OBJS = $(SRCS:.c=.o)

ifneq (,$(findstring Darwin,$(shell uname -s)))
CFLAGS += -DDARWIN_OS -D_DARWIN_C_SOURCE
else
CFLAGS += -pthread
LDFLAGS += -pthread
LDLIBS += -lrt
endif

ifneq (,$(findstring CYGWIN,$(shell uname -s)))
CFLAGS += -DCYGWIN_OS
SO_EXT = .dll
DEPFLAGS += \
-I/usr/lib/gcc/x86_64-pc-cygwin/4.8.3/include
else
CFLAGS += -DLINUX_OS -fPIC
SO_EXT = .so
DEPFLAGS += \
-I/usr/include/linux \
-I/usr/include/x86_64-linux-gnu \
-I/usr/lib/gcc/x86_64-linux-gnu/4.6/include
endif

all: \
	$(BIN_DIR)/libcachester$(SO_EXT) \
	$(BIN_DIR)/publisher \
	$(BIN_DIR)/subscriber \
	$(BIN_DIR)/reader \
	$(BIN_DIR)/writer \
	$(BIN_DIR)/inspector \
	$(BIN_DIR)/grower

all: 
	@for dir in $(COMPONENTS); do \
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

$(BIN_DIR)/publisher: publisher.o $(BIN_DIR)/libcachester.a
	$(CC) $(LDFLAGS) $^ $(LOADLIBES) $(LDLIBS) -o $@

$(BIN_DIR)/subscriber: subscriber.o $(BIN_DIR)/libcachester.a
	$(CC) $(LDFLAGS) $^ $(LOADLIBES) $(LDLIBS) -o $@

$(BIN_DIR)/reader: reader.o $(BIN_DIR)/libcachester.a
	$(CC) $(LDFLAGS) $^ $(LOADLIBES) $(LDLIBS) -o $@

$(BIN_DIR)/writer: writer.o $(BIN_DIR)/libcachester.a
	$(CC) $(LDFLAGS) $^ $(LOADLIBES) $(LDLIBS) -o $@

$(BIN_DIR)/inspector: inspector.o $(BIN_DIR)/libcachester.a
	$(CC) $(LDFLAGS) $^ $(LOADLIBES) $(LDLIBS) -o $@

$(BIN_DIR)/grower: grower.o $(BIN_DIR)/libcachester.a
	$(CC) $(LDFLAGS) $^ $(LOADLIBES) $(LDLIBS) -o $@

$(BIN_DIR)/libcachester.a: $(OBJS)
	ar -r $(BIN_DIR)/libcachester.a $(OBJS)

$(BIN_DIR)/libcachester$(SO_EXT): $(OBJS)
	$(CC) -shared $(LDFLAGS) $^ $(LDLIBS) -o $@

clean: 
	rm -rf $(BIN_DIR)/libcachester.a $(BIN_DIR)/libcachester$(SO_EXT) \
	    $(BIN_DIR)/writer writer.o writer.dSYM \
		$(BIN_DIR)/publisher publisher.o publisher.dSYM \
	    $(BIN_DIR)/subscriber subscriber.o subscriber.dSYM \
		$(BIN_DIR)/reader reader.o reader.dSYM \
		$(BIN_DIR)/inspector inspector.o inspector.dSYM \
		$(BIN_DIR)/grower grower.o grower.dSYM \
	    $(OBJS)
	@for dir in $(COMPONENTS); do \
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
	@for dir in $(COMPONENTS); do \
	   $(MAKE) -C $$dir fetch; \
	done

.PHONY: all release debug profile depend clean distclean
.PHONY: deps fetch components $(COMPONENTS)

include DEPEND.mk
