#include "edgepulse.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sqlite3.h>

#define AGENT_STR_SIZE 256

#ifndef EDGEPULSE_ENABLE_AI_AGENT
#define EDGEPULSE_AGENT_UNUSED __attribute__((unused))
#else
#define EDGEPULSE_AGENT_UNUSED
#endif

struct agent_config {
	int enabled;
	int local_only;
	int memory_enabled;
	int shell_enabled;
	int ubus_enabled;
	int request_timeout_sec;
	int tool_timeout_sec;
	int max_tool_output_bytes;
	char policy_profile[64];
};

struct agent_model_config {
	int present;
	int enabled;
	int configured;
	int timeout_sec;
	int retry_count;
	char name[64];
	char role[128];
	char base_url[AGENT_STR_SIZE];
	char model[128];
	char api_key[AGENT_STR_SIZE];
	char api_key_env[128];
};

static void json_escape(FILE *fp, const char *value)
{
	const unsigned char *p = (const unsigned char *)value;

	if (!value)
		return;

	for (; *p; p++) {
		switch (*p) {
		case '\\':
			fputs("\\\\", fp);
			break;
		case '"':
			fputs("\\\"", fp);
			break;
		case '\n':
			fputs("\\n", fp);
			break;
		case '\r':
			fputs("\\r", fp);
			break;
		case '\t':
			fputs("\\t", fp);
			break;
		default:
			if (*p < 32)
				fprintf(fp, "\\u%04x", *p);
			else
				fputc(*p, fp);
			break;
		}
	}
}

static void print_json_string(const char *value)
{
	putchar('"');
	json_escape(stdout, value ? value : "");
	putchar('"');
}

static void strip_quotes(char *value)
{
	size_t len;

	if (!value)
		return;

	len = strlen(value);
	if (len >= 2 && ((value[0] == '\'' && value[len - 1] == '\'') ||
			 (value[0] == '"' && value[len - 1] == '"'))) {
		memmove(value, value + 1, len - 2);
		value[len - 2] = '\0';
	}
}

