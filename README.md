CACHESTER - fast, reliable multicasting of ephemeral data
=========================================================

The software consists of a C library, libcachester, provided in both static and
dynamic forms, together with nine utility programs for use in production and
development environments.

Any questions, bug reports, suggestions for improvements etc. - please contact
Justin Flude <jflude@peak6.com>

             ===============================================

    writer [-v] [-p ERROR PREFIX] [-q CHANGE-QUEUE-CAPACITY] [-r] \
           [-T TOUCH-PERIOD] STORAGE-FILE DELAY

    reader [-v] [-O ORPHAN-TIMEOUT] [-p ERROR PREFIX] [-s] STORAGE-FILE

These are test programs which write and read data to/from a "storage" and check
whether what is read is what was written, in the correct order.

Storages are arrays of records, each having an identifier, contained within a
regular file on disk, such as "/tmp/my-file", or a POSIX shared memory segment,
which is specified with a URI-like prefix of shm, eg. "shm:/my-segment".
Storages can persist across runs (although the storages created by the test
programs do not).

A "change queue" is an optional section of a storage used as a circular buffer
containing the identifiers of records recently modified.  The capacity of a
change queue, if specified, must be either zero or a non-zero power of two.

WRITER will create a storage with a change queue of the given capacity, then
update sequential slots with ascending values at a speed determined by DELAY
(the number of microseconds to pause after each write, which may be zero).  If
the -r option is specified, slots will be chosen for update at random, instead
of sequentially.  The storage will be "touched" at least every TOUCH-PERIOD
microseconds (defaulting to one second).

READER outputs a hexadecimal digit every fifth of a second to indicate the
integrity of the read data - its value is the bitwise OR-ing of the following
numbers, indicating which conditions occured in this last "tick":-

    0 - no data was read
    1 - data was read
    2 - data was read out-of-order to what was written
    4 - change queue was overrun

If the -s option is supplied to READER then it will output storage latency
statistics instead of its usual output.  If the storage has not been "touched"
by its writer for ORPHAN-TIMEOUT microseconds (defaulting to 3 seconds), READER
will exit with an error.

The -p option causes the programs to include the specified prefix in error
messages, to allow easier identification when running multiple instances.

             ===============================================

    publisher [-v] [-a ADVERT-ADDRESS:PORT] [-A ADVERT-PERIOD] \
              [-e ENVIRONMENT] [-H HEARTBEAT-PERIOD] [-i DATA-INTERFACE] \
              [-I ADVERT-INTERFACE] [-j|-s] [-l] [-O ORPHAN-TIMEOUT] \
              [-p ERROR PREFIX] [-P MAXIMUM-PACKET-AGE] [-t TTL] STORAGE-FILE \
              TCP-ADDRESS:PORT MULTICAST-ADDRESS:PORT

    subscriber [-v] [-j] [-p ERROR PREFIX] [-q CHANGE-QUEUE-CAPACITY] \
               [-T TOUCH-PERIOD] STORAGE-FILE TCP-ADDRESS:PORT

These are production-ready, generic programs to establish a multicast transport
between a process wanting to publish data and one or more processes on multiple
hosts wanting to receive it.  PUBLISHER looks for a storage to read from (such
as one created by WRITER).  It will listen on TCP-ADDRESS:PORT for connections
from subscribers.  When one is made, PUBLISHER will send the subscriber the
MULTICAST-ADDRESS:PORT to receive data, and the HEARTBEAT-PERIOD microseconds
(defaulting to one second) it should expect to receive heartbeats in the absence
of data (PUBLISHER will send separate heartbeats over both the TCP and multicast
channels).  If a heartbeat is not received in time, PUBLISHER will exit with an
error.  PUBLISHER will attempt to fill a UDP packet with data before sending it,
but will send a partial packet if the data in it is older than MAX-PACKET-AGE
microseconds (defaulting to 2 milliseconds).  PUBLISHER will multicast data over
the DATA-INTERFACE network interface rather than the system's default, if the -i
option is specified.  Multicast data will be sent with a TTL other than 1 if the
-t option is specified.  Multicast data will "loopback" (be delivered also on
the sending host) if the -l option is specified, which enables testing on a
single host.  If the -a option is specified, PUBLISHER will "advertize" its
existence by multicasting its connection and storage details every ADVERT-PERIOD
microseconds (defaulting to 3 seconds).  Those advertisements will be multicast
over the ADVERT-INTERFACE network interface if the -I option is specified, or
the system's default if not.  If the storage has not been recently "touched" by
its writer within ORPHAN-TIMEOUT microseconds (defaulting to 3 seconds), then
PUBLISHER will exit with an error.

