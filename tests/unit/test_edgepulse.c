#include "edgepulse.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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

int main(void)
{
	test_parse_positive_int();
	test_memory_used_ratio();
	test_collect_snapshot();
	test_collect_sample_batch();
	test_database_write();

	if (failures != 0) {
		fprintf(stderr, "%d unit test(s) failed\n", failures);
		return 1;
	}

	puts("edgepulse unit tests passed");
	return 0;
}
