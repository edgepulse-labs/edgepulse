#include "edgepulse.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t keep_running = 1;

static void handle_signal(int signo)
{
	(void)signo;
	keep_running = 0;
}

static int ensure_state_dir(void)
{
	if (mkdir(EDGEPULSE_STATE_DIR, 0755) == 0 || errno == EEXIST)
		return 0;

	fprintf(stderr, "edgepulse: cannot create %s: %s\n",
		EDGEPULSE_STATE_DIR, strerror(errno));
	return -1;
}

static int read_uptime(double *uptime_sec)
{
	FILE *fp = fopen("/proc/uptime", "r");

	if (!fp)
		return -1;

	if (fscanf(fp, "%lf", uptime_sec) != 1) {
		fclose(fp);
		return -1;
	}

	fclose(fp);
	return 0;
}

static int read_loadavg(double *load1, double *load5, double *load15)
{
	FILE *fp = fopen("/proc/loadavg", "r");

	if (!fp)
		return -1;

	if (fscanf(fp, "%lf %lf %lf", load1, load5, load15) != 3) {
		fclose(fp);
		return -1;
	}

	fclose(fp);
	return 0;
}

static int parse_meminfo_line(const char *line, const char *key, unsigned long *value)
{
	size_t key_len = strlen(key);

	if (strncmp(line, key, key_len) != 0)
		return 0;

	if (sscanf(line + key_len, ": %lu kB", value) == 1)
		return 1;

	return -1;
}

static int read_meminfo(struct edgepulse_snapshot *snapshot)
{
	char line[256];
	FILE *fp = fopen("/proc/meminfo", "r");

	if (!fp)
		return -1;

	while (fgets(line, sizeof(line), fp)) {
		if (parse_meminfo_line(line, "MemTotal", &snapshot->mem_total_kb) < 0 ||
		    parse_meminfo_line(line, "MemAvailable", &snapshot->mem_available_kb) < 0 ||
		    parse_meminfo_line(line, "MemFree", &snapshot->mem_free_kb) < 0) {
			fclose(fp);
			return -1;
		}
	}

	fclose(fp);
	return snapshot->mem_total_kb > 0 ? 0 : -1;
}

static int collect_snapshot(struct edgepulse_snapshot *snapshot)
{
	memset(snapshot, 0, sizeof(*snapshot));

	if (read_uptime(&snapshot->uptime_sec) != 0)
		return -1;
	if (read_loadavg(&snapshot->load1, &snapshot->load5, &snapshot->load15) != 0)
		return -1;
	if (read_meminfo(snapshot) != 0)
		return -1;

	return 0;
}

static void write_json(FILE *fp, const struct edgepulse_snapshot *snapshot)
{
	time_t now = time(NULL);
	double used_ratio = 0.0;

	if (snapshot->mem_total_kb > 0) {
		used_ratio = 1.0 -
			((double)snapshot->mem_available_kb / (double)snapshot->mem_total_kb);
	}

	fprintf(fp, "{\n");
	fprintf(fp, "  \"version\": \"%s\",\n", EDGEPULSE_VERSION);
	fprintf(fp, "  \"timestamp\": %lld,\n", (long long)now);
	fprintf(fp, "  \"uptime_sec\": %.2f,\n", snapshot->uptime_sec);
	fprintf(fp, "  \"load\": { \"1m\": %.2f, \"5m\": %.2f, \"15m\": %.2f },\n",
		snapshot->load1, snapshot->load5, snapshot->load15);
	fprintf(fp, "  \"memory\": {\n");
	fprintf(fp, "    \"total_kb\": %lu,\n", snapshot->mem_total_kb);
	fprintf(fp, "    \"available_kb\": %lu,\n", snapshot->mem_available_kb);
	fprintf(fp, "    \"free_kb\": %lu,\n", snapshot->mem_free_kb);
	fprintf(fp, "    \"used_ratio\": %.4f\n", used_ratio);
	fprintf(fp, "  }\n");
	fprintf(fp, "}\n");
}

static int print_status(void)
{
	struct edgepulse_snapshot snapshot;

	if (collect_snapshot(&snapshot) != 0) {
		fprintf(stderr, "edgepulse: failed to collect status snapshot\n");
		return 1;
	}

	write_json(stdout, &snapshot);
	return 0;
}

static int write_status_file(void)
{
	char tmp_path[PATH_MAX];
	struct edgepulse_snapshot snapshot;
	FILE *fp;

	if (collect_snapshot(&snapshot) != 0)
		return -1;
	if (ensure_state_dir() != 0)
		return -1;

	snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", EDGEPULSE_STATUS_PATH);
	fp = fopen(tmp_path, "w");
	if (!fp)
		return -1;

	write_json(fp, &snapshot);

	if (fclose(fp) != 0)
		return -1;
	if (rename(tmp_path, EDGEPULSE_STATUS_PATH) != 0)
		return -1;

	return 0;
}

static int run_daemon(int interval_sec)
{
	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);

	while (keep_running) {
		if (write_status_file() != 0) {
			fprintf(stderr, "edgepulse: failed to write %s: %s\n",
				EDGEPULSE_STATUS_PATH, strerror(errno));
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
			char *end = NULL;
			long parsed = strtol(argv[2], &end, 10);

			if (!end || *end != '\0' || parsed <= 0 || parsed > INT_MAX) {
				fprintf(stderr, "edgepulse: invalid interval: %s\n", argv[2]);
				return 2;
			}

			interval_sec = (int)parsed;
		}

		return run_daemon(interval_sec);
	}

	print_usage(stderr);
	return 2;
}

