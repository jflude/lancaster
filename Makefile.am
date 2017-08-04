## Makefile.am -- Process this file with automake to produce Makefile.in
AUTOMAKE_OPTIONS = subdir-objects

bin_PROGRAMS = copier deleter eraser grower inspector publisher reader \
	subscriber writer

lib_LIBRARIES = liblancaster.a
dist_bin_SCRIPTS = scripts/pubctl scripts/subctl
dist_doc_DATA = COPYING.LESSER README

liblancaster_a_SOURCES = src/a2i.c src/a2i.h src/advert.c src/advert.h \
	src/batch.c src/batch.h src/clock.c src/clock.h src/datum.h src/dict.c \
	src/dict.h src/dump.c src/dump.h src/error.c src/error.h src/h2n2h.h \
	src/latency.c src/latency.h src/poller.c src/poller.h src/receiver.c \
	src/receiver.h src/reporter.c src/reporter.h src/sender.c src/sender.h \
	src/sequence.h src/signals.c src/signals.h src/socket.c src/socket.h \
	src/spin.c src/spin.h src/status.h src/storage.c src/storage.h \
	src/sync.h src/table.c src/table.h src/thread.c src/thread.h \
	src/toucher.c src/toucher.h src/twist.c src/twist.h src/version.c \
	src/version.h src/xalloc.c src/xalloc.h

LDADD = liblancaster.a

copier_SOURCES = src/copier.c
deleter_SOURCES = src/deleter.c
eraser_SOURCES = src/eraser.c
grower_SOURCES = src/grower.c
inspector_SOURCES = src/inspector.c
publisher_SOURCES = src/publisher.c
reader_SOURCES = src/reader.c
subscriber_SOURCES = src/subscriber.c
writer_SOURCES = src/writer.c
