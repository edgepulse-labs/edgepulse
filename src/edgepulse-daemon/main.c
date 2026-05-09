#include "edgepulse.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t keep_running = 1;

static void handle_signal(int signo)
{
	(void)signo;
	keep_running = 0;
}

static int print_status(void)
{
	struct edgepulse_snapshot snapshot;

	if (edgepulse_collect_snapshot(&snapshot) != 0) {
		fprintf(stderr, "edgepulse: failed to collect status snapshot\n");
		return 1;
	}

	edgepulse_write_snapshot_json(stdout, &snapshot);
	return 0;
}

static int run_daemon(int interval_sec)
{
	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);

	while (keep_running) {
		if (edgepulse_write_status_outputs(EDGEPULSE_DB_PATH) != 0) {
			fprintf(stderr, "edgepulse: failed to write telemetry outputs: %s\n",
				strerror(errno));
		}

		for (int i = 0; keep_running && i < interval_sec; i++)
			sleep(1);
	}

	return 0;
}

static void print_usage(FILE *fp)
{
	fprintf(fp, "Usage: edgepulse <status|daemon> [interval_sec]\n");
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		print_usage(stderr);
		return 2;
	}

	if (strcmp(argv[1], "status") == 0)
		return print_status();

	if (strcmp(argv[1], "daemon") == 0) {
		int interval_sec = EDGEPULSE_DEFAULT_INTERVAL_SEC;

		if (argc >= 3) {
			if (edgepulse_parse_positive_int(argv[2], &interval_sec) != 0) {
				fprintf(stderr, "edgepulse: invalid interval: %s\n", argv[2]);
				return 2;
			}
		}

		return run_daemon(interval_sec);
	}

	print_usage(stderr);
	return 2;
}
