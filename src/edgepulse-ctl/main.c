#include "edgepulse.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sqlite3.h>

static int print_status(void)
{
	struct edgepulse_snapshot snapshot;

	if (edgepulse_collect_snapshot(&snapshot) != 0) {
		fprintf(stderr, "edgepulse-ctl: failed to collect status snapshot\n");
		return 1;
	}

	edgepulse_write_snapshot_json(stdout, &snapshot);
	return 0;
}

static int print_latest_from_status_file(void)
{
	char buffer[4096];
	size_t nread;
	FILE *fp = fopen(EDGEPULSE_STATUS_PATH, "r");

	if (!fp) {
		fprintf(stderr, "edgepulse-ctl: cannot read %s: %s\n",
			EDGEPULSE_STATUS_PATH, strerror(errno));
		return 1;
	}

	while ((nread = fread(buffer, 1, sizeof(buffer), fp)) > 0)
		fwrite(buffer, 1, nread, stdout);

	if (ferror(fp)) {
		fclose(fp);
		return 1;
	}

	fclose(fp);
	return 0;
}

static int print_latest(void)
{
	static const char *sql =
		"SELECT timestamp, metric, labels, value, status "
		"FROM raw_samples "
		"WHERE timestamp = (SELECT max(timestamp) FROM raw_samples) "
		"ORDER BY metric, labels;";
	sqlite3 *db = NULL;
	sqlite3_stmt *stmt = NULL;
	int first = 1;
	int rc;

	if (sqlite3_open(EDGEPULSE_DB_PATH, &db) != SQLITE_OK)
		return print_latest_from_status_file();

	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
		sqlite3_close(db);
		return print_latest_from_status_file();
	}

	printf("{\n");
	printf("  \"samples\": [\n");

	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		if (!first)
			printf(",\n");
		first = 0;
		printf("    { \"timestamp\": %lld, \"metric\": \"%s\", \"labels\": \"%s\", \"value\": %.6f, \"status\": \"%s\" }",
		       sqlite3_column_int64(stmt, 0),
		       sqlite3_column_text(stmt, 1),
		       sqlite3_column_text(stmt, 2),
		       sqlite3_column_double(stmt, 3),
		       sqlite3_column_text(stmt, 4));
	}

	printf("\n");
	printf("  ]\n");
	printf("}\n");

	sqlite3_finalize(stmt);
	sqlite3_close(db);
	return rc == SQLITE_DONE ? 0 : 1;
}

static int parse_duration_arg(const char *value, int *seconds)
{
	char *end = NULL;
	long parsed = strtol(value, &end, 10);

	if (parsed <= 0 || parsed > 86400)
		return -1;

	if (!end || *end == '\0') {
		*seconds = (int)parsed;
		return 0;
	}

	if (end[1] != '\0')
		return -1;

	switch (*end) {
	case 's':
		*seconds = (int)parsed;
		return 0;
	case 'm':
		if (parsed > 1440)
			return -1;
		*seconds = (int)(parsed * 60);
		return 0;
	case 'h':
		if (parsed > 24)
			return -1;
		*seconds = (int)(parsed * 3600);
		return 0;
	default:
		return -1;
	}
}

static int parse_window_arg(int argc, char **argv, int *window_sec)
{
	*window_sec = 60;

	for (int i = 2; i < argc; i++) {
		if (strcmp(argv[i], "--json") == 0)
			continue;

		if (strcmp(argv[i], "--format") == 0) {
			if (i + 1 >= argc || strcmp(argv[i + 1], "csv") != 0)
				return -1;
			i++;
			continue;
		}

		if (strcmp(argv[i], "--window") == 0 && i + 1 < argc) {
			if (parse_duration_arg(argv[i + 1], window_sec) != 0)
				return -1;
			i++;
			continue;
		}

		if (strcmp(argv[i], "--since") == 0 && i + 1 < argc) {
			if (parse_duration_arg(argv[i + 1], window_sec) != 0)
				return -1;
			i++;
			continue;
		}

		return -1;
	}

	return 0;
}

