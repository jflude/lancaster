Cachester - a C library for high performance, reliable multicasting of data
===========================================================================

Syntax of the three test programs:-

	   publisher [-v|--verbose] [speed] [mcast address] [mcast port] [tcp address] [tcp port]
	   subscriber [-v|--verbose] [tcp address] [tcp port] [change queue capacity] [storage file]
	   listener [storage file]

For example, if you were to run the publisher on pslchi6dpricedev45 (10.2.2.152), port 23266:-

jflude@pslchi6dpricedev45:~$ publisher -v 16 227.1.1.34 56134 10.2.2.152 23266
jflude@pslchi6ddev2:~$ subscriber -v 10.2.2.152 23266 4096 /tmp/cachester.store
jflude@pslchi6ddev2:~$ listener /tmp/cachester.store

Each of these commands can output some diagnostic information, so they should be run in separate terminals.

"Speed" is an integer in the range 0-16 - the publisher will pause for a microsecond every (2^speed) items changed.
The multicast address in the publisher command above is safe for testing.

The subscriber process is generic - it will work for any kind of data sent from a publisher, without recompilation.

Any questions, bug reports, suggested improvements etc. - please contact Justin Flude <jflude@peak6.com>
