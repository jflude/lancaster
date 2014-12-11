LIB_SRCS = \
	a2i.c \
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

APP_SRCS = \
	writer.c \
	reader.c \
	publisher.c \
	subscriber.c \
	inspector.c \
	grower.c \
	deleter.c \
	eraser.c

COMPONENTS = go

BIN_DIR = bin

include VERSION.mk

CFLAGS = \
	-ansi -pedantic -Wall -Wextra -Werror -g \
	-D_POSIX_C_SOURCE=200112L -D_BSD_SOURCE \
	-DCACHESTER_SOURCE_VERSION='$(CACHESTER_SOURCE_VERSION)' \
	-DCACHESTER_FILE_MAJOR_VERSION='$(CACHESTER_FILE_MAJOR_VERSION)' \
	-DCACHESTER_FILE_MINOR_VERSION='$(CACHESTER_FILE_MINOR_VERSION)' \
	-DCACHESTER_WIRE_MAJOR_VERSION='$(CACHESTER_WIRE_MAJOR_VERSION)' \
	-DCACHESTER_WIRE_MINOR_VERSION='$(CACHESTER_WIRE_MINOR_VERSION)'

LDLIBS = -lm

LIB_OBJS = $(LIB_SRCS:.c=.o)
LIB_BINS = $(BIN_DIR)/libcachester.a $(BIN_DIR)/libcachester$(SO_EXT)

APP_OBJS = $(APP_SRCS:.c=.o)
APP_BINS = $(APP_SRCS:%.c=$(BIN_DIR)/%)
APP_DBG =

ifneq (,$(findstring Darwin,$(shell uname -s)))
CFLAGS += -DDARWIN_OS -D_DARWIN_C_SOURCE
APP_DBG = $(APP_SRCS:.c=.dSYM)
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

all: $(LIB_BINS) $(APP_BINS) releaselog
all: 
	@for dir in $(COMPONENTS); do \
		$(MAKE) -C $$dir all; \
	done

$(BIN_DIR)/libcachester.a: $(LIB_OBJS)
	ar -r $(BIN_DIR)/libcachester.a $(LIB_OBJS)

$(BIN_DIR)/libcachester$(SO_EXT): $(LIB_OBJS)
	$(CC) -shared $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BIN_DIR)/%: %.c $(LIB_BINS)
	$(CC) $(LDFLAGS) $^ $(LOADLIBES) $(LDLIBS) -o $@

release: CFLAGS += -DNDEBUG -O3
release: fetch depend all
release:
	@for dir in $(COMPONENTS); do \
		$(MAKE) -C $$dir release; \
	done

profile: CFLAGS += -pg
profile: LDFLAGS += -pg
profile: all

protocol: CFLAGS += -DDEBUG_PROTOCOL
protocol: all

gaps: CFLAGS += -DDEBUG_GAPS
gaps: all

depend: CFLAGS += -DDEBUG_PROTOCOL -DDEBUG_GAPS
depend: DEPEND.mk
	makedepend -f DEPEND.mk $(DEPFLAGS) -DMAKE_DEPEND -- $(CFLAGS) -- \
		$(LIB_SRCS) $(APP_SRCS)

fetch:
	@rm -f go/Goopfile.lock
	@for dir in $(COMPONENTS); do \
		$(MAKE) -C $$dir fetch; \
	done

clean: 
	rm -rf $(LIB_OBJS) $(LIB_BINS) $(APP_OBJS) $(APP_BINS)
	@for dir in $(COMPONENTS); do \
		$(MAKE) -C $$dir clean; \
	done

distclean: clean
	rm -rf \
		DEPEND.mk \
		$(BIN_DIR)/core $(BIN_DIR)/core.* $(BIN_DIR)/*.stackdump \
		go/Goopfile.lock go/.vendor \
		`find . -name '*~' -o -name '*.bak'`

DEPEND.mk:
	touch DEPEND.mk

releaselog:
	git log -n1 > RELEASE_LOG

include DEPEND.mk

.PHONY: all release profile protocol gaps depend fetch clean distclean
.PHONY: $(COMPONENTS)
