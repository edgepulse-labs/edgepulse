#include "edgepulse.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <sqlite3.h>

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

static int add_sample(struct edgepulse_sample_batch *batch, const char *metric,
		      const char *labels, double value, const char *status)
{
	struct edgepulse_sample *sample;

	if (batch->count >= EDGEPULSE_MAX_SAMPLES)
		return -1;

	sample = &batch->samples[batch->count++];
	snprintf(sample->metric, sizeof(sample->metric), "%s", metric);
	snprintf(sample->labels, sizeof(sample->labels), "%s", labels ? labels : "");
	snprintf(sample->status, sizeof(sample->status), "%s", status ? status : "ok");
	sample->value = value;
	return 0;
}

static int add_snapshot_samples(struct edgepulse_sample_batch *batch,
				const struct edgepulse_snapshot *snapshot)
{
	if (add_sample(batch, "system.uptime_sec", "", snapshot->uptime_sec, "ok") != 0 ||
	    add_sample(batch, "system.load1", "", snapshot->load1, "ok") != 0 ||
	    add_sample(batch, "system.load5", "", snapshot->load5, "ok") != 0 ||
	    add_sample(batch, "system.load15", "", snapshot->load15, "ok") != 0 ||
	    add_sample(batch, "memory.total_kb", "", (double)snapshot->mem_total_kb, "ok") != 0 ||
	    add_sample(batch, "memory.available_kb", "", (double)snapshot->mem_available_kb, "ok") != 0 ||
	    add_sample(batch, "memory.free_kb", "", (double)snapshot->mem_free_kb, "ok") != 0 ||
	    add_sample(batch, "memory.used_ratio", "",
		       edgepulse_memory_used_ratio(snapshot), "ok") != 0)
		return -1;

	return 0;
}

static int collect_cpu_samples(struct edgepulse_sample_batch *batch)
{
	char cpu[32];
	unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
	FILE *fp = fopen("/proc/stat", "r");

	if (!fp)
		return -1;

	if (fscanf(fp, "%31s %llu %llu %llu %llu %llu %llu %llu %llu",
		   cpu, &user, &nice, &system, &idle, &iowait, &irq, &softirq,
		   &steal) != 9) {
		fclose(fp);
		return -1;
	}

	fclose(fp);

	if (strcmp(cpu, "cpu") != 0)
		return -1;

	if (add_sample(batch, "cpu.user_jiffies", "", (double)user, "ok") != 0 ||
	    add_sample(batch, "cpu.nice_jiffies", "", (double)nice, "ok") != 0 ||
	    add_sample(batch, "cpu.system_jiffies", "", (double)system, "ok") != 0 ||
	    add_sample(batch, "cpu.idle_jiffies", "", (double)idle, "ok") != 0 ||
	    add_sample(batch, "cpu.iowait_jiffies", "", (double)iowait, "ok") != 0 ||
	    add_sample(batch, "cpu.irq_jiffies", "", (double)irq, "ok") != 0 ||
	    add_sample(batch, "cpu.softirq_jiffies", "", (double)softirq, "ok") != 0 ||
	    add_sample(batch, "cpu.steal_jiffies", "", (double)steal, "ok") != 0)
		return -1;

	return 0;
}

static void trim_trailing_colon(char *value)
{
	size_t len = strlen(value);

	if (len > 0 && value[len - 1] == ':')
		value[len - 1] = '\0';
}

static int collect_network_samples(struct edgepulse_sample_batch *batch)
{
	char line[512];
	FILE *fp = fopen("/proc/net/dev", "r");

	if (!fp)
		return -1;

	/* Skip headers. */
	if (!fgets(line, sizeof(line), fp) || !fgets(line, sizeof(line), fp)) {
		fclose(fp);
		return -1;
	}

	while (fgets(line, sizeof(line), fp)) {
		char iface[64];
		char labels[96];
		unsigned long long rx_bytes, rx_packets, tx_bytes, tx_packets;
		unsigned long long ignored[12];

		if (sscanf(line,
			   " %63s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
			   iface, &rx_bytes, &rx_packets, &ignored[0], &ignored[1],
			   &ignored[2], &ignored[3], &ignored[4], &ignored[5],
			   &tx_bytes, &tx_packets, &ignored[6], &ignored[7],
			   &ignored[8], &ignored[9], &ignored[10], &ignored[11]) != 17)
			continue;

		trim_trailing_colon(iface);
		snprintf(labels, sizeof(labels), "iface=%s", iface);

		if (add_sample(batch, "network.rx_bytes", labels, (double)rx_bytes, "ok") != 0 ||
		    add_sample(batch, "network.rx_packets", labels, (double)rx_packets, "ok") != 0 ||
		    add_sample(batch, "network.tx_bytes", labels, (double)tx_bytes, "ok") != 0 ||
		    add_sample(batch, "network.tx_packets", labels, (double)tx_packets, "ok") != 0) {
			fclose(fp);
			return -1;
		}
	}

	fclose(fp);
	return 0;
}