static char *trim_space(char *value)
{
	char *end;

	while (*value == ' ' || *value == '\t' || *value == '\n' || *value == '\r')
		value++;

	end = value + strlen(value);
	while (end > value &&
	       (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r'))
		*--end = '\0';

	return value;
}

static int parse_bool_value(const char *value, int default_value)
{
	if (!value || value[0] == '\0')
		return default_value;
	if (strcmp(value, "1") == 0 || strcmp(value, "true") == 0 ||
	    strcmp(value, "yes") == 0 || strcmp(value, "on") == 0)
		return 1;
	if (strcmp(value, "0") == 0 || strcmp(value, "false") == 0 ||
	    strcmp(value, "no") == 0 || strcmp(value, "off") == 0)
		return 0;
	return default_value;
}

static int parse_int_value(const char *value, int default_value)
{
	int parsed;

	if (edgepulse_parse_positive_int(value, &parsed) == 0)
		return parsed;
	return default_value;
}

static void init_agent_config(struct agent_config *agent,
			      struct agent_model_config *model)
{
	memset(agent, 0, sizeof(*agent));
	memset(model, 0, sizeof(*model));
	agent->enabled = 0;
	agent->local_only = 1;
	agent->memory_enabled = 1;
	agent->shell_enabled = 1;
	agent->ubus_enabled = 1;
	agent->request_timeout_sec = 60;
	agent->tool_timeout_sec = 5;
	agent->max_tool_output_bytes = 8192;
	snprintf(agent->policy_profile, sizeof(agent->policy_profile), "%s", "read_only");
	model->timeout_sec = 30;
	model->retry_count = 1;
#ifdef EDGEPULSE_AI_DEFAULT_BASE_URL
	snprintf(model->base_url, sizeof(model->base_url), "%s",
		 EDGEPULSE_AI_DEFAULT_BASE_URL);
#endif
#ifdef EDGEPULSE_AI_DEFAULT_MODEL
	snprintf(model->model, sizeof(model->model), "%s",
		 EDGEPULSE_AI_DEFAULT_MODEL);
#endif
}

static int read_agent_config(struct agent_config *agent,
			     struct agent_model_config *model)
{
	const char *path = getenv("EDGEPULSE_CONFIG_PATH");
	FILE *fp;
	char line[512];
	char section_type[64] = "";
	char section_name[64] = "";

	init_agent_config(agent, model);
	if (!path || path[0] == '\0')
		path = EDGEPULSE_CONFIG_PATH;

	fp = fopen(path, "r");
	if (!fp)
		return -1;

	while (fgets(line, sizeof(line), fp)) {
		char *text = trim_space(line);
		char *key;
		char *value;

		if (text[0] == '\0' || text[0] == '#')
			continue;

		if (strncmp(text, "config ", 7) == 0) {
			char *cursor = trim_space(text + 7);
			char *name;

			key = strsep(&cursor, " \t");
			name = cursor ? trim_space(cursor) : "";
			strip_quotes(name);
			snprintf(section_type, sizeof(section_type), "%s", key ? key : "");
			snprintf(section_name, sizeof(section_name), "%s", name);

			if (strcmp(section_type, "model") == 0 && !model->present) {
				model->present = 1;
				snprintf(model->name, sizeof(model->name), "%s", section_name);
			}
			continue;
		}

		if (strncmp(text, "option ", 7) != 0)
			continue;

		text = trim_space(text + 7);
		key = strsep(&text, " \t");
		value = text ? trim_space(text) : "";
		strip_quotes(value);
		if (!key)
			continue;

		if (strcmp(section_type, "agent") == 0) {
			if (strcmp(key, "enabled") == 0)
				agent->enabled = parse_bool_value(value, agent->enabled);
			else if (strcmp(key, "local_only") == 0)
				agent->local_only = parse_bool_value(value, agent->local_only);
			else if (strcmp(key, "memory_enabled") == 0)
				agent->memory_enabled = parse_bool_value(value, agent->memory_enabled);
			else if (strcmp(key, "shell_enabled") == 0)
				agent->shell_enabled = parse_bool_value(value, agent->shell_enabled);
			else if (strcmp(key, "ubus_enabled") == 0)
				agent->ubus_enabled = parse_bool_value(value, agent->ubus_enabled);
			else if (strcmp(key, "request_timeout_sec") == 0)
				agent->request_timeout_sec = parse_int_value(value, agent->request_timeout_sec);
			else if (strcmp(key, "tool_timeout_sec") == 0)
				agent->tool_timeout_sec = parse_int_value(value, agent->tool_timeout_sec);
			else if (strcmp(key, "max_tool_output_bytes") == 0)
				agent->max_tool_output_bytes = parse_int_value(value, agent->max_tool_output_bytes);
			else if (strcmp(key, "policy_profile") == 0)
				snprintf(agent->policy_profile, sizeof(agent->policy_profile), "%s", value);
		} else if (strcmp(section_type, "model") == 0 &&
			   strcmp(section_name, model->name) == 0) {
			if (strcmp(key, "enabled") == 0)
				model->enabled = parse_bool_value(value, model->enabled);
			else if (strcmp(key, "role") == 0)
				snprintf(model->role, sizeof(model->role), "%s", value);
			else if (strcmp(key, "base_url") == 0 && value[0] != '\0')
				snprintf(model->base_url, sizeof(model->base_url), "%s", value);
			else if (strcmp(key, "model") == 0 && value[0] != '\0')
				snprintf(model->model, sizeof(model->model), "%s", value);
			else if (strcmp(key, "api_key") == 0 && value[0] != '\0')
				snprintf(model->api_key, sizeof(model->api_key), "%s", value);
			else if (strcmp(key, "api_key_env") == 0)
				snprintf(model->api_key_env, sizeof(model->api_key_env), "%s", value);
			else if (strcmp(key, "timeout_sec") == 0)
				model->timeout_sec = parse_int_value(value, model->timeout_sec);
			else if (strcmp(key, "retry_count") == 0)
				model->retry_count = parse_int_value(value, model->retry_count);
		}
	}

	fclose(fp);
	model->configured = model->enabled && model->base_url[0] != '\0' &&
		model->model[0] != '\0' &&
		(model->api_key[0] != '\0' ||
		 (model->api_key_env[0] != '\0' && getenv(model->api_key_env)));
	return 0;
}

static const char *agent_model_status(const struct agent_config *agent,
				      const struct agent_model_config *model)
{
	if (!agent->enabled)
		return "agent_disabled";
	if (agent->local_only)
		return "local_only";
	if (!model->present)
		return "not_configured";
	if (!model->enabled)
		return "model_disabled";
	if (!model->configured)
		return "missing_credentials_or_model";
	return "configured";
}

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
		"SELECT metric, labels, count, mean, min, max, stddev, delta, "
		"       rate_per_sec, coefficient_of_variation, window_start, window_end "
		"FROM feature_rows "
		"WHERE window_sec = ? "
		"  AND window_end = (SELECT max(window_end) FROM feature_rows WHERE window_sec = ?) "
		"ORDER BY metric, labels;";
	sqlite3 *db = NULL;
	sqlite3_stmt *stmt = NULL;
	int first = 1;
	int rc;
	time_t window_end = time(NULL);
	time_t window_start = window_end - window_sec;

	edgepulse_store_feature_window(EDGEPULSE_DB_PATH, window_sec);

	if (sqlite3_open(EDGEPULSE_DB_PATH, &db) != SQLITE_OK)
		goto unavailable;
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
		goto unavailable;

	sqlite3_bind_int(stmt, 1, window_sec);
	sqlite3_bind_int(stmt, 2, window_sec);

	printf("{\n");
	printf("  \"window_sec\": %d,\n", window_sec);
	printf("  \"window_start\": %lld,\n", (long long)window_start);
	printf("  \"window_end\": %lld,\n", (long long)window_end);
	printf("  \"features\": [\n");

	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		if (!first)
			printf(",\n");
		first = 0;
		printf("    { \"metric\": \"%s\", \"labels\": \"%s\", \"count\": %d, \"mean\": %.6f, \"min\": %.6f, \"max\": %.6f, \"stddev\": %.6f, \"delta\": %.6f, \"rate_per_sec\": %.6f, \"coefficient_of_variation\": %.6f, \"window_start\": %lld, \"window_end\": %lld }",
		       sqlite3_column_text(stmt, 0),
		       sqlite3_column_text(stmt, 1),
		       sqlite3_column_int(stmt, 2),
		       sqlite3_column_double(stmt, 3),
		       sqlite3_column_double(stmt, 4),
		       sqlite3_column_double(stmt, 5),
		       sqlite3_column_double(stmt, 6),
		       sqlite3_column_double(stmt, 7),
		       sqlite3_column_double(stmt, 8),
		       sqlite3_column_double(stmt, 9),
		       sqlite3_column_int64(stmt, 10),
		       sqlite3_column_int64(stmt, 11));
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
		"SELECT metric, labels, count, mean, min, max, stddev, delta, "
		"       rate_per_sec, coefficient_of_variation, window_start, window_end "
		"FROM feature_rows "
		"WHERE window_sec = ? "
		"  AND window_end = (SELECT max(window_end) FROM feature_rows WHERE window_sec = ?) "
		"ORDER BY metric, labels;";
	sqlite3 *db = NULL;
	sqlite3_stmt *stmt = NULL;
	char hostname[128] = "local";
	int rc;

	edgepulse_store_feature_window(EDGEPULSE_DB_PATH, window_sec);

	if (gethostname(hostname, sizeof(hostname)) != 0 || hostname[0] == '\0')
		snprintf(hostname, sizeof(hostname), "local");
	hostname[sizeof(hostname) - 1] = '\0';

	printf("device_id,window_sec,window_start,window_end,metric,labels,count,mean,min,max,stddev,delta,rate_per_sec,coefficient_of_variation\n");

	if (sqlite3_open(EDGEPULSE_DB_PATH, &db) != SQLITE_OK)
		return 1;
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
		sqlite3_close(db);
		return 1;
	}

	sqlite3_bind_int(stmt, 1, window_sec);
	sqlite3_bind_int(stmt, 2, window_sec);

	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		printf("%s,%d,%lld,%lld,%s,%s,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
		       hostname,
		       window_sec,
		       sqlite3_column_int64(stmt, 10),
		       sqlite3_column_int64(stmt, 11),
		       sqlite3_column_text(stmt, 0),
		       sqlite3_column_text(stmt, 1),
		       sqlite3_column_int(stmt, 2),
		       sqlite3_column_double(stmt, 3),
		       sqlite3_column_double(stmt, 4),
		       sqlite3_column_double(stmt, 5),
		       sqlite3_column_double(stmt, 6),
		       sqlite3_column_double(stmt, 7),
		       sqlite3_column_double(stmt, 8),
		       sqlite3_column_double(stmt, 9));
	}

	sqlite3_finalize(stmt);
	sqlite3_close(db);
	return rc == SQLITE_DONE ? 0 : 1;
}

