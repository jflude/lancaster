CACHESTER - fast, reliable multicasting of ephemeral data
=========================================================

The software consists of a C library, libcachester, provided in both static and
dynamic forms, together with six utility programs for use in production and
development environments.

Any questions, bug reports, suggested improvements etc. - please contact
Justin Flude <jflude@peak6.com>

	  		   ===============================================

	writer [-v] [-p ERROR PREFIX] STORAGE-FILE CHANGE-QUEUE-SIZE DELAY

	reader [-v] [-p ERROR PREFIX] [-Q] STORAGE-FILE

These are test programs which write and read data to/from a "storage" and check
whether what is read is what was written, in the correct order.

Storages are arrays of records, each having an identifier, contained within a
regular file on disk, such as "/tmp/my_file", or a POSIX shared memory segment,
which is specified with a URI-like prefix of shm, eg. "shm:/my_segment".
Storages can persist across runs (although the storages created by the test
programs do not).

A "change queue" is an optional section of a storage used as a circular buffer
containing the identifiers of records recently modified.  The size of a change
queue must be either zero, or a non-zero power of two.

WRITER will create a storage with a change queue of the given size, then update
slots with ascending values at a speed determined by DELAY (the number of
microseconds to pause after each write, which can be zero).

READER outputs a hexadecimal digit every fifth of a second to indicate the
integrity of the read data - its value is the bitwise OR-ing of the following
numbers, indicating which conditions occured in this last "tick":-

	0 - no data was read
	1 - data was read
	2 - data was read out-of-order to what was written
	4 - change queue was overrun

If the -Q option is supplied to READER then it will output change queue
statistics instead of its usual output.

The -p option causes the programs to include the specified prefix in error
messages, to allow easier identification when running multiple instances.

	  		   ===============================================

	publisher [-v] [-j|-Q] [-a ADDRESS:PORT] [-i DEVICE] [-l] [-t TTL] \
			  STORAGE-FILE TCP-ADDRESS:PORT MULTICAST-ADDRESS:PORT \
			  HEARTBEAT-PERIOD MAXIMUM-PACKET-AGE

	subscriber [-v] [-j] STORAGE-FILE CHANGE-QUEUE-SIZE TCP-ADDRESS:PORT

These are production-ready, generic programs to establish a multicast transport
between a process wanting to publish data and one or more processes on multiple
hosts wanting to receive it.  PUBLISHER looks for a storage to read from (such
as one created by WRITER).  It will listen on TCP-ADDRESS:PORT for connections
from subscribers.  When one is made, PUBLISHER will send the subscriber the
MULTICAST-ADDRESS:PORT to receive data, and the HEARTBEAT-PERIOD microseconds it
should expect to receive heartbeats in the absence of data (PUBLISHER will send
separate heartbeats over both the TCP and multicast channels).  PUBLISHER will
attempt to fill a UDP packet with data before sending it, but will send a
partial packet if the data in it is more than MAX-PACKET-AGE microseconds old.
PUBLISHER will send multicast data over the DEVICE interface rather than the
system's default, if the -i option is specified.  Multicast data will be sent
with a TTL other than 1 if the -t option is specified.  Multicast data will
"loopback" (be delivered also on the sending host) if the -l option is
specified, which enables testing on a single host.  PUBLISHER will "advertize"
its existence by multicasting its connection and storage details every second,
if the -a option is specified.

SUBSCRIBER will try to connect to a PUBLISHER at TCP-ADDRESS:PORT, and based on 
the attributes that PUBLISHER sends it, create a storage similar in structure
to PUBLISHER's (except for the change queue size, which is specified for 
SUBSCRIBER independently).  Data read by PUBLISHER is multicast to SUBSCRIBER
and written to SUBSCRIBER's storage.

Both PUBLISHER and SUBSCRIBER have an -j option which causes their normal 
output of statistics to be output in JSON format.  PUBLISHER also has a -Q
option which causes it to output change queue statistics instead of its
usual output (the -j option also includes the change queue statistics).

A typical scenario for testing would be to run WRITER and PUBLISHER on one host,
and SUBSCRIBER and READER on another.  For example, if the former were to be
run on pslchi6dpricedev45 (10.2.2.152):-

	dev45$ writer /tmp/his_local_file 4096
	dev45$ publisher /tmp/his_local_file 10.2.2.152:23266 227.1.1.34:56134 \
		   			 500000 10000

...and the SUBSCRIBER and READER commands on pslchi6dpricedev42:-

	dev42$ subscriber shm:/her_local_segment 4096 10.2.2.152:23266
	dev42$ reader shm:/her_local_segment

	  		   ===============================================

	inspector [-v] [-a] [-p] [-q] [-V] STORAGE-FILE [RECORD ID...]

	grower [-v] STORAGE-FILE NEW-STORAGE-FILE NEW-BASE-ID NEW-MAX-ID \
		   NEW-VALUE-SIZE NEW-PROPERTY-SIZE NEW-QUEUE-CAPACITY

These are utility programs to show and modify a storage.

INSPECTOR if given the -a option outputs the attribute values of a storage, such
as the size of a record within it, the size of its change queue, the description
associated with it, etc.  The -q option will cause the change queue, if any, to
be output, while the -V option will cause the values of records to be output.
Properties for records will be included in the output if the -p option is
specified.  Individual records can be chosen for output by specifying their
record ID, or every record can be selected by specifying "all".  If no option is
specified, the program will attempt to open and verify the storage, exiting with
zero (success) if the format of the storage is valid.

The GROWER program will create a new storage based upon an existing storage, and
containing the same data copied to its records (as applicable).  Any attribute
of the old storage can be carried over unchanged to the new storage, by
specifying "=" for it.  For example, to create a new storage "new_store" that is
identical to the existing "old_store", except for having a change queue capacity
of 1024 records:-

	dev45$ grower old_store new_store = = = = 1024

Expanding or contracting a storage can be done by specifying different values
for the base and maximum identifiers.

	  		   ===============================================

Exit statuses: the programs will return a unique value for each kind of error
that causes them to quit.  The meaning of the various values is as follows:-

0       - no error
1-127  	- C library error (if errno < 128; cf. errno.h)
128-192 - received a signal (cf. kill -l after subtracting 128)
200-205	- C library error (if errno >= 128; cf. errno.h after subtracting 70)
220-255	- Cachester library error (cf. status.h, after negating)
