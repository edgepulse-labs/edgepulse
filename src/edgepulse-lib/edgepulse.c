#include "edgepulse.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <sqlite3.h>

#ifdef EDGEPULSE_ENABLE_UBUS
#include <libubox/blobmsg.h>
#include <libubus.h>
#endif

#define EDGEPULSE_MAX_INTERFACE_MAPS 32

struct edgepulse_interface_map {
	char logical[32];
	char device[32];
};

static struct edgepulse_interface_map interface_maps[EDGEPULSE_MAX_INTERFACE_MAPS];
static size_t interface_map_count;
static const char *active_db_path = EDGEPULSE_DB_PATH;

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

static void clear_interface_maps(void)
{
	memset(interface_maps, 0, sizeof(interface_maps));
	interface_map_count = 0;
}

static const char *lookup_logical_interface(const char *device)
{
	for (size_t i = 0; i < interface_map_count; i++) {
		if (strcmp(interface_maps[i].device, device) == 0)
			return interface_maps[i].logical;
	}

	return NULL;
}

#ifdef EDGEPULSE_ENABLE_UBUS
static void add_interface_map(const char *logical, const char *device)
{
	if (!logical || !*logical || !device || !*device)
		return;
	if (interface_map_count >= EDGEPULSE_MAX_INTERFACE_MAPS)
		return;

	snprintf(interface_maps[interface_map_count].logical,
		 sizeof(interface_maps[interface_map_count].logical), "%s", logical);
	snprintf(interface_maps[interface_map_count].device,
		 sizeof(interface_maps[interface_map_count].device), "%s", device);
	interface_map_count++;
}
#endif

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

int edgepulse_parse_meminfo_stream(FILE *fp, struct edgepulse_snapshot *snapshot)
{
	char line[256];

	while (fgets(line, sizeof(line), fp)) {
		if (parse_meminfo_line(line, "MemTotal", &snapshot->mem_total_kb) < 0 ||
		    parse_meminfo_line(line, "MemAvailable", &snapshot->mem_available_kb) < 0 ||
		    parse_meminfo_line(line, "MemFree", &snapshot->mem_free_kb) < 0) {
			return -1;
		}
	}

	return snapshot->mem_total_kb > 0 ? 0 : -1;
}

