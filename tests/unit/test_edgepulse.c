#define _GNU_SOURCE

#include "edgepulse.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sqlite3.h>

static int failures;

static void check_int(const char *name, int actual, int expected)
{
	if (actual != expected) {
		fprintf(stderr, "FAIL %s: expected %d, got %d\n", name, expected, actual);
		failures++;
	}
}

static void check_double(const char *name, double actual, double expected)
{
	if (fabs(actual - expected) > 0.0001) {
		fprintf(stderr, "FAIL %s: expected %.4f, got %.4f\n", name, expected, actual);
		failures++;
	}
}

static void test_parse_positive_int(void)
{
	int parsed = 0;

	check_int("parse 5 rc", edgepulse_parse_positive_int("5", &parsed), 0);
	check_int("parse 5 value", parsed, 5);
	check_int("parse zero", edgepulse_parse_positive_int("0", &parsed), -1);
	check_int("parse negative", edgepulse_parse_positive_int("-1", &parsed), -1);
	check_int("parse suffix", edgepulse_parse_positive_int("5s", &parsed), -1);
	check_int("parse empty", edgepulse_parse_positive_int("", &parsed), -1);
	check_int("parse null", edgepulse_parse_positive_int(NULL, &parsed), -1);
}

static void test_memory_used_ratio(void)
{
	struct edgepulse_snapshot snapshot;

	memset(&snapshot, 0, sizeof(snapshot));
	check_double("zero memory ratio", edgepulse_memory_used_ratio(&snapshot), 0.0);

	snapshot.mem_total_kb = 1000;
	snapshot.mem_available_kb = 250;
	check_double("memory ratio", edgepulse_memory_used_ratio(&snapshot), 0.75);
}

static void test_collect_snapshot(void)
{
	struct edgepulse_snapshot snapshot;

	check_int("collect snapshot", edgepulse_collect_snapshot(&snapshot), 0);
	if (snapshot.mem_total_kb == 0) {
		fprintf(stderr, "FAIL collect snapshot: mem_total_kb is zero\n");
		failures++;
	}
}

static void test_collect_sample_batch(void)
{
	struct edgepulse_sample_batch batch;

	check_int("collect sample batch", edgepulse_collect_sample_batch(&batch), 0);
	if (batch.count == 0) {
		fprintf(stderr, "FAIL collect sample batch: count is zero\n");
		failures++;
	}
}

static FILE *open_fixture(const char *data)
{
	return fmemopen((void *)data, strlen(data), "r");
}

static const struct edgepulse_sample *find_sample(const struct edgepulse_sample_batch *batch,
						  const char *metric,
						  const char *labels)
{
	for (size_t i = 0; i < batch->count; i++) {
		if (strcmp(batch->samples[i].metric, metric) == 0 &&
		    strcmp(batch->samples[i].labels, labels) == 0)
			return &batch->samples[i];
	}

	return NULL;
}

static void test_meminfo_fixture(void)
{
	static const char fixture[] =
		"MemTotal:        1000 kB\n"
		"MemFree:          200 kB\n"
		"MemAvailable:     700 kB\n";
	struct edgepulse_snapshot snapshot;
	FILE *fp = open_fixture(fixture);

	memset(&snapshot, 0, sizeof(snapshot));
	check_int("meminfo fixture parse", edgepulse_parse_meminfo_stream(fp, &snapshot), 0);
	check_int("meminfo total", (int)snapshot.mem_total_kb, 1000);
	check_int("meminfo free", (int)snapshot.mem_free_kb, 200);
	check_int("meminfo available", (int)snapshot.mem_available_kb, 700);
	fclose(fp);
}

static void test_proc_stat_fixture(void)
{
	static const char fixture[] = "cpu  10 20 30 40 50 60 70 80 0 0\n";
	struct edgepulse_sample_batch batch;
	const struct edgepulse_sample *sample;
	FILE *fp = open_fixture(fixture);

	memset(&batch, 0, sizeof(batch));
	check_int("proc stat fixture parse", edgepulse_parse_proc_stat_stream(fp, &batch), 0);
	sample = find_sample(&batch, "cpu.system_jiffies", "");
	if (!sample) {
		fprintf(stderr, "FAIL proc stat fixture: missing cpu.system_jiffies\n");
		failures++;
	} else {
		check_double("proc stat system", sample->value, 30.0);
	}
	fclose(fp);
}

static void test_net_dev_fixture(void)
{
	static const char fixture[] =
		"Inter-|   Receive                                                |  Transmit\n"
		" face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
		" eth0: 1234 12 0 0 0 0 0 0 5678 56 0 0 0 0 0 0\n";
	struct edgepulse_sample_batch batch;
	const struct edgepulse_sample *sample;
	FILE *fp = open_fixture(fixture);

	memset(&batch, 0, sizeof(batch));
	check_int("net dev fixture parse", edgepulse_parse_net_dev_stream(fp, &batch), 0);
	sample = find_sample(&batch, "network.tx_bytes", "iface=eth0");
	if (!sample) {
		fprintf(stderr, "FAIL net dev fixture: missing eth0 tx bytes\n");
		failures++;
	} else {
		check_double("net dev tx bytes", sample->value, 5678.0);
	}
	fclose(fp);
}