SUBSCRIBER will try to connect to a PUBLISHER at TCP-ADDRESS:PORT, and based on 
the attributes that PUBLISHER sends it, create a storage similar in structure
to PUBLISHER's (except for the change queue capacity, which may be specified for
SUBSCRIBER independently, with the -q option).  Data read by PUBLISHER is
multicast to SUBSCRIBER and written to SUBSCRIBER's storage.  SUBSCRIBER will
"touch" the storage every TOUCH-PERIOD microseconds (defaulting to one second).

Both PUBLISHER and SUBSCRIBER have an -j option which causes their normal 
output of statistics to be output in JSON format.  PUBLISHER also has a -s
option which causes it to output storage latency statistics instead of its
usual output (the -j option also includes the storage latency statistics).

A typical scenario for testing would be to run WRITER and PUBLISHER on one host,
and SUBSCRIBER and READER on another.  For example, if the former were to be
run on pslchi6dpricedev45 (10.2.2.152):-

    dev45$ writer /tmp/his-local-file 4096
    dev45$ publisher /tmp/his-local-file 10.2.2.152:23266 227.1.1.34:56134 \
                     500000 10000

...and the SUBSCRIBER and READER commands on pslchi6dpricedev42:-

    dev42$ subscriber shm:/her-local-segment 4096 10.2.2.152:23266
    dev42$ reader shm:/her-local-segment

             ===============================================

    inspector [-v] [-a] [-p] [-q] [-r] [-V] STORAGE-FILE [RECORD-ID...]

    grower [-v] STORAGE-FILE NEW-STORAGE-FILE NEW-BASE-ID NEW-MAX-ID \
           NEW-VALUE-SIZE NEW-PROPERTY-SIZE NEW-QUEUE-CAPACITY

    deleter [-v] [-f] STORAGE-FILE [STORAGE-FILE ...]

These are utility programs to show, modify or delete a storage.

INSPECTOR, given the -a option, outputs the attribute values of a storage, such
as the size of a record within it, the capacity of its change queue, the
description associated with it, etc.  The -q option will cause the change queue,
if any, to be output, while the -r option will cause the timestamp and revision
of the specified records to be output - for all records, if no record
identifier(s) are specified).  Properties for records will be included in the
output if the -p option is given.  If no option is specified, the program will
only attempt to open and verify the storage, exiting with zero (success) if the
format of the storage is valid.

The GROWER program will create a new storage based upon an existing storage, and
containing the same data copied to its records (as applicable).  Any attribute
of the old storage can be carried over unchanged to the new storage, by
specifying "=" for it.  For example, to create a new storage "new-store" that is
identical to the existing "old-store", except for having a change queue capacity
of 1024 records:-

    dev45$ grower old-store new-store = = = = 1024

Expanding or contracting a storage can be done by specifying different values
for the base and maximum identifiers.  A straight copy of a storage can be done
by specifying "=" for all attributes.  NB. GROWER never copies the contents of
the change queue.

The DELETER program will delete the given storages.  If the -f option is given
then it is not an error if a storage does not exist.

             ===============================================

    eraser [-v] [-c] [-V] STORAGE-FILE RECORD-ID [RECORD-ID ...]

    copier [-v] [-V] SOURCE-STORAGE-FILE DESTINATION-STORAGE-FILE \
           SOURCE-RECORD-ID [SOURCE-RECORD-ID ...]

These are utility programs to modify the contents of storages.

ERASER removes the specified records from the specified storage.  The storage
is compacted to eliminate the holes left behind if the -c option is given.  (The
compaction is performed by moving the last record in the storage to the place
where the erased record used to be).

COPIER makes a copy of the specified source record in the destination storage.

Each program, if the -V option is specified ("verbose"), will output the
identifiers of the records it is operating upon.

             ===============================================

Exit statuses: the programs will return a unique value for each kind of error
that causes them to quit.  The meaning of the various values is as follows:-

  0       - no error
  1-127   - C library error (if errno < 128; cf. errno.h)
  128-192 - received a signal (cf. kill -l after subtracting 128)
  200-205 - C library error (if errno >= 128; cf. errno.h after subtracting 70)
  220-255 - Cachester library error (cf. status.h, after negating)