static int read_meminfo(struct edgepulse_snapshot *snapshot)
{
	int rc;
	FILE *fp = fopen("/proc/meminfo", "r");

	if (!fp)
		return -1;

	rc = edgepulse_parse_meminfo_stream(fp, snapshot);
	fclose(fp);
	return rc;
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

int edgepulse_parse_proc_stat_stream(FILE *fp, struct edgepulse_sample_batch *batch)
{
	char cpu[32];
	unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;

	if (fscanf(fp, "%31s %llu %llu %llu %llu %llu %llu %llu %llu",
		   cpu, &user, &nice, &system, &idle, &iowait, &irq, &softirq,
		   &steal) != 9)
		return -1;

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

static int collect_cpu_samples(struct edgepulse_sample_batch *batch)
{
	int rc;
	FILE *fp = fopen("/proc/stat", "r");

	if (!fp)
		return -1;

	rc = edgepulse_parse_proc_stat_stream(fp, batch);
	fclose(fp);
	return rc;
}

static void trim_trailing_colon(char *value)
{
	size_t len = strlen(value);

	if (len > 0 && value[len - 1] == ':')
		value[len - 1] = '\0';
}

int edgepulse_parse_net_dev_stream(FILE *fp, struct edgepulse_sample_batch *batch)
{
	char line[512];

	/* Skip headers. */
	if (!fgets(line, sizeof(line), fp) || !fgets(line, sizeof(line), fp))
		return -1;

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
		if (lookup_logical_interface(iface))
			snprintf(labels, sizeof(labels), "iface=%s,logical=%s", iface,
				 lookup_logical_interface(iface));
		else
			snprintf(labels, sizeof(labels), "iface=%s", iface);

		if (add_sample(batch, "network.rx_bytes", labels, (double)rx_bytes, "ok") != 0 ||
		    add_sample(batch, "network.rx_packets", labels, (double)rx_packets, "ok") != 0 ||
		    add_sample(batch, "network.tx_bytes", labels, (double)tx_bytes, "ok") != 0 ||
		    add_sample(batch, "network.tx_packets", labels, (double)tx_packets, "ok") != 0)
			return -1;
	}

	return 0;
}

static int collect_network_samples(struct edgepulse_sample_batch *batch)
{
	int rc;
	FILE *fp = fopen("/proc/net/dev", "r");

	if (!fp)
		return -1;

	rc = edgepulse_parse_net_dev_stream(fp, batch);
	fclose(fp);
	return rc;
}

int edgepulse_parse_wireless_stream(FILE *fp, struct edgepulse_sample_batch *batch)
{
	char line[256];
	int found = 0;

	if (!fgets(line, sizeof(line), fp) || !fgets(line, sizeof(line), fp))
		return -1;

	while (fgets(line, sizeof(line), fp)) {
		char iface[64];
		char labels[96];
		double status, link, level, noise;

		if (sscanf(line, " %63[^:]: %lf %lf %lf %lf",
			   iface, &status, &link, &level, &noise) != 5)
			continue;

		snprintf(labels, sizeof(labels), "iface=%s", iface);
		if (add_sample(batch, "wireless.link_quality", labels, link, "ok") != 0 ||
		    add_sample(batch, "wireless.signal_dbm", labels, level, "ok") != 0 ||
		    add_sample(batch, "wireless.noise_dbm", labels, noise, "ok") != 0)
			return -1;
		found++;
	}

	return found > 0 ? 0 : -1;
}

static int collect_wireless_samples(struct edgepulse_sample_batch *batch)
{
	int rc;
	FILE *fp = fopen("/proc/net/wireless", "r");

	if (!fp)
		return -1;

	rc = edgepulse_parse_wireless_stream(fp, batch);
	fclose(fp);
	return rc;
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

static int collect_conntrack_samples(struct edgepulse_sample_batch *batch)
{
	unsigned long count;
	FILE *fp = fopen("/proc/sys/net/netfilter/nf_conntrack_count", "r");

	if (!fp)
		return -1;

	if (fscanf(fp, "%lu", &count) != 1) {
		fclose(fp);
		return -1;
	}

	fclose(fp);
	return add_sample(batch, "network.conntrack_count", "", (double)count, "ok");
}

static void trim_trailing_brace(char *value)
{
	size_t len = strlen(value);

	while (len > 0 && (value[len - 1] == '{' || value[len - 1] == ':')) {
		value[len - 1] = '\0';
		len--;
	}
}

int edgepulse_parse_nft_counters_stream(FILE *fp, struct edgepulse_sample_batch *batch)
{
	char line[256];
	char family[32] = "";
	char table[64] = "";
	char counter[64] = "";
	int found = 0;

	while (fgets(line, sizeof(line), fp)) {
		char first[32];
		char second[64];
		unsigned long long packets;
		unsigned long long bytes;
		char labels[160];

		if (sscanf(line, " table %31s %63s", first, second) == 2) {
			snprintf(family, sizeof(family), "%s", first);
			snprintf(table, sizeof(table), "%s", second);
			trim_trailing_brace(table);
			continue;
		}

		if (sscanf(line, " counter %63s", first) == 1) {
			snprintf(counter, sizeof(counter), "%s", first);
			trim_trailing_brace(counter);
			continue;
		}

		if (sscanf(line, " %31s %llu %63s %llu", first, &packets,
			   second, &bytes) != 4)
			continue;
		if (strcmp(first, "packets") != 0 || strcmp(second, "bytes") != 0)
			continue;
		if (family[0] == '\0' || table[0] == '\0' || counter[0] == '\0')
			continue;

		snprintf(labels, sizeof(labels), "family=%s,table=%s,counter=%s",
			 family, table, counter);
		if (add_sample(batch, "nft.counter_packets", labels,
			       (double)packets, "ok") != 0 ||
		    add_sample(batch, "nft.counter_bytes", labels,
			       (double)bytes, "ok") != 0)
			return -1;
		found++;
	}

	return found > 0 ? 0 : -1;
}

static int collect_nft_counter_samples(struct edgepulse_sample_batch *batch)
{
	int pipefd[2];
	pid_t pid;
	int rc;
	FILE *fp;
	int status = 0;

	if (pipe(pipefd) != 0)
		return -1;

	pid = fork();
	if (pid < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		return -1;
	}

	if (pid == 0) {
		int devnull;

		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		close(pipefd[1]);
		devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) {
			dup2(devnull, STDERR_FILENO);
			close(devnull);
		}
		execl("/usr/sbin/nft", "nft", "list", "counters", NULL);
		_exit(127);
	}

	close(pipefd[1]);
	fp = fdopen(pipefd[0], "r");
	if (!fp) {
		close(pipefd[0]);
		waitpid(pid, &status, 0);
		return -1;
	}
	rc = edgepulse_parse_nft_counters_stream(fp, batch);
	fclose(fp);
	if (waitpid(pid, &status, 0) < 0)
		return -1;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return -1;
	return rc;
}

#ifdef EDGEPULSE_ENABLE_UBUS
enum {
	BOARD_KERNEL,
	BOARD_HOSTNAME,
	BOARD_SYSTEM,
	BOARD_MODEL,
	BOARD_RELEASE,
	__BOARD_MAX
};

static const struct blobmsg_policy board_policy[__BOARD_MAX] = {
	[BOARD_KERNEL] = { .name = "kernel", .type = BLOBMSG_TYPE_STRING },
	[BOARD_HOSTNAME] = { .name = "hostname", .type = BLOBMSG_TYPE_STRING },
	[BOARD_SYSTEM] = { .name = "system", .type = BLOBMSG_TYPE_STRING },
	[BOARD_MODEL] = { .name = "model", .type = BLOBMSG_TYPE_STRING },
	[BOARD_RELEASE] = { .name = "release", .type = BLOBMSG_TYPE_TABLE },
};

enum {
	RELEASE_DISTRIBUTION,
	RELEASE_VERSION,
	__RELEASE_MAX
};

static const struct blobmsg_policy release_policy[__RELEASE_MAX] = {
	[RELEASE_DISTRIBUTION] = { .name = "distribution", .type = BLOBMSG_TYPE_STRING },
	[RELEASE_VERSION] = { .name = "version", .type = BLOBMSG_TYPE_STRING },
};

struct ubus_metadata_context {
	const char *db_path;
	int stored;
};

static void board_cb(struct ubus_request *req, int type, struct blob_attr *msg)
{
	struct ubus_metadata_context *ctx = req->priv;
	struct blob_attr *tb[__BOARD_MAX];
	struct blob_attr *release[__RELEASE_MAX];

	(void)type;
	blobmsg_parse(board_policy, __BOARD_MAX, tb, blob_data(msg), blob_len(msg));

	if (tb[BOARD_KERNEL]) {
		edgepulse_store_metadata(ctx->db_path, "board.kernel",
					 blobmsg_get_string(tb[BOARD_KERNEL]));
		ctx->stored++;
	}
	if (tb[BOARD_HOSTNAME]) {
		edgepulse_store_metadata(ctx->db_path, "board.hostname",
					 blobmsg_get_string(tb[BOARD_HOSTNAME]));
		ctx->stored++;
	}
	if (tb[BOARD_SYSTEM]) {
		edgepulse_store_metadata(ctx->db_path, "board.system",
					 blobmsg_get_string(tb[BOARD_SYSTEM]));
		ctx->stored++;
	}
	if (tb[BOARD_MODEL]) {
		edgepulse_store_metadata(ctx->db_path, "board.model",
					 blobmsg_get_string(tb[BOARD_MODEL]));
		ctx->stored++;
	}
	if (tb[BOARD_RELEASE]) {
		blobmsg_parse(release_policy, __RELEASE_MAX, release,
			      blobmsg_data(tb[BOARD_RELEASE]), blobmsg_data_len(tb[BOARD_RELEASE]));
		if (release[RELEASE_DISTRIBUTION])
			edgepulse_store_metadata(ctx->db_path, "board.release_distribution",
						 blobmsg_get_string(release[RELEASE_DISTRIBUTION]));
		if (release[RELEASE_VERSION])
			edgepulse_store_metadata(ctx->db_path, "board.release_version",
						 blobmsg_get_string(release[RELEASE_VERSION]));
		ctx->stored++;
	}
}

enum {
	IFACE_INTERFACE,
	IFACE_UP,
	IFACE_DEVICE,
	IFACE_L3_DEVICE,
	__IFACE_MAX
};

static const struct blobmsg_policy iface_policy[__IFACE_MAX] = {
	[IFACE_INTERFACE] = { .name = "interface", .type = BLOBMSG_TYPE_STRING },
	[IFACE_UP] = { .name = "up", .type = BLOBMSG_TYPE_BOOL },
	[IFACE_DEVICE] = { .name = "device", .type = BLOBMSG_TYPE_STRING },
	[IFACE_L3_DEVICE] = { .name = "l3_device", .type = BLOBMSG_TYPE_STRING },
};

enum {
	DUMP_INTERFACE,
	__DUMP_MAX
};

static const struct blobmsg_policy dump_policy[__DUMP_MAX] = {
	[DUMP_INTERFACE] = { .name = "interface", .type = BLOBMSG_TYPE_ARRAY },
};

static void network_dump_cb(struct ubus_request *req, int type, struct blob_attr *msg)
{
	struct edgepulse_sample_batch *batch = req->priv;
	struct blob_attr *tb[__DUMP_MAX];
	struct blob_attr *cur;
	int rem;

	(void)type;
	blobmsg_parse(dump_policy, __DUMP_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[DUMP_INTERFACE])
		return;

	blobmsg_for_each_attr(cur, tb[DUMP_INTERFACE], rem) {
		struct blob_attr *iface[__IFACE_MAX];
		const char *logical;
		const char *device = NULL;
		char labels[96];

		blobmsg_parse(iface_policy, __IFACE_MAX, iface,
			      blobmsg_data(cur), blobmsg_data_len(cur));
		if (!iface[IFACE_INTERFACE])
			continue;

		logical = blobmsg_get_string(iface[IFACE_INTERFACE]);
		if (iface[IFACE_L3_DEVICE])
			device = blobmsg_get_string(iface[IFACE_L3_DEVICE]);
		else if (iface[IFACE_DEVICE])
			device = blobmsg_get_string(iface[IFACE_DEVICE]);

		if (device)
			add_interface_map(logical, device);

		snprintf(labels, sizeof(labels), "logical=%s", logical);
		if (iface[IFACE_UP])
			add_sample(batch, "network.interface_up", labels,
				   blobmsg_get_bool(iface[IFACE_UP]) ? 1.0 : 0.0, "ok");
	}
}

static int collect_ubus_samples_and_metadata(const char *db_path,
					     struct edgepulse_sample_batch *batch)
{
	struct ubus_context *ctx;
	struct ubus_metadata_context metadata = { .db_path = db_path, .stored = 0 };
	uint32_t id;
	int rc = -1;

	ctx = ubus_connect(NULL);
	if (!ctx)
		return -1;

	if (ubus_lookup_id(ctx, "system", &id) == 0)
		ubus_invoke(ctx, id, "board", NULL, board_cb, &metadata, 3000);

	if (ubus_lookup_id(ctx, "network.interface", &id) == 0 &&
	    ubus_invoke(ctx, id, "dump", NULL, network_dump_cb, batch, 3000) == 0)
		rc = 0;

	ubus_free(ctx);
	return rc == 0 || metadata.stored > 0 ? 0 : -1;
}
#else
static int collect_ubus_samples_and_metadata(const char *db_path,
					     struct edgepulse_sample_batch *batch)
{
	(void)db_path;
	(void)batch;
	return -1;
}
#endif

int edgepulse_collect_sample_batch(struct edgepulse_sample_batch *batch)
{
	struct edgepulse_snapshot snapshot;

	memset(batch, 0, sizeof(*batch));
	batch->timestamp = time(NULL);
	clear_interface_maps();

	if (collect_ubus_samples_and_metadata(active_db_path, batch) != 0)
		add_sample(batch, "collector.ubus", "", 0.0, "unavailable");

	if (edgepulse_collect_snapshot(&snapshot) == 0)
		add_snapshot_samples(batch, &snapshot);
	else
		add_sample(batch, "collector.snapshot", "", 0.0, "error");

	if (collect_cpu_samples(batch) != 0)
		add_sample(batch, "collector.cpu", "", 0.0, "error");
	if (collect_network_samples(batch) != 0)
		add_sample(batch, "collector.network", "", 0.0, "error");
	if (collect_wireless_samples(batch) != 0)
		add_sample(batch, "collector.wireless", "", 0.0, "unavailable");
	if (collect_thermal_samples(batch) != 0)
		add_sample(batch, "collector.thermal", "", 0.0, "unavailable");
	if (collect_conntrack_samples(batch) != 0)
		add_sample(batch, "collector.conntrack", "", 0.0, "unavailable");
	if (collect_nft_counter_samples(batch) != 0)
		add_sample(batch, "collector.nft", "", 0.0, "unavailable");

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
		"ON raw_samples(metric, labels, timestamp);"
		"CREATE TABLE IF NOT EXISTS feature_rows ("
		"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"  window_sec INTEGER NOT NULL,"
		"  window_start INTEGER NOT NULL,"
		"  window_end INTEGER NOT NULL,"
		"  metric TEXT NOT NULL,"
		"  labels TEXT NOT NULL DEFAULT '',"
		"  count INTEGER NOT NULL,"
		"  mean REAL NOT NULL,"
		"  min REAL NOT NULL,"
		"  max REAL NOT NULL,"
		"  stddev REAL NOT NULL,"
		"  delta REAL NOT NULL,"
		"  rate_per_sec REAL NOT NULL,"
		"  coefficient_of_variation REAL NOT NULL"
		");"
		"CREATE UNIQUE INDEX IF NOT EXISTS idx_feature_rows_unique "
		"ON feature_rows(window_sec, window_start, window_end, metric, labels);"
		"CREATE TABLE IF NOT EXISTS device_metadata ("
		"  key TEXT PRIMARY KEY,"
		"  value TEXT NOT NULL,"
		"  updated_at INTEGER NOT NULL"
		");"
		"CREATE TABLE IF NOT EXISTS agent_memory ("
		"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"  created_at INTEGER NOT NULL,"
		"  source TEXT NOT NULL,"
		"  sensitivity TEXT NOT NULL DEFAULT 'normal',"
		"  ttl_sec INTEGER NOT NULL DEFAULT 0,"
		"  content TEXT NOT NULL"
		");"
		"CREATE INDEX IF NOT EXISTS idx_agent_memory_created "
		"ON agent_memory(created_at);"
		"CREATE TABLE IF NOT EXISTS agent_audit_log ("
		"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"  created_at INTEGER NOT NULL,"
		"  request_id TEXT NOT NULL,"
		"  event_type TEXT NOT NULL,"
		"  detail TEXT NOT NULL"
		");"
		"CREATE INDEX IF NOT EXISTS idx_agent_audit_request "
		"ON agent_audit_log(request_id, created_at);"
		"CREATE TABLE IF NOT EXISTS agent_requests ("
		"  request_id TEXT PRIMARY KEY,"
		"  created_at INTEGER NOT NULL,"
		"  status TEXT NOT NULL,"
		"  question TEXT NOT NULL,"
		"  model_status TEXT NOT NULL,"
		"  answer TEXT NOT NULL"
		");"
		"CREATE TABLE IF NOT EXISTS agent_conversations ("
		"  conversation_id TEXT PRIMARY KEY,"
		"  created_at INTEGER NOT NULL,"
		"  updated_at INTEGER NOT NULL,"
		"  title TEXT NOT NULL DEFAULT ''"
		");"
		"CREATE TABLE IF NOT EXISTS agent_messages ("
		"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"  conversation_id TEXT NOT NULL,"
		"  request_id TEXT NOT NULL,"
		"  created_at INTEGER NOT NULL,"
		"  role TEXT NOT NULL,"
		"  content TEXT NOT NULL,"
		"  model_status TEXT NOT NULL DEFAULT '',"
		"  FOREIGN KEY(conversation_id) REFERENCES agent_conversations(conversation_id)"
		");"
		"CREATE INDEX IF NOT EXISTS idx_agent_messages_conversation "
		"ON agent_messages(conversation_id, created_at, id);";
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
	if (rc == SQLITE_OK) {
		char hostname[128] = "local";
		sqlite3_stmt *stmt = NULL;

		if (gethostname(hostname, sizeof(hostname)) != 0 || hostname[0] == '\0')
			snprintf(hostname, sizeof(hostname), "%s", "local");
		hostname[sizeof(hostname) - 1] = '\0';

		if (sqlite3_prepare_v2(db,
				       "INSERT INTO device_metadata(key, value, updated_at) "
				       "VALUES('hostname', ?, strftime('%s','now')) "
				       "ON CONFLICT(key) DO UPDATE SET "
				       "value=excluded.value, updated_at=excluded.updated_at;",
				       -1, &stmt, NULL) == SQLITE_OK) {
			sqlite3_bind_text(stmt, 1, hostname, -1, SQLITE_TRANSIENT);
			if (sqlite3_step(stmt) != SQLITE_DONE)
				rc = SQLITE_ERROR;
			sqlite3_finalize(stmt);
		} else {
			rc = SQLITE_ERROR;
		}
	}
	if (errmsg)
		sqlite3_free(errmsg);
	sqlite3_close(db);
	return rc == SQLITE_OK ? 0 : -1;
}

int edgepulse_store_metadata(const char *db_path, const char *key, const char *value)
{
	static const char *sql =
		"INSERT INTO device_metadata(key, value, updated_at) "
		"VALUES(?, ?, strftime('%s','now')) "
		"ON CONFLICT(key) DO UPDATE SET "
		"value=excluded.value, updated_at=excluded.updated_at;";
	sqlite3 *db = NULL;
	sqlite3_stmt *stmt = NULL;
	int rc;

	if (!key || !*key || !value)
		return -1;
	if (sqlite3_open(db_path, &db) != SQLITE_OK)
		return -1;
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
		sqlite3_close(db);
		return -1;
	}

	sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, value, -1, SQLITE_STATIC);
	rc = sqlite3_step(stmt);

	sqlite3_finalize(stmt);
	sqlite3_close(db);
	return rc == SQLITE_DONE ? 0 : -1;
}

