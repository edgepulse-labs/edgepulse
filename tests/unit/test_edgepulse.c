#include "edgepulse.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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

int main(void)
{
	test_parse_positive_int();
	test_memory_used_ratio();
	test_collect_snapshot();

	if (failures != 0) {
		fprintf(stderr, "%d unit test(s) failed\n", failures);
		return 1;
	}

	puts("edgepulse unit tests passed");
	return 0;
}