static int collect_thermal_samples(struct edgepulse_sample_batch *batch)
{
	int found = 0;

	for (int zone = 0; zone < 16; zone++) {
		char path[128];
		char labels[64];
		long temp_millic;
		FILE *fp;

		snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%d/temp", zone);
		fp = fopen(path, "r");
		if (!fp)
			continue;

		if (fscanf(fp, "%ld", &temp_millic) == 1) {
			snprintf(labels, sizeof(labels), "zone=%d", zone);
			if (add_sample(batch, "thermal.temp_c", labels,
				       (double)temp_millic / 1000.0, "ok") != 0) {
				fclose(fp);
				return -1;
			}
			found++;
		}

		fclose(fp);
	}

	return found > 0 ? 0 : -1;
}

int edgepulse_collect_sample_batch(struct edgepulse_sample_batch *batch)
{
	struct edgepulse_snapshot snapshot;

	memset(batch, 0, sizeof(*batch));
	batch->timestamp = time(NULL);

	if (edgepulse_collect_snapshot(&snapshot) == 0)
		add_snapshot_samples(batch, &snapshot);
	else
		add_sample(batch, "collector.snapshot", "", 0.0, "error");

	if (collect_cpu_samples(batch) != 0)
		add_sample(batch, "collector.cpu", "", 0.0, "error");
	if (collect_network_samples(batch) != 0)
		add_sample(batch, "collector.network", "", 0.0, "error");
	if (collect_thermal_samples(batch) != 0)
		add_sample(batch, "collector.thermal", "", 0.0, "unavailable");

	return batch->count > 0 ? 0 : -1;
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

int edgepulse_init_database(const char *db_path)
{
	static const char *schema =
		"PRAGMA journal_mode=WAL;"
		"CREATE TABLE IF NOT EXISTS raw_samples ("
		"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"  timestamp INTEGER NOT NULL,"
		"  metric TEXT NOT NULL,"
		"  labels TEXT NOT NULL DEFAULT '',"
		"  value REAL NOT NULL,"
		"  status TEXT NOT NULL DEFAULT 'ok'"
		");"
		"CREATE INDEX IF NOT EXISTS idx_raw_samples_metric_time "
		"ON raw_samples(metric, labels, timestamp);";
	sqlite3 *db = NULL;
	char *errmsg = NULL;
	int rc;

	if (edgepulse_ensure_state_dir() != 0)
		return -1;

	rc = sqlite3_open(db_path, &db);
	if (rc != SQLITE_OK) {
		if (db)
			sqlite3_close(db);
		return -1;
	}

	rc = sqlite3_exec(db, schema, NULL, NULL, &errmsg);
	if (errmsg)
		sqlite3_free(errmsg);
	sqlite3_close(db);
	return rc == SQLITE_OK ? 0 : -1;
}

int edgepulse_write_sample_batch(const char *db_path,
				 const struct edgepulse_sample_batch *batch)
{
	static const char *sql =
		"INSERT INTO raw_samples(timestamp, metric, labels, value, status) "
		"VALUES (?, ?, ?, ?, ?);";
	sqlite3 *db = NULL;
	sqlite3_stmt *stmt = NULL;
	int rc;

	if (edgepulse_init_database(db_path) != 0)
		return -1;

	if (sqlite3_open(db_path, &db) != SQLITE_OK)
		goto fail;
	if (sqlite3_exec(db, "BEGIN IMMEDIATE;", NULL, NULL, NULL) != SQLITE_OK)
		goto fail;
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
		goto fail;

	for (size_t i = 0; i < batch->count; i++) {
		const struct edgepulse_sample *sample = &batch->samples[i];

		sqlite3_bind_int64(stmt, 1, (sqlite3_int64)batch->timestamp);
		sqlite3_bind_text(stmt, 2, sample->metric, -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 3, sample->labels, -1, SQLITE_STATIC);
		sqlite3_bind_double(stmt, 4, sample->value);
		sqlite3_bind_text(stmt, 5, sample->status, -1, SQLITE_STATIC);

		rc = sqlite3_step(stmt);
		if (rc != SQLITE_DONE)
			goto fail;

		sqlite3_reset(stmt);
		sqlite3_clear_bindings(stmt);
	}

	if (sqlite3_finalize(stmt) != SQLITE_OK) {
		stmt = NULL;
		goto fail;
	}
	stmt = NULL;

	if (sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK)
		goto fail;

	sqlite3_close(db);
	return 0;

fail:
	if (stmt)
		sqlite3_finalize(stmt);
	if (db) {
		sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
		sqlite3_close(db);
	}
	return -1;
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

int edgepulse_write_status_outputs(const char *db_path)
{
	struct edgepulse_sample_batch batch;

	if (edgepulse_write_status_file() != 0)
		return -1;
	if (edgepulse_collect_sample_batch(&batch) != 0)
		return -1;
	if (edgepulse_write_sample_batch(db_path, &batch) != 0)
		return -1;

	return 0;
}