static int print_features(int window_sec)
{
	static const char *sql =
		"SELECT metric, labels, count(*), avg(value), min(value), max(value) "
		"FROM raw_samples "
		"WHERE status = 'ok' AND timestamp >= strftime('%s','now') - ? "
		"GROUP BY metric, labels "
		"ORDER BY metric, labels;";
	sqlite3 *db = NULL;
	sqlite3_stmt *stmt = NULL;
	int first = 1;
	int rc;

	if (sqlite3_open(EDGEPULSE_DB_PATH, &db) != SQLITE_OK)
		goto unavailable;
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
		goto unavailable;

	sqlite3_bind_int(stmt, 1, window_sec);

	printf("{\n");
	printf("  \"window_sec\": %d,\n", window_sec);
	printf("  \"window_start\": %lld,\n", (long long)(time(NULL) - window_sec));
	printf("  \"window_end\": %lld,\n", (long long)time(NULL));
	printf("  \"features\": [\n");

	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		if (!first)
			printf(",\n");
		first = 0;
		printf("    { \"metric\": \"%s\", \"labels\": \"%s\", \"count\": %d, \"mean\": %.6f, \"min\": %.6f, \"max\": %.6f }",
		       sqlite3_column_text(stmt, 0),
		       sqlite3_column_text(stmt, 1),
		       sqlite3_column_int(stmt, 2),
		       sqlite3_column_double(stmt, 3),
		       sqlite3_column_double(stmt, 4),
		       sqlite3_column_double(stmt, 5));
	}

	printf("\n");
	printf("  ]\n");
	printf("}\n");

	sqlite3_finalize(stmt);
	sqlite3_close(db);
	return rc == SQLITE_DONE ? 0 : 1;

unavailable:
	if (stmt)
		sqlite3_finalize(stmt);
	if (db)
		sqlite3_close(db);
	printf("{\n");
	printf("  \"window_sec\": %d,\n", window_sec);
	printf("  \"features\": [],\n");
	printf("  \"status\": \"unavailable\"\n");
	printf("}\n");
	return 0;
}

static int print_export(int window_sec)
{
	static const char *sql =
		"SELECT metric, labels, count(*), avg(value), min(value), max(value) "
		"FROM raw_samples "
		"WHERE status = 'ok' AND timestamp >= strftime('%s','now') - ? "
		"GROUP BY metric, labels "
		"ORDER BY metric, labels;";
	sqlite3 *db = NULL;
	sqlite3_stmt *stmt = NULL;
	char hostname[128] = "local";
	time_t window_end = time(NULL);
	time_t window_start = window_end - window_sec;
	int rc;

	if (gethostname(hostname, sizeof(hostname)) != 0 || hostname[0] == '\0')
		snprintf(hostname, sizeof(hostname), "local");
	hostname[sizeof(hostname) - 1] = '\0';

	printf("device_id,window_sec,window_start,window_end,metric,labels,count,mean,min,max\n");

	if (sqlite3_open(EDGEPULSE_DB_PATH, &db) != SQLITE_OK)
		return 1;
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
		sqlite3_close(db);
		return 1;
	}

	sqlite3_bind_int(stmt, 1, window_sec);

	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		printf("%s,%d,%lld,%lld,%s,%s,%d,%.6f,%.6f,%.6f\n",
		       hostname,
		       window_sec,
		       (long long)window_start,
		       (long long)window_end,
		       sqlite3_column_text(stmt, 0),
		       sqlite3_column_text(stmt, 1),
		       sqlite3_column_int(stmt, 2),
		       sqlite3_column_double(stmt, 3),
		       sqlite3_column_double(stmt, 4),
		       sqlite3_column_double(stmt, 5));
	}

	sqlite3_finalize(stmt);
	sqlite3_close(db);
	return rc == SQLITE_DONE ? 0 : 1;
}

static void print_usage(FILE *fp)
{
	fprintf(fp, "Usage: edgepulse-ctl <status|latest|features|export|version> [--json] [--format csv] [--window seconds|60s|5m|1h] [--since seconds|60s|5m|1h]\n");
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		print_usage(stderr);
		return 2;
	}

	if (strcmp(argv[1], "status") == 0)
		return print_status();

	if (strcmp(argv[1], "latest") == 0)
		return print_latest();

	if (strcmp(argv[1], "features") == 0) {
		int window_sec;

		if (parse_window_arg(argc, argv, &window_sec) != 0) {
			print_usage(stderr);
			return 2;
		}

		return print_features(window_sec);
	}

	if (strcmp(argv[1], "export") == 0) {
		int window_sec;

		if (parse_window_arg(argc, argv, &window_sec) != 0) {
			print_usage(stderr);
			return 2;
		}

		return print_export(window_sec);
	}

	if (strcmp(argv[1], "version") == 0) {
		printf("%s\n", EDGEPULSE_VERSION);
		return 0;
	}

	print_usage(stderr);
	return 2;
}