static int EDGEPULSE_AGENT_UNUSED print_agent_status(void)
{
	struct agent_config agent;
	struct agent_model_config model;
	int config_rc = read_agent_config(&agent, &model);
	const char *status = agent_model_status(&agent, &model);

	printf("{\n");
	printf("  \"agent\": {\n");
	printf("    \"enabled\": %s,\n", agent.enabled ? "true" : "false");
	printf("    \"local_only\": %s,\n", agent.local_only ? "true" : "false");
	printf("    \"memory_enabled\": %s,\n", agent.memory_enabled ? "true" : "false");
	printf("    \"shell_enabled\": %s,\n", agent.shell_enabled ? "true" : "false");
	printf("    \"ubus_enabled\": %s,\n", agent.ubus_enabled ? "true" : "false");
	printf("    \"policy_profile\": ");
	print_json_string(agent.policy_profile);
	printf(",\n");
	printf("    \"request_timeout_sec\": %d,\n", agent.request_timeout_sec);
	printf("    \"tool_timeout_sec\": %d,\n", agent.tool_timeout_sec);
	printf("    \"max_tool_output_bytes\": %d\n", agent.max_tool_output_bytes);
	printf("  },\n");
	printf("  \"model\": {\n");
	printf("    \"present\": %s,\n", model.present ? "true" : "false");
	printf("    \"enabled\": %s,\n", model.enabled ? "true" : "false");
	printf("    \"configured\": %s,\n", model.configured ? "true" : "false");
	printf("    \"name\": ");
	print_json_string(model.name);
	printf(",\n");
	printf("    \"role\": ");
	print_json_string(model.role);
	printf(",\n");
	printf("    \"base_url\": ");
	print_json_string(model.base_url);
	printf(",\n");
	printf("    \"model\": ");
	print_json_string(model.model);
	printf(",\n");
	printf("    \"api_key_source_configured\": %s,\n",
	       (model.api_key[0] != '\0' || model.api_key_env[0] != '\0') ? "true" : "false");
	printf("    \"api_key_available\": %s,\n",
	       (model.api_key[0] != '\0' ||
		(model.api_key_env[0] != '\0' && getenv(model.api_key_env))) ? "true" : "false");
	printf("    \"api_key_env\": ");
	print_json_string(model.api_key_env);
	printf(",\n");
	printf("    \"timeout_sec\": %d,\n", model.timeout_sec);
	printf("    \"retry_count\": %d\n", model.retry_count);
	printf("  },\n");
	printf("  \"status\": ");
	print_json_string(status);
	printf(",\n");
	printf("  \"config_status\": ");
	print_json_string(config_rc == 0 ? "loaded" : "defaults");
	printf("\n");
	printf("}\n");

	return 0;
}

