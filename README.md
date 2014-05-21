cachester
=========

A C library for high performance, reliable multicasting of data.

Test program arguments:-

server [numeric speed] [mcast address] [mcast port] [tcp address] [tcp port]
client [tcp address] [tcp port]

For example, if you were to run the server on pslchi6dpricedev45 (10.2.2.152), port 23266:-

jflude@pslchi6dpricedev45:~$ server 7 227.1.1.34 56134 10.2.2.152 23266
jflude@pslchi6ddev2:~$ client 10.2.2.152 23266

"Numeric speed" is an integer in the range 0-10 - the server will pause for a microsecond every 2^speed items changed.
The multicast addresses in the server command above are safe for testing.
