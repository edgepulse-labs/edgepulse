#ifndef EDGEPULSE_H
#define EDGEPULSE_H

#include <stdio.h>
#include <time.h>

#define EDGEPULSE_VERSION "0.1.0-dev"
#define EDGEPULSE_STATE_DIR "/tmp/edgepulse"
#define EDGEPULSE_STATUS_PATH "/tmp/edgepulse/edgepulse.json"
#define EDGEPULSE_DB_PATH "/tmp/edgepulse/edgepulse.db"
#define EDGEPULSE_DEFAULT_INTERVAL_SEC 5
#define EDGEPULSE_MAX_SAMPLES 256

struct edgepulse_snapshot {
	double uptime_sec;
	double load1;
	double load5;
	double load15;
	unsigned long mem_total_kb;
	unsigned long mem_available_kb;
	unsigned long mem_free_kb;
};

struct edgepulse_sample {
	char metric[64];
	char labels[128];
	double value;
	char status[32];
};

struct edgepulse_sample_batch {
	time_t timestamp;
	struct edgepulse_sample samples[EDGEPULSE_MAX_SAMPLES];
	size_t count;
};

struct edgepulse_feature {
	char metric[64];
	char labels[128];
	int window_sec;
	time_t window_start;
	time_t window_end;
	int count;
	double mean;
	double min;
	double max;
	double stddev;
	double delta;
	double rate_per_sec;
	double coefficient_of_variation;
};

int edgepulse_collect_snapshot(struct edgepulse_snapshot *snapshot);
int edgepulse_collect_sample_batch(struct edgepulse_sample_batch *batch);
int edgepulse_init_database(const char *db_path);
int edgepulse_write_sample_batch(const char *db_path,
				 const struct edgepulse_sample_batch *batch);
int edgepulse_store_feature_window(const char *db_path, int window_sec);
int edgepulse_ensure_state_dir(void);
int edgepulse_parse_positive_int(const char *value, int *parsed);
int edgepulse_parse_meminfo_stream(FILE *fp, struct edgepulse_snapshot *snapshot);
int edgepulse_parse_proc_stat_stream(FILE *fp, struct edgepulse_sample_batch *batch);
int edgepulse_parse_net_dev_stream(FILE *fp, struct edgepulse_sample_batch *batch);
int edgepulse_parse_wireless_stream(FILE *fp, struct edgepulse_sample_batch *batch);
int edgepulse_parse_nft_counters_stream(FILE *fp, struct edgepulse_sample_batch *batch);
int edgepulse_store_metadata(const char *db_path, const char *key, const char *value);
int edgepulse_apply_retention(const char *db_path, int raw_retention_sec,
			      int feature_retention_sec);
int edgepulse_write_status_file(void);
int edgepulse_write_status_outputs(const char *db_path);
double edgepulse_memory_used_ratio(const struct edgepulse_snapshot *snapshot);
void edgepulse_write_snapshot_json(FILE *fp, const struct edgepulse_snapshot *snapshot);

#endif