static void print_agent_findings(const struct edgepulse_snapshot *snapshot,
				 const struct agent_config *agent)
{
	double memory_ratio = edgepulse_memory_used_ratio(snapshot);
	int first = 1;

	printf("  \"findings\": [\n");

	if (snapshot->load1 > 2.0) {
		printf("    { \"severity\": \"warning\", \"source\": \"edgepulse_snapshot\", \"message\": \"1-minute load is elevated at %.2f\" }",
		       snapshot->load1);
		first = 0;
	}

	if (memory_ratio > 0.85) {
		if (!first)
			printf(",\n");
		printf("    { \"severity\": \"warning\", \"source\": \"edgepulse_snapshot\", \"message\": \"Memory usage is high at %.2f%%\" }",
		       memory_ratio * 100.0);
		first = 0;
	}

	if (!agent->shell_enabled) {
		if (!first)
			printf(",\n");
		printf("    { \"severity\": \"info\", \"source\": \"policy\", \"message\": \"Shell tool execution is disabled by policy\" }");
		first = 0;
	}

	if (!agent->ubus_enabled) {
		if (!first)
			printf(",\n");
		printf("    { \"severity\": \"info\", \"source\": \"policy\", \"message\": \"ubus tool execution is disabled by policy\" }");
		first = 0;
	}

	if (first)
		printf("    { \"severity\": \"ok\", \"source\": \"edgepulse_snapshot\", \"message\": \"No obvious CPU or memory pressure detected in the current snapshot\" }");

	printf("\n");
	printf("  ],\n");
}

