#include "edgepulse.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

int edgepulse_ensure_state_dir(void)
{
	if (mkdir(EDGEPULSE_STATE_DIR, 0755) == 0 || errno == EEXIST)
		return 0;

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

int edgepulse_collect_snapshot(struct edgepulse_snapshot *snapshot)
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

double edgepulse_memory_used_ratio(const struct edgepulse_snapshot *snapshot)
{
	if (snapshot->mem_total_kb == 0)
		return 0.0;

	return 1.0 -
		((double)snapshot->mem_available_kb / (double)snapshot->mem_total_kb);
}

int edgepulse_parse_positive_int(const char *value, int *parsed)
{
	char *end = NULL;
	long result;

	if (!value || !*value)
		return -1;

	errno = 0;
	result = strtol(value, &end, 10);
	if (errno != 0 || !end || *end != '\0' || result <= 0 || result > INT_MAX)
		return -1;

	*parsed = (int)result;
	return 0;
}

void edgepulse_write_snapshot_json(FILE *fp, const struct edgepulse_snapshot *snapshot)
{
	time_t now = time(NULL);
	double used_ratio = edgepulse_memory_used_ratio(snapshot);

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

int edgepulse_write_status_file(void)
{
	char tmp_path[PATH_MAX];
	struct edgepulse_snapshot snapshot;
	FILE *fp;

	if (edgepulse_collect_snapshot(&snapshot) != 0)
		return -1;
	if (edgepulse_ensure_state_dir() != 0)
		return -1;

	snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", EDGEPULSE_STATUS_PATH);
	fp = fopen(tmp_path, "w");
	if (!fp)
		return -1;

	edgepulse_write_snapshot_json(fp, &snapshot);

	if (fclose(fp) != 0)
		return -1;
	if (rename(tmp_path, EDGEPULSE_STATUS_PATH) != 0)
		return -1;

	return 0;
}