static void compute_feature_math(struct edgepulse_feature *feature,
				 double avg_square, double first_value,
				 double last_value, time_t first_ts,
				 time_t last_ts)
{
	double variance = avg_square - (feature->mean * feature->mean);
	double elapsed = difftime(last_ts, first_ts);

	if (variance < 0.0 && variance > -0.000001)
		variance = 0.0;

	feature->stddev = variance > 0.0 ? sqrt(variance) : 0.0;
	feature->delta = last_value - first_value;
	feature->rate_per_sec = elapsed > 0.0 ? feature->delta / elapsed : 0.0;
	feature->coefficient_of_variation =
		feature->mean != 0.0 ? feature->stddev / fabs(feature->mean) : 0.0;
}

int edgepulse_store_feature_window(const char *db_path, int window_sec)
{
	static const char *select_sql =
		"WITH grouped AS ("
		"  SELECT metric, labels, count(*) AS sample_count, avg(value) AS mean_value,"
		"         min(value) AS min_value, max(value) AS max_value,"
		"         avg(value * value) AS avg_square,"
		"         min(timestamp) AS first_ts, max(timestamp) AS last_ts "
		"  FROM raw_samples "
		"  WHERE status = 'ok' AND timestamp >= ? AND timestamp <= ? "
		"  GROUP BY metric, labels"
		") "
		"SELECT g.metric, g.labels, g.sample_count, g.mean_value, g.min_value,"
		"       g.max_value, g.avg_square, g.first_ts, g.last_ts,"
		"       (SELECT value FROM raw_samples r "
		"        WHERE r.status = 'ok' AND r.metric = g.metric AND r.labels = g.labels "
		"          AND r.timestamp >= ? AND r.timestamp <= ? "
		"        ORDER BY r.timestamp ASC, r.id ASC LIMIT 1) AS first_value,"
		"       (SELECT value FROM raw_samples r "
		"        WHERE r.status = 'ok' AND r.metric = g.metric AND r.labels = g.labels "
		"          AND r.timestamp >= ? AND r.timestamp <= ? "
		"        ORDER BY r.timestamp DESC, r.id DESC LIMIT 1) AS last_value "
		"FROM grouped g ORDER BY g.metric, g.labels;";
	static const char *insert_sql =
		"INSERT INTO feature_rows("
		"  window_sec, window_start, window_end, metric, labels, count,"
		"  mean, min, max, stddev, delta, rate_per_sec, coefficient_of_variation"
		") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
		"ON CONFLICT(window_sec, window_start, window_end, metric, labels) "
		"DO UPDATE SET "
		"  count=excluded.count, mean=excluded.mean, min=excluded.min,"
		"  max=excluded.max, stddev=excluded.stddev, delta=excluded.delta,"
		"  rate_per_sec=excluded.rate_per_sec,"
		"  coefficient_of_variation=excluded.coefficient_of_variation;";
	sqlite3 *db = NULL;
	sqlite3_stmt *select_stmt = NULL;
	sqlite3_stmt *insert_stmt = NULL;
	time_t window_end = time(NULL);
	time_t window_start = window_end - window_sec;
	int rc;

	if (window_sec <= 0)
		return -1;
	if (edgepulse_init_database(db_path) != 0)
		return -1;
	if (sqlite3_open(db_path, &db) != SQLITE_OK)
		goto fail;
	if (sqlite3_exec(db, "BEGIN IMMEDIATE;", NULL, NULL, NULL) != SQLITE_OK)
		goto fail;
	if (sqlite3_prepare_v2(db, select_sql, -1, &select_stmt, NULL) != SQLITE_OK)
		goto fail;
	if (sqlite3_prepare_v2(db, insert_sql, -1, &insert_stmt, NULL) != SQLITE_OK)
		goto fail;

	sqlite3_bind_int64(select_stmt, 1, (sqlite3_int64)window_start);
	sqlite3_bind_int64(select_stmt, 2, (sqlite3_int64)window_end);
	sqlite3_bind_int64(select_stmt, 3, (sqlite3_int64)window_start);
	sqlite3_bind_int64(select_stmt, 4, (sqlite3_int64)window_end);
	sqlite3_bind_int64(select_stmt, 5, (sqlite3_int64)window_start);
	sqlite3_bind_int64(select_stmt, 6, (sqlite3_int64)window_end);

	while ((rc = sqlite3_step(select_stmt)) == SQLITE_ROW) {
		struct edgepulse_feature feature;
		double avg_square = sqlite3_column_double(select_stmt, 6);
		time_t first_ts = (time_t)sqlite3_column_int64(select_stmt, 7);
		time_t last_ts = (time_t)sqlite3_column_int64(select_stmt, 8);
		double first_value = sqlite3_column_double(select_stmt, 9);
		double last_value = sqlite3_column_double(select_stmt, 10);

		memset(&feature, 0, sizeof(feature));
		snprintf(feature.metric, sizeof(feature.metric), "%s",
			 sqlite3_column_text(select_stmt, 0));
		snprintf(feature.labels, sizeof(feature.labels), "%s",
			 sqlite3_column_text(select_stmt, 1));
		feature.window_sec = window_sec;
		feature.window_start = window_start;
		feature.window_end = window_end;
		feature.count = sqlite3_column_int(select_stmt, 2);
		feature.mean = sqlite3_column_double(select_stmt, 3);
		feature.min = sqlite3_column_double(select_stmt, 4);
		feature.max = sqlite3_column_double(select_stmt, 5);
		compute_feature_math(&feature, avg_square, first_value, last_value,
				     first_ts, last_ts);

		sqlite3_bind_int(insert_stmt, 1, feature.window_sec);
		sqlite3_bind_int64(insert_stmt, 2, (sqlite3_int64)feature.window_start);
		sqlite3_bind_int64(insert_stmt, 3, (sqlite3_int64)feature.window_end);
		sqlite3_bind_text(insert_stmt, 4, feature.metric, -1, SQLITE_STATIC);
		sqlite3_bind_text(insert_stmt, 5, feature.labels, -1, SQLITE_STATIC);
		sqlite3_bind_int(insert_stmt, 6, feature.count);
		sqlite3_bind_double(insert_stmt, 7, feature.mean);
		sqlite3_bind_double(insert_stmt, 8, feature.min);
		sqlite3_bind_double(insert_stmt, 9, feature.max);
		sqlite3_bind_double(insert_stmt, 10, feature.stddev);
		sqlite3_bind_double(insert_stmt, 11, feature.delta);
		sqlite3_bind_double(insert_stmt, 12, feature.rate_per_sec);
		sqlite3_bind_double(insert_stmt, 13, feature.coefficient_of_variation);

		if (sqlite3_step(insert_stmt) != SQLITE_DONE)
			goto fail;
		sqlite3_reset(insert_stmt);
		sqlite3_clear_bindings(insert_stmt);
	}

	if (rc != SQLITE_DONE)
		goto fail;
	if (sqlite3_finalize(select_stmt) != SQLITE_OK) {
		select_stmt = NULL;
		goto fail;
	}
	select_stmt = NULL;
	if (sqlite3_finalize(insert_stmt) != SQLITE_OK) {
		insert_stmt = NULL;
		goto fail;
	}
	insert_stmt = NULL;
	if (sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK)
		goto fail;

	sqlite3_close(db);
	return 0;

fail:
	if (select_stmt)
		sqlite3_finalize(select_stmt);
	if (insert_stmt)
		sqlite3_finalize(insert_stmt);
	if (db) {
		sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
		sqlite3_close(db);
	}
	return -1;
}

