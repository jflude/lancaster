# Copyright (C)2014-2016 Peak6 Investments, LP.  All rights reserved.
# Use of this source code is governed by the LICENSE file.

LIB_SRCS = \
	a2i.c \
	advert.c \
	batch.c \
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
	socket.c \
	spin.c \
	storage.c \
	table.c \
	toucher.c \
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
	eraser.c \
	copier.c

LIB_STATIC = liblancaster.a
LIB_DYNAMIC = liblancaster$(SO_EXT)

COMPONENTS = go

BIN_DIR = bin
LIB_DIR = lib

include VERSION.mk

CFLAGS = \
	-ansi -pedantic -Wall -Wextra -Werror -g \
	-D_POSIX_C_SOURCE=200112L -D_BSD_SOURCE \
	-DLANCASTER_SOURCE_VERSION='$(LANCASTER_SOURCE_VERSION)' \
	-DLANCASTER_FILE_MAJOR_VERSION='$(LANCASTER_FILE_MAJOR_VERSION)' \
	-DLANCASTER_FILE_MINOR_VERSION='$(LANCASTER_FILE_MINOR_VERSION)' \
	-DLANCASTER_WIRE_MAJOR_VERSION='$(LANCASTER_WIRE_MAJOR_VERSION)' \
	-DLANCASTER_WIRE_MINOR_VERSION='$(LANCASTER_WIRE_MINOR_VERSION)'

LDLIBS = -lm

LIB_OBJS = $(LIB_SRCS:.c=.o)
LIB_STATIC_BIN = $(LIB_DIR)/$(LIB_STATIC)
LIB_DYNAMIC_BIN = $(LIB_DIR)/$(LIB_DYNAMIC)
LIB_BINS = $(LIB_STATIC_BIN) $(LIB_DYNAMIC_BIN)

APP_OBJS = $(APP_SRCS:.c=.o)
APP_BINS = $(APP_OBJS:%.o=$(BIN_DIR)/%)
APP_DBG =

OS_NAME = $(shell uname)

ifneq (,$(findstring Darwin,$(OS_NAME)))
CFLAGS += -DDARWIN_OS -D_DARWIN_C_SOURCE
APP_DBG = $(APP_SRCS:.c=.dSYM)
else ifneq (,$(findstring CYGWIN,$(OS_NAME)))
CFLAGS += -DCYGWIN_OS -pthread
LDFLAGS += -pthread
LDLIBS += -lrt
SO_EXT = .dll
else ifneq (,$(findstring Linux,$(OS_NAME)))
CFLAGS += -DLINUX_OS -fPIC -pthread
LDFLAGS += -pthread
LDLIBS += -lrt
SO_EXT = .so
else
$(error Unknown operating system: $(OS_NAME))
endif

all: $(LIB_BINS) $(APP_BINS)
all:
	@for dir in $(COMPONENTS); do \
		$(MAKE) -C $$dir all || break ; \
	done

$(LIB_STATIC_BIN): $(LIB_OBJS)
	ar -r $@ $(LIB_OBJS)

$(LIB_DYNAMIC_BIN): $(LIB_OBJS)
	$(CC) -shared $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BIN_DIR)/%: %.o $(LIB_STATIC_BIN)
	$(CC) $(LDFLAGS) $^ $(LOADLIBES) $(LDLIBS) -o $@

$(BIN_DIR)/%.o: %.c
	$(CC) $^ -c $@

release: CFLAGS += -DNDEBUG -O3
release: fetch depend all
release:
	@for dir in $(COMPONENTS); do \
		$(MAKE) -C $$dir release || break ; \
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
	$(CC) -M $(CFLAGS) -DMAKE_DEPEND $(LIB_SRCS) $(APP_SRCS) > DEPEND.mk

clean:
	rm -rf $(LIB_OBJS) $(LIB_BINS) $(APP_OBJS) $(APP_BINS)
	@for dir in $(COMPONENTS); do \
		$(MAKE) -C $$dir clean || break ; \
	done

distclean: clean
	rm -rf \
		DEPEND.mk \
		$(BIN_DIR)/core $(BIN_DIR)/core.* $(BIN_DIR)/*.stackdump \
		`find . -name '*~' -o -name '*.bak'`

DEPEND.mk:
	touch DEPEND.mk

.PHONY: all release profile protocol gaps
.PHONY: depend fetch clean distclean
.PHONY: $(COMPONENTS)

.SECONDARY: $(APP_OBJS)

include DEPEND.mk
