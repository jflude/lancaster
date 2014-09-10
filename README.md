CACHESTER - a C library for high performance, reliable multicasting of ephemeral data
=====================================================================================

Syntax of the four test programs:-

	writer STORAGE-FILE-OR-SEGMENT CHANGE-QUEUE-SIZE DELAY

	publisher [-e] [-a ADDRESS:PORT] [-i DEVICE] [-l] [-t TTL] STORAGE-FILE-OR-SEGMENT TCP-ADDRESS:PORT \
			  MULTICAST-ADDRESS:PORT HEARTBEAT-INTERVAL MAXIMUM-PACKET-AGE

	subscriber [-e] STORAGE-FILE-OR-SEGMENT CHANGE-QUEUE-SIZE TCP-ADDRESS:PORT

	reader STORAGE-FILE-OR-SEGMENT

For example, you might run the WRITER and PUBLISHER commands on pslchi6dpricedev45 (10.2.2.152), port 23266:-

	jflude@pslchi6dpricedev45:~$ writer /tmp/his_local_file 4096
	jflude@pslchi6dpricedev45:~$ publisher /tmp/his_local_file 10.2.2.152:23266 227.1.1.34:56134 500000 10000

...and run the SUBSCRIBER and READER commands on pslchi6dpricedev42:-

	jflude@pslchi6dpricedev42:~$ subscriber /tmp/her_local_file 4096 10.2.2.152:23266
	jflude@pslchi6dpricedev42:~$ reader /tmp/her_local_file

"Delay" is the number of microseconds the WRITER should pause after updating a datum.  It can be zero, to
write at maximum speed.  "Heartbeat interval" is the number of microseconds between multicast and TCP heartbeats
sent by the PUBLISHER to SUBSCRIBERs.  "Change queue size" must be a power of 2.

The multicast address in the PUBLISHER command above is safe for testing.

The PUBLISHER and SUBSCRIBER programs are generic - they will work for any kind of data within a storage file/segment.
To advertize the presence of the PUBLISHER on the network, use the -a option.  The multicast interface to publish on
can be specified by the -i option.  Multicast traffic will "loopback" (be delivered on the sending host) if the -l
option is specified.

A POSIX shared memory segment can be used instead of a memory-mapped file - specify the name of the memory segment
with a prefix of shm:, eg. "shm:/my_local_segment".

All of these commands except WRITER will output some diagnostic information, so they should be run in separate terminals.
Alternatively, their standard output can be suppressed by redirecting to /dev/null.  Note also that it possible to run
the WRITER and READER "offline", ie. on the same machine without a PUBLISHER-SUBSCRIBER networking intermediary.

The -e option causes the PUBLISHER and SUBSCRIBER to output "embedded" statistics information, in JSON format.

	jflude@pslchi6dpricedev45:~$ writer /tmp/my_local_file 4096
	jflude@pslchi6dpricedev45:~$ reader /tmp/my_local_file

Any questions, bug reports, suggested improvements etc. - please contact Justin Flude <jflude@peak6.com>
