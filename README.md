Cachester - a C library for high performance, reliable multicasting of data
===========================================================================

Syntax of the three test programs:-

	   publisher [numeric speed] [mcast address] [mcast port] [tcp address] [tcp port]
	   subscriber [tcp address] [tcp port]
	   listener [no arguments]

For example, if you were to run the publisher on pslchi6dpricedev45 (10.2.2.152), port 23266:-

jflude@pslchi6dpricedev45:~$ publisher 16 227.1.1.34 56134 10.2.2.152 23266
jflude@pslchi6ddev2:~$ subscriber 10.2.2.152 23266
jflude@pslchi6ddev2:~$ listener

Each of these commands outputs to stderr some diagnostic information, so they should be run in separate terminals.

"Numeric speed" is an integer in the range 0-16 - the publisher will pause for a microsecond every (2^speed) items changed.
The multicast address in the publisher command above is safe for testing.

Any questions, bug reports, suggested improvements etc. - please contact Justin Flude <jflude@peak6.com>