static int EDGEPULSE_AGENT_UNUSED print_agent_diagnose(const char *question)
{
	struct agent_config agent;
	struct agent_model_config model;
	struct edgepulse_snapshot snapshot;
	const char *model_status;
	double memory_ratio;

	read_agent_config(&agent, &model);
	model_status = agent_model_status(&agent, &model);

	if (!agent.enabled) {
		printf("{\n");
		printf("  \"status\": \"disabled\",\n");
		printf("  \"answer\": \"EdgePulse AI agent is installed but disabled. Enable edgepulse.agent.enabled before running diagnostics.\"\n");
		printf("}\n");
		return 0;
	}

	if (edgepulse_collect_snapshot(&snapshot) != 0) {
		fprintf(stderr, "edgepulse-ctl: failed to collect agent diagnostic snapshot\n");
		return 1;
	}

	memory_ratio = edgepulse_memory_used_ratio(&snapshot);

	printf("{\n");
	printf("  \"status\": \"ok\",\n");
	printf("  \"mode\": \"read_only_diagnostic\",\n");
	printf("  \"question\": ");
	print_json_string(question ? question : "");
	printf(",\n");
	printf("  \"model_status\": ");
	print_json_string(model_status);
	printf(",\n");
	printf("  \"policy_profile\": ");
	print_json_string(agent.policy_profile);
	printf(",\n");
	printf("  \"tools\": [\n");
	printf("    { \"name\": \"edgepulse_snapshot\", \"mode\": \"read_only\", \"status\": \"ok\" }\n");
	printf("  ],\n");
	print_agent_findings(&snapshot, &agent);
	printf("  \"snapshot\": {\n");
	printf("    \"uptime_sec\": %.2f,\n", snapshot.uptime_sec);
	printf("    \"load1\": %.2f,\n", snapshot.load1);
	printf("    \"load5\": %.2f,\n", snapshot.load5);
	printf("    \"load15\": %.2f,\n", snapshot.load15);
	printf("    \"memory_used_ratio\": %.4f\n", memory_ratio);
	printf("  },\n");
	printf("  \"answer\": ");
	if (strcmp(model_status, "configured") == 0) {
		print_json_string("The remote model backend is configured, but this MVP keeps execution local and read-only. The local telemetry snapshot is included for grounded diagnostics.");
	} else {
		print_json_string("The AI agent MVP ran a local read-only diagnostic. Configure and enable a model backend to add remote reasoning; local telemetry and policy findings are included in this response.");
	}
	printf("\n");
	printf("}\n");

	return 0;
}

static int handle_agent_command(int argc, char **argv)
{
#ifndef EDGEPULSE_ENABLE_AI_AGENT
	(void)argc;
	(void)argv;
	printf("{\n");
	printf("  \"status\": \"compiled_disabled\",\n");
	printf("  \"answer\": \"EdgePulse AI agent support was not included in this package build.\"\n");
	printf("}\n");
	return 0;
#else
	if (argc < 3) {
		fprintf(stderr, "Usage: edgepulse-ctl agent <status|diagnose|ask> [message]\n");
		return 2;
	}

	if (strcmp(argv[2], "status") == 0)
		return print_agent_status();

	if (strcmp(argv[2], "diagnose") == 0)
		return print_agent_diagnose(argc >= 4 ? argv[3] : "Run a local EdgePulse diagnostic.");

	if (strcmp(argv[2], "ask") == 0) {
		if (argc < 4) {
			fprintf(stderr, "edgepulse-ctl: agent ask requires a message\n");
			return 2;
		}
		return print_agent_diagnose(argv[3]);
	}

	fprintf(stderr, "Usage: edgepulse-ctl agent <status|diagnose|ask> [message]\n");
	return 2;
#endif
}

static void print_usage(FILE *fp)
{
	fprintf(fp, "Usage: edgepulse-ctl <status|latest|features|export|agent|version> [--json] [--format csv] [--window seconds|60s|5m|1h] [--since seconds|60s|5m|1h]\n");
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

	if (strcmp(argv[1], "agent") == 0)
		return handle_agent_command(argc, argv);

	if (strcmp(argv[1], "version") == 0) {
		printf("%s\n", EDGEPULSE_VERSION);
		return 0;
	}

	print_usage(stderr);
	return 2;
}
