CACHESTER - a C library for high performance, reliable multicasting of data
===========================================================================

Syntax of the three test programs:-

	   publisher [-v|--verbose] [delay] [heartbeat interval] [mcast address] [mcast port] [tcp address] [tcp port]
	   subscriber [-v|--verbose] [tcp address] [tcp port] [change queue capacity] [storage file or segment]
	   reader [storage file or segment]

For example, you might run the publisher command on pslchi6dpricedev45 (10.2.2.152), port 23266:-

jflude@pslchi6dpricedev45:~$ publisher -v 16 227.1.1.34 56134 10.2.2.152 23266

...and run the subscriber and reader commands on pslchi6dpricedev42:-

jflude@pslchi6dpricedev42:~$ subscriber -v 10.2.2.152 23266 4096 /tmp/my_file
jflude@pslchi6dpricedev42:~$ reader /tmp/my_file

Each of these commands can output some diagnostic information, so they should be run in separate terminals.

"Delay" is the number of microseconds the publisher should pause after multicasting an updated datum.  It can
be zero, to publish at maximum speed.  "Heartbeat interval" is the number of microseconds between multicast and
TCP heartbeats sent by the publisher to subscribers.

The multicast address in the publisher command above is safe for testing.

The subscriber process is generic - it will work for any kind of data sent from a publisher, without recompilation.

POSIX shared memory can be used instead of a memory-mapped file - specify the name of the memory segment with a
prefix of shm:, eg. "shm:/my_segment".

Any questions, bug reports, suggested improvements etc. - please contact Justin Flude <jflude@peak6.com>
