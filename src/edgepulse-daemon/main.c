#include "edgepulse.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifdef EDGEPULSE_ENABLE_AI_AGENT
#include <sqlite3.h>
#endif

static volatile sig_atomic_t keep_running = 1;

static void handle_signal(int signo)
{
	(void)signo;
	keep_running = 0;
}

static int print_status(void)
{
	struct edgepulse_snapshot snapshot;

	if (edgepulse_collect_snapshot(&snapshot) != 0) {
		fprintf(stderr, "edgepulse: failed to collect status snapshot\n");
		return 1;
	}

	edgepulse_write_snapshot_json(stdout, &snapshot);
	return 0;
}

static int run_daemon(const char *db_path, int interval_sec, int raw_retention_sec,
		      int feature_retention_sec)
{
	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);

	while (keep_running) {
		if (edgepulse_write_status_outputs(db_path) != 0) {
			fprintf(stderr, "edgepulse: failed to write telemetry outputs: %s\n",
				strerror(errno));
		}
		if (edgepulse_apply_retention(db_path, raw_retention_sec,
					      feature_retention_sec) != 0) {
			fprintf(stderr, "edgepulse: failed to apply retention cleanup: %s\n",
				strerror(errno));
		}

		for (int i = 0; keep_running && i < interval_sec; i++)
			sleep(1);
	}

	return 0;
}

#ifdef EDGEPULSE_ENABLE_AI_AGENT
static int store_agent_audit(const char *db_path, const char *event_type,
			     const char *detail)
{
	sqlite3 *db = NULL;
	sqlite3_stmt *stmt = NULL;
	int rc;

	if (edgepulse_init_database(db_path) != 0)
		return -1;

	rc = sqlite3_open(db_path, &db);
	if (rc != SQLITE_OK) {
		if (db)
			sqlite3_close(db);
		return -1;
	}

	rc = sqlite3_prepare_v2(db,
				"INSERT INTO agent_audit_log(created_at, request_id, event_type, detail) "
				"VALUES(strftime('%s','now'), 'agentd', ?, ?);",
				-1, &stmt, NULL);
	if (rc == SQLITE_OK) {
		sqlite3_bind_text(stmt, 1, event_type, -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt, 2, detail, -1, SQLITE_TRANSIENT);
		rc = sqlite3_step(stmt);
	}

	sqlite3_finalize(stmt);
	sqlite3_close(db);
	return rc == SQLITE_DONE ? 0 : -1;
}

static int run_agent_daemon(const char *db_path, int heartbeat_interval_sec)
{
	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);

	if (store_agent_audit(db_path, "agentd.started",
			      "EdgePulse AI agent runtime monitor started") != 0) {
		fprintf(stderr, "edgepulse: failed to initialize agent runtime state: %s\n",
			strerror(errno));
		return 1;
	}

	while (keep_running) {
		if (store_agent_audit(db_path, "agentd.heartbeat",
				      "EdgePulse AI agent runtime monitor heartbeat") != 0) {
			fprintf(stderr, "edgepulse: failed to write agent heartbeat: %s\n",
				strerror(errno));
		}

		for (int i = 0; keep_running && i < heartbeat_interval_sec; i++)
			sleep(1);
	}

	if (store_agent_audit(db_path, "agentd.stopped",
			      "EdgePulse AI agent runtime monitor stopped") != 0) {
		fprintf(stderr, "edgepulse: failed to write agent stop audit: %s\n",
			strerror(errno));
	}

	return 0;
}
#endif

static void print_usage(FILE *fp)
{
	fprintf(fp, "Usage:\n");
	fprintf(fp, "  edgepulse status\n");
	fprintf(fp, "  edgepulse daemon [interval_sec] [db_path] [raw_retention_sec] [feature_retention_sec]\n");
	fprintf(fp, "  edgepulse agent [db_path] [heartbeat_interval_sec]\n");
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
		int raw_retention_sec = 3600;
		int feature_retention_sec = 86400;
		const char *db_path = EDGEPULSE_DB_PATH;

		if (argc >= 3) {
			if (edgepulse_parse_positive_int(argv[2], &interval_sec) != 0) {
				fprintf(stderr, "edgepulse: invalid interval: %s\n", argv[2]);
				return 2;
			}
		}
		if (argc >= 4 && argv[3][0] != '\0')
			db_path = argv[3];
		if (argc >= 5) {
			if (edgepulse_parse_positive_int(argv[4], &raw_retention_sec) != 0) {
				fprintf(stderr, "edgepulse: invalid raw retention: %s\n", argv[4]);
				return 2;
			}
		}
		if (argc >= 6) {
			if (edgepulse_parse_positive_int(argv[5], &feature_retention_sec) != 0) {
				fprintf(stderr, "edgepulse: invalid feature retention: %s\n", argv[5]);
				return 2;
			}
		}

		return run_daemon(db_path, interval_sec, raw_retention_sec,
				  feature_retention_sec);
	}

	if (strcmp(argv[1], "agent") == 0) {
#ifndef EDGEPULSE_ENABLE_AI_AGENT
		fprintf(stderr, "edgepulse: AI agent support was not included in this package build\n");
		return 0;
#else
		int heartbeat_interval_sec = 60;
		const char *db_path = EDGEPULSE_DB_PATH;

		if (argc >= 3 && argv[2][0] != '\0')
			db_path = argv[2];
		if (argc >= 4) {
			if (edgepulse_parse_positive_int(argv[3], &heartbeat_interval_sec) != 0) {
				fprintf(stderr, "edgepulse: invalid agent heartbeat interval: %s\n",
					argv[3]);
				return 2;
			}
		}

		return run_agent_daemon(db_path, heartbeat_interval_sec);
#endif
	}

	print_usage(stderr);
	return 2;
}
