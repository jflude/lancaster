## Makefile.am -- Process this file with automake to produce Makefile.in
AUTOMAKE_OPTIONS = subdir-objects

bin_PROGRAMS = copier deleter eraser grower inspector publisher reader subscriber writer
lib_LIBRARIES = liblancaster.a

liblancaster_a_SOURCES = src/a2i.c src/clock.c src/dict.c src/error.c src/latency.c src/sender.c src/spin.c src/table.c src/twist.c src/xalloc.c src/advert.c src/dump.c src/grower.c src/poller.c src/receiver.c src/signals.c src/storage.c src/thread.c src/version.c src/batch.c src/reporter.c src/socket.c src/toucher.c

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