int edgepulse_apply_retention(const char *db_path, int raw_retention_sec,
			      int feature_retention_sec)
{
	static const char *raw_sql =
		"DELETE FROM raw_samples WHERE timestamp < strftime('%s','now') - ?;";
	static const char *feature_sql =
		"DELETE FROM feature_rows WHERE window_end < strftime('%s','now') - ?;";
	sqlite3 *db = NULL;
	sqlite3_stmt *stmt = NULL;
	int rc = SQLITE_OK;

	if (raw_retention_sec <= 0 && feature_retention_sec <= 0)
		return 0;
	if (edgepulse_init_database(db_path) != 0)
		return -1;
	if (sqlite3_open(db_path, &db) != SQLITE_OK)
		return -1;
	if (sqlite3_exec(db, "BEGIN IMMEDIATE;", NULL, NULL, NULL) != SQLITE_OK)
		goto fail;

	if (raw_retention_sec > 0) {
		if (sqlite3_prepare_v2(db, raw_sql, -1, &stmt, NULL) != SQLITE_OK)
			goto fail;
		sqlite3_bind_int(stmt, 1, raw_retention_sec);
		rc = sqlite3_step(stmt);
		sqlite3_finalize(stmt);
		stmt = NULL;
		if (rc != SQLITE_DONE)
			goto fail;
	}

	if (feature_retention_sec > 0) {
		if (sqlite3_prepare_v2(db, feature_sql, -1, &stmt, NULL) != SQLITE_OK)
			goto fail;
		sqlite3_bind_int(stmt, 1, feature_retention_sec);
		rc = sqlite3_step(stmt);
		sqlite3_finalize(stmt);
		stmt = NULL;
		if (rc != SQLITE_DONE)
			goto fail;
	}

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
	static const int feature_windows[] = { 60, 300, 900 };

	if (edgepulse_init_database(db_path) != 0)
		return -1;
	active_db_path = db_path;
	if (edgepulse_write_status_file() != 0)
		return -1;
	if (edgepulse_collect_sample_batch(&batch) != 0)
		return -1;
	if (edgepulse_write_sample_batch(db_path, &batch) != 0)
		return -1;

	for (size_t i = 0; i < sizeof(feature_windows) / sizeof(feature_windows[0]); i++)
		edgepulse_store_feature_window(db_path, feature_windows[i]);

	return 0;
}
