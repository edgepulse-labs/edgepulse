#ifndef EDGEPULSE_H
#define EDGEPULSE_H

#include <stdio.h>

#define EDGEPULSE_VERSION "0.1.0-dev"
#define EDGEPULSE_STATE_DIR "/tmp/edgepulse"
#define EDGEPULSE_STATUS_PATH "/tmp/edgepulse/edgepulse.json"
#define EDGEPULSE_DEFAULT_INTERVAL_SEC 5

struct edgepulse_snapshot {
	double uptime_sec;
	double load1;
	double load5;
	double load15;
	unsigned long mem_total_kb;
	unsigned long mem_available_kb;
	unsigned long mem_free_kb;
};

int edgepulse_collect_snapshot(struct edgepulse_snapshot *snapshot);
int edgepulse_ensure_state_dir(void);
int edgepulse_parse_positive_int(const char *value, int *parsed);
int edgepulse_write_status_file(void);
double edgepulse_memory_used_ratio(const struct edgepulse_snapshot *snapshot);
void edgepulse_write_snapshot_json(FILE *fp, const struct edgepulse_snapshot *snapshot);

#endif
