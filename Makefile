SRCS = \
accum.c \
circ.c \
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

depend:
	makedepend $(DEPFLAGS) -- $(CFLAGS) -- $(SRCS) publisher.c subscriber.c reader.c

clean:
	rm -f libcachester.a libcachester$(SO_EXT) publisher publisher.o subscriber subscriber.o reader reader.o $(OBJS)

distclean: clean
	rm -f *~ *.bak core core.* *.stackdump

# DO NOT DELETE

accum.o: accum.h status.h /usr/include/linux/stddef.h error.h xalloc.h
accum.o: /usr/include/linux/string.h /usr/include/linux/errno.h
accum.o: /usr/include/x86_64-linux-gnu/asm/errno.h
accum.o: /usr/include/asm-generic/errno.h
accum.o: /usr/include/asm-generic/errno-base.h
accum.o: /usr/include/x86_64-linux-gnu/sys/time.h /usr/include/features.h
accum.o: /usr/include/x86_64-linux-gnu/bits/predefs.h
accum.o: /usr/include/x86_64-linux-gnu/sys/cdefs.h
accum.o: /usr/include/x86_64-linux-gnu/bits/wordsize.h
accum.o: /usr/include/x86_64-linux-gnu/gnu/stubs.h
accum.o: /usr/include/x86_64-linux-gnu/gnu/stubs-64.h
accum.o: /usr/include/x86_64-linux-gnu/bits/types.h
accum.o: /usr/include/x86_64-linux-gnu/bits/typesizes.h
accum.o: /usr/include/linux/time.h /usr/include/linux/types.h
accum.o: /usr/include/x86_64-linux-gnu/asm/types.h
accum.o: /usr/include/asm-generic/types.h /usr/include/asm-generic/int-ll64.h
accum.o: /usr/include/x86_64-linux-gnu/asm/bitsperlong.h
accum.o: /usr/include/asm-generic/bitsperlong.h
accum.o: /usr/include/linux/posix_types.h /usr/include/linux/stddef.h
accum.o: /usr/include/x86_64-linux-gnu/asm/posix_types.h
accum.o: /usr/include/x86_64-linux-gnu/asm/posix_types_64.h
accum.o: /usr/include/x86_64-linux-gnu/bits/time.h
accum.o: /usr/include/x86_64-linux-gnu/sys/select.h
accum.o: /usr/include/x86_64-linux-gnu/bits/select.h
accum.o: /usr/include/x86_64-linux-gnu/bits/sigset.h
circ.o: circ.h status.h barrier.h error.h xalloc.h
circ.o: /usr/include/linux/stddef.h /usr/include/linux/string.h yield.h
dump.o: dump.h status.h /usr/include/stdio.h /usr/include/features.h
dump.o: /usr/include/x86_64-linux-gnu/bits/predefs.h
dump.o: /usr/include/x86_64-linux-gnu/sys/cdefs.h
dump.o: /usr/include/x86_64-linux-gnu/bits/wordsize.h
dump.o: /usr/include/x86_64-linux-gnu/gnu/stubs.h
dump.o: /usr/include/x86_64-linux-gnu/gnu/stubs-64.h
dump.o: /usr/include/linux/stddef.h
dump.o: /usr/include/x86_64-linux-gnu/bits/types.h
dump.o: /usr/include/x86_64-linux-gnu/bits/typesizes.h /usr/include/libio.h
dump.o: /usr/include/_G_config.h /usr/include/wchar.h
dump.o: /usr/lib/gcc/x86_64-linux-gnu/4.6/include/stdarg.h
dump.o: /usr/include/x86_64-linux-gnu/bits/stdio_lim.h
dump.o: /usr/include/x86_64-linux-gnu/bits/sys_errlist.h error.h
dump.o: /usr/include/ctype.h /usr/include/endian.h
dump.o: /usr/include/x86_64-linux-gnu/bits/endian.h
dump.o: /usr/include/x86_64-linux-gnu/bits/byteswap.h
dict.o: dict.h status.h /usr/include/linux/stddef.h error.h table.h xalloc.h
dict.o: /usr/include/linux/string.h
error.o: error.h spin.h barrier.h yield.h status.h /usr/include/linux/errno.h
error.o: /usr/include/x86_64-linux-gnu/asm/errno.h
error.o: /usr/include/asm-generic/errno.h
error.o: /usr/include/asm-generic/errno-base.h /usr/include/stdio.h
error.o: /usr/include/features.h /usr/include/x86_64-linux-gnu/bits/predefs.h
error.o: /usr/include/x86_64-linux-gnu/sys/cdefs.h
error.o: /usr/include/x86_64-linux-gnu/bits/wordsize.h
error.o: /usr/include/x86_64-linux-gnu/gnu/stubs.h
error.o: /usr/include/x86_64-linux-gnu/gnu/stubs-64.h
error.o: /usr/include/linux/stddef.h
error.o: /usr/include/x86_64-linux-gnu/bits/types.h
error.o: /usr/include/x86_64-linux-gnu/bits/typesizes.h /usr/include/libio.h
error.o: /usr/include/_G_config.h /usr/include/wchar.h
error.o: /usr/lib/gcc/x86_64-linux-gnu/4.6/include/stdarg.h
error.o: /usr/include/x86_64-linux-gnu/bits/stdio_lim.h
error.o: /usr/include/x86_64-linux-gnu/bits/sys_errlist.h
error.o: /usr/include/stdlib.h /usr/include/x86_64-linux-gnu/sys/types.h
error.o: /usr/include/linux/time.h /usr/include/linux/types.h
error.o: /usr/include/x86_64-linux-gnu/asm/types.h
error.o: /usr/include/asm-generic/types.h /usr/include/asm-generic/int-ll64.h
error.o: /usr/include/x86_64-linux-gnu/asm/bitsperlong.h
error.o: /usr/include/asm-generic/bitsperlong.h
error.o: /usr/include/linux/posix_types.h /usr/include/linux/stddef.h
error.o: /usr/include/x86_64-linux-gnu/asm/posix_types.h
error.o: /usr/include/x86_64-linux-gnu/asm/posix_types_64.h
error.o: /usr/include/endian.h /usr/include/x86_64-linux-gnu/bits/endian.h
error.o: /usr/include/x86_64-linux-gnu/bits/byteswap.h
error.o: /usr/include/x86_64-linux-gnu/sys/select.h
error.o: /usr/include/x86_64-linux-gnu/bits/select.h
error.o: /usr/include/x86_64-linux-gnu/bits/sigset.h
error.o: /usr/include/x86_64-linux-gnu/bits/time.h
error.o: /usr/include/x86_64-linux-gnu/sys/sysmacros.h
error.o: /usr/include/x86_64-linux-gnu/bits/pthreadtypes.h
error.o: /usr/include/alloca.h /usr/include/linux/string.h
poll.o: poll.h status.h sock.h /usr/include/linux/stddef.h error.h xalloc.h
poll.o: /usr/include/linux/string.h /usr/include/linux/errno.h
poll.o: /usr/include/x86_64-linux-gnu/asm/errno.h
poll.o: /usr/include/asm-generic/errno.h
poll.o: /usr/include/asm-generic/errno-base.h
receiver.o: receiver.h storage.h spin.h barrier.h yield.h status.h
receiver.o: /usr/include/linux/stddef.h error.h sock.h thread.h xalloc.h
receiver.o: /usr/include/linux/string.h /usr/include/alloca.h
receiver.o: /usr/include/features.h
receiver.o: /usr/include/x86_64-linux-gnu/bits/predefs.h
receiver.o: /usr/include/x86_64-linux-gnu/sys/cdefs.h
receiver.o: /usr/include/x86_64-linux-gnu/bits/wordsize.h
receiver.o: /usr/include/x86_64-linux-gnu/gnu/stubs.h
receiver.o: /usr/include/x86_64-linux-gnu/gnu/stubs-64.h
receiver.o: /usr/include/linux/errno.h
receiver.o: /usr/include/x86_64-linux-gnu/asm/errno.h
receiver.o: /usr/include/asm-generic/errno.h
receiver.o: /usr/include/asm-generic/errno-base.h
receiver.o: /usr/lib/gcc/x86_64-linux-gnu/4.6/include/float.h
receiver.o: /usr/include/math.h /usr/include/x86_64-linux-gnu/bits/huge_val.h
receiver.o: /usr/include/x86_64-linux-gnu/bits/huge_valf.h
receiver.o: /usr/include/x86_64-linux-gnu/bits/huge_vall.h
receiver.o: /usr/include/x86_64-linux-gnu/bits/inf.h
receiver.o: /usr/include/x86_64-linux-gnu/bits/nan.h
receiver.o: /usr/include/x86_64-linux-gnu/bits/mathdef.h
receiver.o: /usr/include/x86_64-linux-gnu/bits/mathcalls.h poll.h
receiver.o: /usr/include/stdio.h /usr/include/x86_64-linux-gnu/bits/types.h
receiver.o: /usr/include/x86_64-linux-gnu/bits/typesizes.h
receiver.o: /usr/include/libio.h /usr/include/_G_config.h
receiver.o: /usr/include/wchar.h
receiver.o: /usr/lib/gcc/x86_64-linux-gnu/4.6/include/stdarg.h
receiver.o: /usr/include/x86_64-linux-gnu/bits/stdio_lim.h
receiver.o: /usr/include/x86_64-linux-gnu/bits/sys_errlist.h
receiver.o: /usr/include/linux/time.h /usr/include/linux/types.h
receiver.o: /usr/include/x86_64-linux-gnu/asm/types.h
receiver.o: /usr/include/asm-generic/types.h
receiver.o: /usr/include/asm-generic/int-ll64.h
receiver.o: /usr/include/x86_64-linux-gnu/asm/bitsperlong.h
receiver.o: /usr/include/asm-generic/bitsperlong.h
receiver.o: /usr/include/linux/posix_types.h /usr/include/linux/stddef.h
receiver.o: /usr/include/x86_64-linux-gnu/asm/posix_types.h
receiver.o: /usr/include/x86_64-linux-gnu/asm/posix_types_64.h
receiver.o: /usr/include/linux/unistd.h
receiver.o: /usr/include/x86_64-linux-gnu/asm/unistd.h
receiver.o: /usr/include/x86_64-linux-gnu/asm/unistd_64.h
receiver.o: /usr/include/x86_64-linux-gnu/sys/socket.h
receiver.o: /usr/include/x86_64-linux-gnu/sys/uio.h
receiver.o: /usr/include/x86_64-linux-gnu/sys/types.h /usr/include/endian.h
receiver.o: /usr/include/x86_64-linux-gnu/bits/endian.h
receiver.o: /usr/include/x86_64-linux-gnu/bits/byteswap.h
receiver.o: /usr/include/x86_64-linux-gnu/sys/select.h
receiver.o: /usr/include/x86_64-linux-gnu/bits/select.h
receiver.o: /usr/include/x86_64-linux-gnu/bits/sigset.h
receiver.o: /usr/include/x86_64-linux-gnu/bits/time.h
receiver.o: /usr/include/x86_64-linux-gnu/sys/sysmacros.h
receiver.o: /usr/include/x86_64-linux-gnu/bits/pthreadtypes.h
receiver.o: /usr/include/x86_64-linux-gnu/bits/uio.h
receiver.o: /usr/include/x86_64-linux-gnu/bits/socket.h
receiver.o: /usr/include/x86_64-linux-gnu/bits/sockaddr.h
receiver.o: /usr/include/x86_64-linux-gnu/asm/socket.h
receiver.o: /usr/include/asm-generic/socket.h
receiver.o: /usr/include/x86_64-linux-gnu/asm/sockios.h
receiver.o: /usr/include/asm-generic/sockios.h
sender.o: sender.h storage.h spin.h barrier.h yield.h status.h
sender.o: /usr/include/linux/stddef.h accum.h error.h poll.h sock.h thread.h
sender.o: xalloc.h /usr/include/linux/string.h /usr/include/linux/errno.h
sender.o: /usr/include/x86_64-linux-gnu/asm/errno.h
sender.o: /usr/include/asm-generic/errno.h
sender.o: /usr/include/asm-generic/errno-base.h /usr/include/linux/limits.h
sender.o: /usr/include/stdio.h /usr/include/features.h
sender.o: /usr/include/x86_64-linux-gnu/bits/predefs.h
sender.o: /usr/include/x86_64-linux-gnu/sys/cdefs.h
sender.o: /usr/include/x86_64-linux-gnu/bits/wordsize.h
sender.o: /usr/include/x86_64-linux-gnu/gnu/stubs.h
sender.o: /usr/include/x86_64-linux-gnu/gnu/stubs-64.h
sender.o: /usr/include/x86_64-linux-gnu/bits/types.h
sender.o: /usr/include/x86_64-linux-gnu/bits/typesizes.h /usr/include/libio.h
sender.o: /usr/include/_G_config.h /usr/include/wchar.h
sender.o: /usr/lib/gcc/x86_64-linux-gnu/4.6/include/stdarg.h
sender.o: /usr/include/x86_64-linux-gnu/bits/stdio_lim.h
sender.o: /usr/include/x86_64-linux-gnu/bits/sys_errlist.h
sender.o: /usr/include/linux/time.h /usr/include/linux/types.h
sender.o: /usr/include/x86_64-linux-gnu/asm/types.h
sender.o: /usr/include/asm-generic/types.h
sender.o: /usr/include/asm-generic/int-ll64.h
sender.o: /usr/include/x86_64-linux-gnu/asm/bitsperlong.h
sender.o: /usr/include/asm-generic/bitsperlong.h
sender.o: /usr/include/linux/posix_types.h /usr/include/linux/stddef.h
sender.o: /usr/include/x86_64-linux-gnu/asm/posix_types.h
sender.o: /usr/include/x86_64-linux-gnu/asm/posix_types_64.h
sender.o: /usr/include/linux/unistd.h
sender.o: /usr/include/x86_64-linux-gnu/asm/unistd.h
sender.o: /usr/include/x86_64-linux-gnu/asm/unistd_64.h
sender.o: /usr/include/x86_64-linux-gnu/sys/socket.h
sender.o: /usr/include/x86_64-linux-gnu/sys/uio.h
sender.o: /usr/include/x86_64-linux-gnu/sys/types.h /usr/include/endian.h
sender.o: /usr/include/x86_64-linux-gnu/bits/endian.h
sender.o: /usr/include/x86_64-linux-gnu/bits/byteswap.h
sender.o: /usr/include/x86_64-linux-gnu/sys/select.h
sender.o: /usr/include/x86_64-linux-gnu/bits/select.h
sender.o: /usr/include/x86_64-linux-gnu/bits/sigset.h
sender.o: /usr/include/x86_64-linux-gnu/bits/time.h
sender.o: /usr/include/x86_64-linux-gnu/sys/sysmacros.h
sender.o: /usr/include/x86_64-linux-gnu/bits/pthreadtypes.h
sender.o: /usr/include/x86_64-linux-gnu/bits/uio.h
sender.o: /usr/include/x86_64-linux-gnu/bits/socket.h
sender.o: /usr/include/x86_64-linux-gnu/bits/sockaddr.h
sender.o: /usr/include/x86_64-linux-gnu/asm/socket.h
sender.o: /usr/include/asm-generic/socket.h
sender.o: /usr/include/x86_64-linux-gnu/asm/sockios.h
sender.o: /usr/include/asm-generic/sockios.h
signals.o: signals.h status.h /usr/include/linux/signal.h
signals.o: /usr/include/x86_64-linux-gnu/asm/signal.h
signals.o: /usr/include/linux/types.h
signals.o: /usr/include/x86_64-linux-gnu/asm/types.h
signals.o: /usr/include/asm-generic/types.h
signals.o: /usr/include/asm-generic/int-ll64.h
signals.o: /usr/include/x86_64-linux-gnu/asm/bitsperlong.h
signals.o: /usr/include/asm-generic/bitsperlong.h
signals.o: /usr/include/linux/posix_types.h /usr/include/linux/stddef.h
signals.o: /usr/include/x86_64-linux-gnu/asm/posix_types.h
signals.o: /usr/include/x86_64-linux-gnu/asm/posix_types_64.h
signals.o: /usr/include/linux/time.h /usr/include/asm-generic/signal-defs.h
signals.o: /usr/include/x86_64-linux-gnu/asm/siginfo.h
signals.o: /usr/include/asm-generic/siginfo.h error.h
signals.o: /usr/include/linux/stddef.h
sock.o: sock.h status.h /usr/include/linux/stddef.h error.h xalloc.h
sock.o: /usr/include/linux/string.h /usr/include/linux/errno.h
sock.o: /usr/include/x86_64-linux-gnu/asm/errno.h
sock.o: /usr/include/asm-generic/errno.h
sock.o: /usr/include/asm-generic/errno-base.h /usr/include/linux/fcntl.h
sock.o: /usr/include/x86_64-linux-gnu/asm/fcntl.h
sock.o: /usr/include/asm-generic/fcntl.h /usr/include/linux/types.h
sock.o: /usr/include/x86_64-linux-gnu/asm/types.h
sock.o: /usr/include/asm-generic/types.h /usr/include/asm-generic/int-ll64.h
sock.o: /usr/include/x86_64-linux-gnu/asm/bitsperlong.h
sock.o: /usr/include/asm-generic/bitsperlong.h
sock.o: /usr/include/linux/posix_types.h /usr/include/linux/stddef.h
sock.o: /usr/include/x86_64-linux-gnu/asm/posix_types.h
sock.o: /usr/include/x86_64-linux-gnu/asm/posix_types_64.h
sock.o: /usr/include/stdio.h /usr/include/features.h
sock.o: /usr/include/x86_64-linux-gnu/bits/predefs.h
sock.o: /usr/include/x86_64-linux-gnu/sys/cdefs.h
sock.o: /usr/include/x86_64-linux-gnu/bits/wordsize.h
sock.o: /usr/include/x86_64-linux-gnu/gnu/stubs.h
sock.o: /usr/include/x86_64-linux-gnu/gnu/stubs-64.h
sock.o: /usr/include/x86_64-linux-gnu/bits/types.h
sock.o: /usr/include/x86_64-linux-gnu/bits/typesizes.h /usr/include/libio.h
sock.o: /usr/include/_G_config.h /usr/include/wchar.h
sock.o: /usr/lib/gcc/x86_64-linux-gnu/4.6/include/stdarg.h
sock.o: /usr/include/x86_64-linux-gnu/bits/stdio_lim.h
sock.o: /usr/include/x86_64-linux-gnu/bits/sys_errlist.h
sock.o: /usr/include/linux/unistd.h
sock.o: /usr/include/x86_64-linux-gnu/asm/unistd.h
sock.o: /usr/include/x86_64-linux-gnu/asm/unistd_64.h
sock.o: /usr/include/arpa/inet.h /usr/include/netinet/in.h
sock.o: /usr/lib/gcc/x86_64-linux-gnu/4.6/include/stdint.h
sock.o: /usr/lib/gcc/x86_64-linux-gnu/4.6/include/stdint-gcc.h
sock.o: /usr/include/x86_64-linux-gnu/sys/socket.h
sock.o: /usr/include/x86_64-linux-gnu/sys/uio.h
sock.o: /usr/include/x86_64-linux-gnu/sys/types.h /usr/include/linux/time.h
sock.o: /usr/include/endian.h /usr/include/x86_64-linux-gnu/bits/endian.h
sock.o: /usr/include/x86_64-linux-gnu/bits/byteswap.h
sock.o: /usr/include/x86_64-linux-gnu/sys/select.h
sock.o: /usr/include/x86_64-linux-gnu/bits/select.h
sock.o: /usr/include/x86_64-linux-gnu/bits/sigset.h
sock.o: /usr/include/x86_64-linux-gnu/bits/time.h
sock.o: /usr/include/x86_64-linux-gnu/sys/sysmacros.h
sock.o: /usr/include/x86_64-linux-gnu/bits/pthreadtypes.h
sock.o: /usr/include/x86_64-linux-gnu/bits/uio.h
sock.o: /usr/include/x86_64-linux-gnu/bits/socket.h
sock.o: /usr/include/x86_64-linux-gnu/bits/sockaddr.h
sock.o: /usr/include/x86_64-linux-gnu/asm/socket.h
sock.o: /usr/include/asm-generic/socket.h
sock.o: /usr/include/x86_64-linux-gnu/asm/sockios.h
sock.o: /usr/include/asm-generic/sockios.h
sock.o: /usr/include/x86_64-linux-gnu/bits/in.h /usr/include/net/if.h
sock.o: /usr/include/x86_64-linux-gnu/sys/ioctl.h
sock.o: /usr/include/x86_64-linux-gnu/bits/ioctls.h
sock.o: /usr/include/x86_64-linux-gnu/asm/ioctls.h
sock.o: /usr/include/asm-generic/ioctls.h /usr/include/linux/ioctl.h
sock.o: /usr/include/x86_64-linux-gnu/asm/ioctl.h
sock.o: /usr/include/asm-generic/ioctl.h
sock.o: /usr/include/x86_64-linux-gnu/bits/ioctl-types.h
sock.o: /usr/include/x86_64-linux-gnu/sys/ttydefaults.h
storage.o: storage.h spin.h barrier.h yield.h status.h
storage.o: /usr/include/linux/stddef.h error.h xalloc.h
storage.o: /usr/include/linux/string.h /usr/include/linux/errno.h
storage.o: /usr/include/x86_64-linux-gnu/asm/errno.h
storage.o: /usr/include/asm-generic/errno.h
storage.o: /usr/include/asm-generic/errno-base.h /usr/include/linux/fcntl.h
storage.o: /usr/include/x86_64-linux-gnu/asm/fcntl.h
storage.o: /usr/include/asm-generic/fcntl.h /usr/include/linux/types.h
storage.o: /usr/include/x86_64-linux-gnu/asm/types.h
storage.o: /usr/include/asm-generic/types.h
storage.o: /usr/include/asm-generic/int-ll64.h
storage.o: /usr/include/x86_64-linux-gnu/asm/bitsperlong.h
storage.o: /usr/include/asm-generic/bitsperlong.h
storage.o: /usr/include/linux/posix_types.h /usr/include/linux/stddef.h
storage.o: /usr/include/x86_64-linux-gnu/asm/posix_types.h
storage.o: /usr/include/x86_64-linux-gnu/asm/posix_types_64.h
storage.o: /usr/include/linux/unistd.h
storage.o: /usr/include/x86_64-linux-gnu/asm/unistd.h
storage.o: /usr/include/x86_64-linux-gnu/asm/unistd_64.h
storage.o: /usr/include/x86_64-linux-gnu/sys/mman.h /usr/include/features.h
storage.o: /usr/include/x86_64-linux-gnu/bits/predefs.h
storage.o: /usr/include/x86_64-linux-gnu/sys/cdefs.h
storage.o: /usr/include/x86_64-linux-gnu/bits/wordsize.h
storage.o: /usr/include/x86_64-linux-gnu/gnu/stubs.h
storage.o: /usr/include/x86_64-linux-gnu/gnu/stubs-64.h
storage.o: /usr/include/x86_64-linux-gnu/bits/types.h
storage.o: /usr/include/x86_64-linux-gnu/bits/typesizes.h
storage.o: /usr/include/x86_64-linux-gnu/bits/mman.h
storage.o: /usr/include/x86_64-linux-gnu/sys/types.h
storage.o: /usr/include/linux/time.h /usr/include/endian.h
storage.o: /usr/include/x86_64-linux-gnu/bits/endian.h
storage.o: /usr/include/x86_64-linux-gnu/bits/byteswap.h
storage.o: /usr/include/x86_64-linux-gnu/sys/select.h
storage.o: /usr/include/x86_64-linux-gnu/bits/select.h
storage.o: /usr/include/x86_64-linux-gnu/bits/sigset.h
storage.o: /usr/include/x86_64-linux-gnu/bits/time.h
storage.o: /usr/include/x86_64-linux-gnu/sys/sysmacros.h
storage.o: /usr/include/x86_64-linux-gnu/bits/pthreadtypes.h
storage.o: /usr/include/x86_64-linux-gnu/sys/stat.h
storage.o: /usr/include/x86_64-linux-gnu/bits/stat.h
table.o: table.h status.h /usr/include/linux/stddef.h error.h xalloc.h
table.o: /usr/include/linux/string.h
thread.o: thread.h status.h error.h xalloc.h /usr/include/linux/stddef.h
thread.o: /usr/include/linux/string.h /usr/include/linux/errno.h
thread.o: /usr/include/x86_64-linux-gnu/asm/errno.h
thread.o: /usr/include/asm-generic/errno.h
thread.o: /usr/include/asm-generic/errno-base.h /usr/include/pthread.h
thread.o: /usr/include/features.h
thread.o: /usr/include/x86_64-linux-gnu/bits/predefs.h
thread.o: /usr/include/x86_64-linux-gnu/sys/cdefs.h
thread.o: /usr/include/x86_64-linux-gnu/bits/wordsize.h
thread.o: /usr/include/x86_64-linux-gnu/gnu/stubs.h
thread.o: /usr/include/x86_64-linux-gnu/gnu/stubs-64.h /usr/include/endian.h
thread.o: /usr/include/x86_64-linux-gnu/bits/endian.h
thread.o: /usr/include/x86_64-linux-gnu/bits/byteswap.h
thread.o: /usr/include/linux/sched.h /usr/include/linux/time.h
thread.o: /usr/include/linux/types.h
thread.o: /usr/include/x86_64-linux-gnu/asm/types.h
thread.o: /usr/include/asm-generic/types.h
thread.o: /usr/include/asm-generic/int-ll64.h
thread.o: /usr/include/x86_64-linux-gnu/asm/bitsperlong.h
thread.o: /usr/include/asm-generic/bitsperlong.h
thread.o: /usr/include/linux/posix_types.h /usr/include/linux/stddef.h
thread.o: /usr/include/x86_64-linux-gnu/asm/posix_types.h
thread.o: /usr/include/x86_64-linux-gnu/asm/posix_types_64.h
thread.o: /usr/include/x86_64-linux-gnu/bits/pthreadtypes.h
thread.o: /usr/include/x86_64-linux-gnu/bits/setjmp.h
twist.o: twist.h status.h error.h xalloc.h /usr/include/linux/stddef.h
twist.o: /usr/include/linux/string.h
uudict.o: uudict.h status.h /usr/include/linux/stddef.h error.h table.h
uudict.o: xalloc.h /usr/include/linux/string.h
xalloc.o: xalloc.h /usr/include/linux/stddef.h /usr/include/linux/string.h
xalloc.o: error.h /usr/include/linux/errno.h
xalloc.o: /usr/include/x86_64-linux-gnu/asm/errno.h
xalloc.o: /usr/include/asm-generic/errno.h
xalloc.o: /usr/include/asm-generic/errno-base.h /usr/include/stdlib.h
xalloc.o: /usr/include/features.h
xalloc.o: /usr/include/x86_64-linux-gnu/bits/predefs.h
xalloc.o: /usr/include/x86_64-linux-gnu/sys/cdefs.h
xalloc.o: /usr/include/x86_64-linux-gnu/bits/wordsize.h
xalloc.o: /usr/include/x86_64-linux-gnu/gnu/stubs.h
xalloc.o: /usr/include/x86_64-linux-gnu/gnu/stubs-64.h
xalloc.o: /usr/include/x86_64-linux-gnu/sys/types.h
xalloc.o: /usr/include/x86_64-linux-gnu/bits/types.h
xalloc.o: /usr/include/x86_64-linux-gnu/bits/typesizes.h
xalloc.o: /usr/include/linux/time.h /usr/include/linux/types.h
xalloc.o: /usr/include/x86_64-linux-gnu/asm/types.h
xalloc.o: /usr/include/asm-generic/types.h
xalloc.o: /usr/include/asm-generic/int-ll64.h
xalloc.o: /usr/include/x86_64-linux-gnu/asm/bitsperlong.h
xalloc.o: /usr/include/asm-generic/bitsperlong.h
xalloc.o: /usr/include/linux/posix_types.h /usr/include/linux/stddef.h
xalloc.o: /usr/include/x86_64-linux-gnu/asm/posix_types.h
xalloc.o: /usr/include/x86_64-linux-gnu/asm/posix_types_64.h
xalloc.o: /usr/include/endian.h /usr/include/x86_64-linux-gnu/bits/endian.h
xalloc.o: /usr/include/x86_64-linux-gnu/bits/byteswap.h
xalloc.o: /usr/include/x86_64-linux-gnu/sys/select.h
xalloc.o: /usr/include/x86_64-linux-gnu/bits/select.h
xalloc.o: /usr/include/x86_64-linux-gnu/bits/sigset.h
xalloc.o: /usr/include/x86_64-linux-gnu/bits/time.h
xalloc.o: /usr/include/x86_64-linux-gnu/sys/sysmacros.h
xalloc.o: /usr/include/x86_64-linux-gnu/bits/pthreadtypes.h
xalloc.o: /usr/include/alloca.h
yield.o: yield.h /usr/include/linux/sched.h /usr/include/linux/unistd.h
yield.o: /usr/include/x86_64-linux-gnu/asm/unistd.h
yield.o: /usr/include/x86_64-linux-gnu/asm/unistd_64.h
publisher.o: datum.h error.h sender.h storage.h spin.h barrier.h yield.h
publisher.o: status.h /usr/include/linux/stddef.h signals.h
publisher.o: /usr/include/linux/signal.h
publisher.o: /usr/include/x86_64-linux-gnu/asm/signal.h
publisher.o: /usr/include/linux/types.h
publisher.o: /usr/include/x86_64-linux-gnu/asm/types.h
publisher.o: /usr/include/asm-generic/types.h
publisher.o: /usr/include/asm-generic/int-ll64.h
publisher.o: /usr/include/x86_64-linux-gnu/asm/bitsperlong.h
publisher.o: /usr/include/asm-generic/bitsperlong.h
publisher.o: /usr/include/linux/posix_types.h /usr/include/linux/stddef.h
publisher.o: /usr/include/x86_64-linux-gnu/asm/posix_types.h
publisher.o: /usr/include/x86_64-linux-gnu/asm/posix_types_64.h
publisher.o: /usr/include/linux/time.h /usr/include/asm-generic/signal-defs.h
publisher.o: /usr/include/x86_64-linux-gnu/asm/siginfo.h
publisher.o: /usr/include/asm-generic/siginfo.h /usr/include/stdio.h
publisher.o: /usr/include/features.h
publisher.o: /usr/include/x86_64-linux-gnu/bits/predefs.h
publisher.o: /usr/include/x86_64-linux-gnu/sys/cdefs.h
publisher.o: /usr/include/x86_64-linux-gnu/bits/wordsize.h
publisher.o: /usr/include/x86_64-linux-gnu/gnu/stubs.h
publisher.o: /usr/include/x86_64-linux-gnu/gnu/stubs-64.h
publisher.o: /usr/include/x86_64-linux-gnu/bits/types.h
publisher.o: /usr/include/x86_64-linux-gnu/bits/typesizes.h
publisher.o: /usr/include/libio.h /usr/include/_G_config.h
publisher.o: /usr/include/wchar.h
publisher.o: /usr/lib/gcc/x86_64-linux-gnu/4.6/include/stdarg.h
publisher.o: /usr/include/x86_64-linux-gnu/bits/stdio_lim.h
publisher.o: /usr/include/x86_64-linux-gnu/bits/sys_errlist.h
publisher.o: /usr/include/stdlib.h /usr/include/x86_64-linux-gnu/sys/types.h
publisher.o: /usr/include/linux/time.h /usr/include/endian.h
publisher.o: /usr/include/x86_64-linux-gnu/bits/endian.h
publisher.o: /usr/include/x86_64-linux-gnu/bits/byteswap.h
publisher.o: /usr/include/x86_64-linux-gnu/sys/select.h
publisher.o: /usr/include/x86_64-linux-gnu/bits/select.h
publisher.o: /usr/include/x86_64-linux-gnu/bits/sigset.h
publisher.o: /usr/include/x86_64-linux-gnu/bits/time.h
publisher.o: /usr/include/x86_64-linux-gnu/sys/sysmacros.h
publisher.o: /usr/include/x86_64-linux-gnu/bits/pthreadtypes.h
publisher.o: /usr/include/alloca.h /usr/include/linux/string.h
subscriber.o: error.h receiver.h storage.h spin.h barrier.h yield.h status.h
subscriber.o: /usr/include/linux/stddef.h signals.h
subscriber.o: /usr/include/linux/signal.h
subscriber.o: /usr/include/x86_64-linux-gnu/asm/signal.h
subscriber.o: /usr/include/linux/types.h
subscriber.o: /usr/include/x86_64-linux-gnu/asm/types.h
subscriber.o: /usr/include/asm-generic/types.h
subscriber.o: /usr/include/asm-generic/int-ll64.h
subscriber.o: /usr/include/x86_64-linux-gnu/asm/bitsperlong.h
subscriber.o: /usr/include/asm-generic/bitsperlong.h
subscriber.o: /usr/include/linux/posix_types.h /usr/include/linux/stddef.h
subscriber.o: /usr/include/x86_64-linux-gnu/asm/posix_types.h
subscriber.o: /usr/include/x86_64-linux-gnu/asm/posix_types_64.h
subscriber.o: /usr/include/linux/time.h
subscriber.o: /usr/include/asm-generic/signal-defs.h
subscriber.o: /usr/include/x86_64-linux-gnu/asm/siginfo.h
subscriber.o: /usr/include/asm-generic/siginfo.h /usr/include/stdio.h
subscriber.o: /usr/include/features.h
subscriber.o: /usr/include/x86_64-linux-gnu/bits/predefs.h
subscriber.o: /usr/include/x86_64-linux-gnu/sys/cdefs.h
subscriber.o: /usr/include/x86_64-linux-gnu/bits/wordsize.h
subscriber.o: /usr/include/x86_64-linux-gnu/gnu/stubs.h
subscriber.o: /usr/include/x86_64-linux-gnu/gnu/stubs-64.h
subscriber.o: /usr/include/x86_64-linux-gnu/bits/types.h
subscriber.o: /usr/include/x86_64-linux-gnu/bits/typesizes.h
subscriber.o: /usr/include/libio.h /usr/include/_G_config.h
subscriber.o: /usr/include/wchar.h
subscriber.o: /usr/lib/gcc/x86_64-linux-gnu/4.6/include/stdarg.h
subscriber.o: /usr/include/x86_64-linux-gnu/bits/stdio_lim.h
subscriber.o: /usr/include/x86_64-linux-gnu/bits/sys_errlist.h
subscriber.o: /usr/include/stdlib.h /usr/include/x86_64-linux-gnu/sys/types.h
subscriber.o: /usr/include/linux/time.h /usr/include/endian.h
subscriber.o: /usr/include/x86_64-linux-gnu/bits/endian.h
subscriber.o: /usr/include/x86_64-linux-gnu/bits/byteswap.h
subscriber.o: /usr/include/x86_64-linux-gnu/sys/select.h
subscriber.o: /usr/include/x86_64-linux-gnu/bits/select.h
subscriber.o: /usr/include/x86_64-linux-gnu/bits/sigset.h
subscriber.o: /usr/include/x86_64-linux-gnu/bits/time.h
subscriber.o: /usr/include/x86_64-linux-gnu/sys/sysmacros.h
subscriber.o: /usr/include/x86_64-linux-gnu/bits/pthreadtypes.h
subscriber.o: /usr/include/alloca.h /usr/include/linux/string.h
reader.o: datum.h error.h signals.h status.h /usr/include/linux/signal.h
reader.o: /usr/include/x86_64-linux-gnu/asm/signal.h
reader.o: /usr/include/linux/types.h
reader.o: /usr/include/x86_64-linux-gnu/asm/types.h
reader.o: /usr/include/asm-generic/types.h
reader.o: /usr/include/asm-generic/int-ll64.h
reader.o: /usr/include/x86_64-linux-gnu/asm/bitsperlong.h
reader.o: /usr/include/asm-generic/bitsperlong.h
reader.o: /usr/include/linux/posix_types.h /usr/include/linux/stddef.h
reader.o: /usr/include/x86_64-linux-gnu/asm/posix_types.h
reader.o: /usr/include/x86_64-linux-gnu/asm/posix_types_64.h
reader.o: /usr/include/linux/time.h /usr/include/asm-generic/signal-defs.h
reader.o: /usr/include/x86_64-linux-gnu/asm/siginfo.h
reader.o: /usr/include/asm-generic/siginfo.h storage.h spin.h barrier.h
reader.o: yield.h /usr/include/linux/stddef.h /usr/include/stdio.h
reader.o: /usr/include/features.h
reader.o: /usr/include/x86_64-linux-gnu/bits/predefs.h
reader.o: /usr/include/x86_64-linux-gnu/sys/cdefs.h
reader.o: /usr/include/x86_64-linux-gnu/bits/wordsize.h
reader.o: /usr/include/x86_64-linux-gnu/gnu/stubs.h
reader.o: /usr/include/x86_64-linux-gnu/gnu/stubs-64.h
reader.o: /usr/include/x86_64-linux-gnu/bits/types.h
reader.o: /usr/include/x86_64-linux-gnu/bits/typesizes.h /usr/include/libio.h
reader.o: /usr/include/_G_config.h /usr/include/wchar.h
reader.o: /usr/lib/gcc/x86_64-linux-gnu/4.6/include/stdarg.h
reader.o: /usr/include/x86_64-linux-gnu/bits/stdio_lim.h
reader.o: /usr/include/x86_64-linux-gnu/bits/sys_errlist.h
