## Makefile.am -- Process this file with automake to produce Makefile.in
AUTOMAKE_OPTIONS = subdir-objects
ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS = copier deleter eraser grower inspector publisher reader \
	subscriber writer

lib_LTLIBRARIES = liblancaster.la

nobase_include_HEADERS = \
	lancaster/a2i.h \
	lancaster/advert.h \
	lancaster/batch.h \
	lancaster/clock.h \
	lancaster/config.h \
	lancaster/datum.h \
	lancaster/dict.h \
	lancaster/dump.h \
	lancaster/error.h \
	lancaster/h2n2h.h \
	lancaster/int64.h \
	lancaster/latency.h \
	lancaster/poller.h \
	lancaster/receiver.h \
	lancaster/reporter.h \
	lancaster/sender.h \
	lancaster/sequence.h \
	lancaster/signals.h \
	lancaster/socket.h \
	lancaster/spin.h \
	lancaster/status.h \
	lancaster/storage.h \
	lancaster/sync.h \
	lancaster/table.h \
	lancaster/thread.h \
	lancaster/toucher.h \
	lancaster/version.h \
	lancaster/xalloc.h

dist_bin_SCRIPTS = scripts/pubctl scripts/subctl
dist_doc_DATA = COPYING README

EXTRA_DIST = ffi
DISTCLEANFILES = lancaster/config.h

liblancaster_la_SOURCES = \
	src/a2i.c \
	src/advert.c \
	src/batch.c \
	src/clock.c \
	src/dict.c \
	src/dump.c \
	src/error.c \
	src/latency.c \
	src/poller.c \
	src/receiver.c \
	src/reporter.c \
	src/sender.c \
	src/signals.c \
	src/socket.c \
	src/spin.c \
	src/storage.c \
	src/table.c \
	src/thread.c \
	src/toucher.c \
	src/version.c \
	src/xalloc.c \
	src/xalloca.h

LDADD = liblancaster.la -lm

copier_SOURCES = src/copier.c
deleter_SOURCES = src/deleter.c
eraser_SOURCES = src/eraser.c
grower_SOURCES = src/grower.c
inspector_SOURCES = src/inspector.c
publisher_SOURCES = src/publisher.c
reader_SOURCES = src/reader.c
subscriber_SOURCES = src/subscriber.c
writer_SOURCES = src/writer.c