static void test_wireless_fixture(void)
{
	static const char fixture[] =
		"Inter-| sta-|   Quality        |   Discarded packets               | Missed | WE\n"
		" face | tus | link level noise |  nwid  crypt   frag  retry   misc | beacon | 22\n"
		" wlan0: 0000   42.  -51.  -95.       0      0      0      0      0        0\n";
	struct edgepulse_sample_batch batch;
	const struct edgepulse_sample *sample;
	FILE *fp = open_fixture(fixture);

	memset(&batch, 0, sizeof(batch));
	check_int("wireless fixture parse", edgepulse_parse_wireless_stream(fp, &batch), 0);
	sample = find_sample(&batch, "wireless.signal_dbm", "iface=wlan0");
	if (!sample) {
		fprintf(stderr, "FAIL wireless fixture: missing wlan0 signal\n");
		failures++;
	} else {
		check_double("wireless signal", sample->value, -51.0);
	}
	fclose(fp);
}

static void test_nft_counters_fixture(void)
{
	static const char fixture[] =
		"table inet fw4 {\n"
		"	counter wan_rx {\n"
		"		packets 12 bytes 3456\n"
		"	}\n"
		"	counter lan_tx {\n"
		"		packets 7 bytes 890\n"
		"	}\n"
		"}\n";
	struct edgepulse_sample_batch batch;
	const struct edgepulse_sample *sample;
	FILE *fp = open_fixture(fixture);

	memset(&batch, 0, sizeof(batch));
	check_int("nft counters fixture parse",
		  edgepulse_parse_nft_counters_stream(fp, &batch), 0);
	sample = find_sample(&batch, "nft.counter_bytes",
			     "family=inet,table=fw4,counter=wan_rx");
	if (!sample) {
		fprintf(stderr, "FAIL nft counters fixture: missing wan_rx bytes\n");
		failures++;
	} else {
		check_double("nft counter bytes", sample->value, 3456.0);
	}
	fclose(fp);
}

static void test_database_write(void)
{
	const char *db_path = "/tmp/edgepulse-test.db";
	struct edgepulse_sample_batch batch;

	unlink(db_path);
	memset(&batch, 0, sizeof(batch));
	batch.timestamp = 123;
	batch.count = 1;
	snprintf(batch.samples[0].metric, sizeof(batch.samples[0].metric), "%s", "test.metric");
	snprintf(batch.samples[0].labels, sizeof(batch.samples[0].labels), "%s", "label=value");
	snprintf(batch.samples[0].status, sizeof(batch.samples[0].status), "%s", "ok");
	batch.samples[0].value = 42.5;

	check_int("init database", edgepulse_init_database(db_path), 0);
	check_int("write sample batch", edgepulse_write_sample_batch(db_path, &batch), 0);
	unlink(db_path);
}

static void test_agent_schema_tables(void)
{
	const char *db_path = "/tmp/edgepulse-agent-schema-test.db";
	sqlite3 *db = NULL;
	sqlite3_stmt *stmt = NULL;
	int rc;

	unlink(db_path);
	check_int("agent schema init database", edgepulse_init_database(db_path), 0);
	check_int("agent schema open db", sqlite3_open(db_path, &db), SQLITE_OK);
	check_int("agent schema query prepare",
		  sqlite3_prepare_v2(db,
				     "SELECT count(*) FROM sqlite_master "
				     "WHERE type = 'table' "
				     "AND name IN ('agent_memory', 'agent_audit_log', 'agent_requests');",
				     -1, &stmt, NULL),
		  SQLITE_OK);
	rc = sqlite3_step(stmt);
	check_int("agent schema query row", rc, SQLITE_ROW);
	if (rc == SQLITE_ROW)
		check_int("agent schema table count", sqlite3_column_int(stmt, 0), 3);

	sqlite3_finalize(stmt);
	sqlite3_close(db);
	unlink(db_path);
}

