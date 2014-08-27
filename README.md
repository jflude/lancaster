CACHESTER - a C library for high performance, reliable multicasting of data
===========================================================================

Syntax of the four test programs:-

	   writer [delay] [change queue size] [storage file or segment]

	   publisher [-v|--verbose] [heartbeat interval] [maximum packet age] \
	   			 [multicast address] [multicast port] [TCP address] [TCP port] [storage file or segment]

	   subscriber [-v|--verbose] [TCP address] [TCP port] [change queue size] [storage file or segment]

	   reader [storage file or segment]

For example, you might run the writer and publisher commands on pslchi6dpricedev45 (10.2.2.152), port 23266:-

jflude@pslchi6dpricedev45:~$ writer 4096 /tmp/your_file
jflude@pslchi6dpricedev45:~$ publisher -v 500000 10000 227.1.1.34 56134 10.2.2.152 23266 /tmp/your_file

...and run the subscriber and reader commands on pslchi6dpricedev42:-

jflude@pslchi6dpricedev42:~$ subscriber -v 10.2.2.152 23266 4096 /tmp/my_file
jflude@pslchi6dpricedev42:~$ reader /tmp/my_file

Each of these commands can output some diagnostic information, so they should be run in separate terminals.
Note also that it is possible to run the writer and reader "offline", ie. on the same machine without a
publisher-subscriber networking intermediary.

"Delay" is the number of microseconds the writer should pause after updating a datum.  It can be zero, to
write at maximum speed.  "Heartbeat interval" is the number of microseconds between multicast and TCP heartbeats
sent by the publisher to subscribers.  "Change queue size" must be a power of 2.

The multicast address in the publisher command above is safe for testing.

The publisher and subscriber programs are generic - they will work for any kind of data within a storage file/segment.

A POSIX shared memory segment can be used instead of a memory-mapped file - specify the name of the memory segment
with a prefix of shm:, eg. "shm:/my_segment".

Any questions, bug reports, suggested improvements etc. - please contact Justin Flude <jflude@peak6.com>
