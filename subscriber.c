/* generic subscriber */

#include "error.h"
#include "clock.h"
#include "receiver.h"
#include "signals.h"
#include "sock.h"
#include "thread.h"
#include "udp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DISPLAY_DELAY_USEC (1 * 1000000)

boolean as_json = FALSE;

static void show_syntax(void)
{
	fprintf(stderr, "Syntax: %s [-v] [-j] [-p ERROR PREFIX] STORAGE-FILE "
			"CHANGE-QUEUE-SIZE TCP-ADDRESS:PORT\n", error_get_program_name());

	exit(-SYNTAX_ERROR);
}

static void show_version(void)
{
	puts("subscriber " SOURCE_VERSION);
	exit(0);
}

static void* stats_func(thread_handle thr)
{
	receiver_handle recv = thread_get_param(thr);
	status st;
	char hostname[256], alias[32];
	const char *storage_desc, *delim_pos, *eol_seq;
	microsec last_print;
	struct udp_conn_info udp_stat_conn;
    const char* udp_stat_url = getenv("UDP_STATS_URL");
    boolean udp_stat_pub_enabled = FALSE;
    int stats_buff_used = 0;

    if (udp_stat_url) {
        if (FAILED(st = open_udp_sock_conn(&udp_stat_conn, udp_stat_url)))
            return (void*) (long) st;

        udp_stat_pub_enabled = TRUE;
    }
    
	storage_desc = storage_get_description(receiver_get_storage(recv));

	if ((delim_pos = strchr(storage_desc, '.')) == NULL)
		strncpy(alias, "unknown", sizeof(alias));
	else
		strncpy(alias, storage_desc, delim_pos - storage_desc);
	
	if (FAILED(st = sock_get_hostname(hostname, sizeof(hostname))) ||
		FAILED(st = clock_time(&last_print)))
		return (void*) (long) st;

	eol_seq = (isatty(STDOUT_FILENO) ? "\033[K\r" : "\n");

	while (!thread_is_stopping(thr)) {
		microsec now;
		double secs;
		if (FAILED(st = signal_is_raised(SIGHUP)) ||
			FAILED(st = signal_is_raised(SIGINT)) ||
			FAILED(st = signal_is_raised(SIGTERM)) ||
			FAILED(st = clock_sleep(DISPLAY_DELAY_USEC)) ||
			FAILED(st = clock_time(&now)))
			break;

		secs = (now - last_print) / 1000000.0;

		if (as_json) {
			char ts[64];
			char stats_buf[1024];
			if (FAILED(st = clock_get_text(now, 3, ts, sizeof(ts))))
                break;

            stats_buff_used =
				sprintf(stats_buf,
						"{\"@timestamp\":\"%s\", "
						"\"app\":\"subscriber\", "
						"\"cat\":\"data_feed\", "
						"\"alias\":\"%s\", "
						"\"storage\":\"%s\", "
						"\"pkt/s\":%.2f, "
						"\"gap/s\":%.2f, "
						"\"tcp_kb/s\":%.2f, "
						"\"mcast_kb/s\":%.2f, "
						"\"min/us\":%.2f, "
						"\"avg/us\":%.2f, "
						"\"max/us\":%.2f, "
						"\"std/us\":%.2f}",
						ts,
						alias,
						storage_get_file(receiver_get_storage(recv)),
						receiver_get_mcast_packets_recv(recv) / secs,
						receiver_get_tcp_gap_count(recv) / secs,
						receiver_get_tcp_bytes_recv(recv) / secs / 1024,
						receiver_get_mcast_bytes_recv(recv) / secs / 1024,
						receiver_get_mcast_min_latency(recv),
						receiver_get_mcast_mean_latency(recv),
						receiver_get_mcast_max_latency(recv),
						receiver_get_mcast_stddev_latency(recv));

			if (stats_buff_used < 0) {
				st = error_errno("sprintf");
				break;
			}
            
            if (!udp_stat_pub_enabled)
                puts(stats_buf);
			else {
                if (FAILED(st = sock_sendto(udp_stat_conn.sock_fd_,
											udp_stat_conn.server_sock_addr_,
											stats_buf, stats_buff_used)))
                    break;
            }
        } else
			printf("\"%.20s\", PKT/s: %.2f, GAP/s: %.2f, "
				   "TCP KB/s: %.2f, MCAST KB/s: %.2f, "
				   "MIN/us: %.2f, AVG/us: %.2f, MAX/us: %.2f, "
				   "STD/us: %.2f%s",
				   storage_desc,
				   receiver_get_mcast_packets_recv(recv) / secs,
				   receiver_get_tcp_gap_count(recv) / secs,
				   receiver_get_tcp_bytes_recv(recv) / secs / 1024,
				   receiver_get_mcast_bytes_recv(recv) / secs / 1024,
				   receiver_get_mcast_min_latency(recv),
				   receiver_get_mcast_mean_latency(recv),
				   receiver_get_mcast_max_latency(recv),
				   receiver_get_mcast_stddev_latency(recv),
				   eol_seq);

		if (FAILED(st = receiver_roll_stats(recv)))
			break;

		last_print = now;
		if (!udp_stat_pub_enabled)
			fflush(stdout);
	}

	receiver_stop(recv);

    if (udp_stat_pub_enabled) {
		status st2 = close_udp_sock_conn(&udp_stat_conn);
		if (!FAILED(st))
			st = st2;
	}
    
	putchar('\n');
	return (void*) (long) st;
}

int main(int argc, char* argv[])
{
	receiver_handle recv;
	thread_handle stats_thread;
	const char *mmap_file, *tcp_addr;
	char* colon;
	int tcp_port;
	size_t q_capacity;
	int opt;
	void* stats_result;

	char prog_name[256];
	strcpy(prog_name, argv[0]);
	error_set_program_name(prog_name);

	while ((opt = getopt(argc, argv, "jp:v")) != -1)
		switch (opt) {
		case 'j':
			as_json = TRUE;
			break;
		case 'p':
			strcat(prog_name, ": ");
			strcat(prog_name, optarg);
			error_set_program_name(prog_name);
			break;
		case 'v':
			show_version();
		default:
			show_syntax();
		}

	if ((argc - optind) != 3)
		show_syntax();

	mmap_file = argv[optind++];
	q_capacity = atoi(argv[optind++]);

	tcp_addr = argv[optind++];
	colon = strchr(tcp_addr, ':');
	if (!colon)
		show_syntax();

	*colon = '\0';
	tcp_port = atoi(colon + 1);

	if (FAILED(signal_add_handler(SIGHUP)) ||
		FAILED(signal_add_handler(SIGINT)) ||
		FAILED(signal_add_handler(SIGTERM)) ||
		FAILED(receiver_create(&recv, mmap_file, q_capacity,
							   0, tcp_addr, tcp_port)) ||
		FAILED(thread_create(&stats_thread, stats_func, recv)) ||
		FAILED(receiver_run(recv)) ||
		FAILED(thread_stop(stats_thread, &stats_result)) ||
		FAILED(thread_destroy(&stats_thread)) ||
		FAILED((status) (long) stats_result) ||
		FAILED(receiver_destroy(&recv)) ||
		FAILED(signal_remove_handler(SIGHUP)) ||
		FAILED(signal_remove_handler(SIGINT)) ||
		FAILED(signal_remove_handler(SIGTERM))) {
		if (!as_json)
			putchar('\n');
	
		error_report_fatal();
	}

	return 0;
}
