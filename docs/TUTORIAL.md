# Writer/Reader Tutorial

We've included a small test program that demonstrates the functionality of the Lancaster library.
For this demo, you'll need two machines with multicast connectivity between them. Usually this
means running on the same LAN with support for multicast on your switches.

From a high-level perspective, the flow of data looks like this:

    writer -> shm:/lancaster -> publisher -> subscriber -> shm:/lanster -> reader

That is, the writer application writes to the shared memory "lancaster" storage. The publisher
reads this shared memory and multicasts it to subscribers running on different hosts. The
subscriber receives this data and writes it into its own shared memory (also called "lancaster").
And finally the reader can retrieve this data from shared memory for its own use.

This creates a very fast pub-sub system for high-volume, low-latency data. Other built-in
mechanisms such as a TCP back-channel add reliability semantics on top of this base.

## Install

This should be familiar to you if you've worked much with open source or other C programs.
Standard GNU autoconf build here. We'll just install everything into `/tmp/lancaster` for our demo.

    ./configure --prefix=/tmp/lancaster
    make
    make install

## Computer 1: Writer and Publisher

We'll start the writer program on the first machine. This uses the Lancaster API to write into
storage (which can be a file or shared memory).

Our writer will create a store named "TEST" that's written to shared memory at "shm:/lancaster", as
shown in the command below. We also specify that the local change capacity (`-q`) is 1024 records.
We tell the writer to write every 1000ms. Let's just start it in the background.

    ./writer -q 1024 shm:/lancaster 1000 &

At this point, This just writes into local shared memory. We have to start the publisher in order
to actually send the data to other host over multicast. We'll setup our publisher to multicast
the data that's being written to "shm:/lancaster".

Since high-performance environments often have multiple NICs on a single host, we must specify
an IP address rather than a DNS name. Let's say that we want our Lancaster pub-sub system to
communicate over the multicast group at 226.1.1.1 on port 12345. For this example, let's also
assume that the host we're running on has an IP address 10.2.2.152. So we'll bind the publisher
to send over that interface. If you don't specify the local port, it'll pick one dynamically.
This port is used by the TCP back-channel to bootstrap communications and ensure reliability.

    ./publisher shm:/lancaster 10.2.2.152:12345 226.1.1.1:12345
    "TEST", RECV: 1, PKT/s: 10.00, GAP/s: 0.00, TCP KB/s: 0.01, MCAST KB/s: 14.37

You should see some helpful stats output describing the network activity.

## Computer 2: Subscriber and Reader

On our second machine, we can start the subscriber and reader programs.

Let's start the subscriber first. Again, we'll specify to store all received data in shared memory
as well. Although we'll call this shared memory store "lancaster" for consistency, it doesn't have
to match the publisher. Obviously shared memory on a host is entirely independent from that on
another host.

We also need to tell the subscriber how to connect to the publisher's TCP back-channel. The
multicast group and other information will be sent to the subscriber over this TCP channel.
Continuing the example, we tell it to connect to the publisher at "10.2.2.152:12345" as configured
above.

    ./subscriber shm:/lancaster 10.2.2.152:12345
    "TEST", PKT/s: 10.00, GAP/s: 0.00, TCP KB/s: 0.01, MCAST KB/s: 14.37, MIN/us: 81.00, AVG/us: 105.70, MAX/us: 169.00, STD/us: 27.83

Like the publisher, the subscriber outputs some useful metrics about its network performance.

Now  that we're writing to shared memory on our second machine, the last step is to setup the
reader application to actually use the data sent by the writer application. All we need to tell
it is where to read the data from. This is the local shared memory storage.

    ./reader shm:/lancaster
    "TEST", 111111111111111111111111...

The output of the reader shows that data was sent from the "TEST" store (configured in the writer).
Then a digit is output every 0.2 second representing the integrity of the read data. This digit is
a bitwise OR of the following values.

    * 0 - no data was read
    * 1 - data was read
    * 2 - data was read out-of-order to what was written
    * 4 - change queue was overrun (only if -Q option is specified)

Thus a "1" means data were received successfully, a "3" means data was received but it was out of
order, a "5" means it was received but the change queues was overrun, and so on.

## Troubleshooting

### No Multicast Heartbeat

If your subscriber application is showing no MCAST traffic and prints this error

    subscriber: receiver_run: no multicast heartbeat

then you likely don't have multicast connectivity between your publisher and subscriber. The
first step is confirm basic network connectivity (`ping`, `tracert`, etc.), check your IP and port
assignments, etc. If that looks good, there may be switches or routers sitting between the two
hosts that do not support multicast traffic. You'll need to talk to your network admins about that.

Good luck and God bless the queen!