static void test_feature_window_storage(void)
{
	const char *db_path = "/tmp/edgepulse-feature-test.db";
	struct edgepulse_sample_batch batch;
	sqlite3 *db = NULL;
	sqlite3_stmt *stmt = NULL;
	time_t base = time(NULL);
	int rc;

	unlink(db_path);
	check_int("feature init database", edgepulse_init_database(db_path), 0);

	memset(&batch, 0, sizeof(batch));
	batch.timestamp = base - 20;
	batch.count = 1;
	snprintf(batch.samples[0].metric, sizeof(batch.samples[0].metric), "%s", "test.counter");
	snprintf(batch.samples[0].status, sizeof(batch.samples[0].status), "%s", "ok");
	batch.samples[0].value = 10.0;
	check_int("feature write first batch", edgepulse_write_sample_batch(db_path, &batch), 0);

	batch.timestamp = base - 10;
	batch.samples[0].value = 20.0;
	check_int("feature write second batch", edgepulse_write_sample_batch(db_path, &batch), 0);
	check_int("feature store window", edgepulse_store_feature_window(db_path, 60), 0);

	check_int("feature open db", sqlite3_open(db_path, &db), SQLITE_OK);
	check_int("feature query prepare",
		  sqlite3_prepare_v2(db,
				     "SELECT count, mean, min, max, stddev, delta, rate_per_sec "
				     "FROM feature_rows WHERE metric = 'test.counter';",
				     -1, &stmt, NULL),
		  SQLITE_OK);
	rc = sqlite3_step(stmt);
	check_int("feature query row", rc, SQLITE_ROW);
	if (rc == SQLITE_ROW) {
		check_int("feature count", sqlite3_column_int(stmt, 0), 2);
		check_double("feature mean", sqlite3_column_double(stmt, 1), 15.0);
		check_double("feature min", sqlite3_column_double(stmt, 2), 10.0);
		check_double("feature max", sqlite3_column_double(stmt, 3), 20.0);
		check_double("feature stddev", sqlite3_column_double(stmt, 4), 5.0);
		check_double("feature delta", sqlite3_column_double(stmt, 5), 10.0);
		check_double("feature rate", sqlite3_column_double(stmt, 6), 1.0);
	}

	sqlite3_finalize(stmt);
	sqlite3_close(db);
	unlink(db_path);
}

static void test_empty_feature_window_storage(void)
{
	const char *db_path = "/tmp/edgepulse-empty-feature-test.db";
	sqlite3 *db = NULL;
	sqlite3_stmt *stmt = NULL;
	int rc;

	unlink(db_path);
	check_int("empty feature init database", edgepulse_init_database(db_path), 0);
	check_int("empty feature store window", edgepulse_store_feature_window(db_path, 60), 0);
	check_int("empty feature open db", sqlite3_open(db_path, &db), SQLITE_OK);
	check_int("empty feature query prepare",
		  sqlite3_prepare_v2(db, "SELECT count(*) FROM feature_rows;",
				     -1, &stmt, NULL),
		  SQLITE_OK);
	rc = sqlite3_step(stmt);
	check_int("empty feature query row", rc, SQLITE_ROW);
	if (rc == SQLITE_ROW)
		check_int("empty feature row count", sqlite3_column_int(stmt, 0), 0);

	sqlite3_finalize(stmt);
	sqlite3_close(db);
	unlink(db_path);
}

static void test_retention_cleanup(void)
{
	const char *db_path = "/tmp/edgepulse-retention-test.db";
	struct edgepulse_sample_batch batch;
	sqlite3 *db = NULL;
	sqlite3_stmt *stmt = NULL;
	time_t now = time(NULL);
	int rc;

	unlink(db_path);
	check_int("retention init database", edgepulse_init_database(db_path), 0);

	memset(&batch, 0, sizeof(batch));
	batch.count = 1;
	snprintf(batch.samples[0].metric, sizeof(batch.samples[0].metric), "%s",
		 "test.retention");
	snprintf(batch.samples[0].status, sizeof(batch.samples[0].status), "%s", "ok");
	batch.samples[0].value = 1.0;

	batch.timestamp = now - 120;
	check_int("retention write old batch", edgepulse_write_sample_batch(db_path, &batch), 0);
	batch.timestamp = now;
	check_int("retention write fresh batch", edgepulse_write_sample_batch(db_path, &batch), 0);
	check_int("retention apply", edgepulse_apply_retention(db_path, 60, 60), 0);

	check_int("retention open db", sqlite3_open(db_path, &db), SQLITE_OK);
	check_int("retention query prepare",
		  sqlite3_prepare_v2(db, "SELECT count(*) FROM raw_samples;",
				     -1, &stmt, NULL),
		  SQLITE_OK);
	rc = sqlite3_step(stmt);
	check_int("retention query row", rc, SQLITE_ROW);
	if (rc == SQLITE_ROW)
		check_int("retention row count", sqlite3_column_int(stmt, 0), 1);

	sqlite3_finalize(stmt);
	sqlite3_close(db);
	unlink(db_path);
}

int main(void)
{
	test_parse_positive_int();
	test_memory_used_ratio();
	test_meminfo_fixture();
	test_proc_stat_fixture();
	test_net_dev_fixture();
	test_wireless_fixture();
	test_nft_counters_fixture();
	test_collect_snapshot();
	test_collect_sample_batch();
	test_database_write();
	test_agent_schema_tables();
	test_empty_feature_window_storage();
	test_feature_window_storage();
	test_retention_cleanup();

	if (failures != 0) {
		fprintf(stderr, "%d unit test(s) failed\n", failures);
		return 1;
	}

	puts("edgepulse unit tests passed");
	return 0;
}
