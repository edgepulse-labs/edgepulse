#include "edgepulse.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <syslog.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <sqlite3.h>

#define AGENT_STR_SIZE 256
#define AGENT_MAX_MODELS 8

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
	int chat_enabled;
	int mcp_enabled;
	int request_timeout_sec;
	int heartbeat_interval_sec;
	int tool_timeout_sec;
	int max_tool_output_bytes;
	char policy_profile[64];
	char default_conversation_id[64];
	char db_path[AGENT_STR_SIZE];
};

struct agent_model_config {
	int present;
	int enabled;
	int configured;
	int timeout_sec;
	int retry_count;
	int max_tokens;
	int no_think;
	int priority;
	char name[64];
	char role[128];
	char base_url[AGENT_STR_SIZE];
	char model[128];
	char api_key[AGENT_STR_SIZE];
	char api_key_env[128];
};

struct agent_tool_result {
	char name[64];
	char status[32];
	int exit_code;
	long elapsed_ms;
	char output[2048];
};

struct agent_model_request {
	char route_role[64];
	char provider[64];
	char model[128];
	char endpoint[AGENT_STR_SIZE + 64];
	char status[64];
};

struct agent_model_response {
	char status[64];
	int attempts;
	int http_status;
	int reasoning_present;
	char text[8192];
	char finish_reason[64];
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

#ifdef EDGEPULSE_ENABLE_AI_AGENT
static void agent_syslog(int priority, const char *fmt, ...)
{
	va_list ap;

	openlog("edgepulse-agent", LOG_PID, LOG_DAEMON);
	va_start(ap, fmt);
	vsyslog(priority, fmt, ap);
	va_end(ap);
	closelog();
}
#endif

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

static void init_agent_model_config(struct agent_model_config *model, int index)
{
	memset(model, 0, sizeof(*model));
	model->timeout_sec = 60;
	model->retry_count = 0;
	model->max_tokens = 2048;
	model->no_think = 0;
	model->priority = 100 + index;
#ifdef EDGEPULSE_AI_DEFAULT_BASE_URL
	snprintf(model->base_url, sizeof(model->base_url), "%s",
		 EDGEPULSE_AI_DEFAULT_BASE_URL);
#endif
#ifdef EDGEPULSE_AI_DEFAULT_MODEL
	snprintf(model->model, sizeof(model->model), "%s",
		 EDGEPULSE_AI_DEFAULT_MODEL);
#endif
}

static void init_agent_config(struct agent_config *agent,
			      struct agent_model_config *model)
{
	memset(agent, 0, sizeof(*agent));
	agent->enabled = 0;
	agent->local_only = 1;
	agent->memory_enabled = 1;
	agent->shell_enabled = 1;
	agent->ubus_enabled = 1;
	agent->chat_enabled = 1;
	agent->mcp_enabled = 0;
	agent->request_timeout_sec = 60;
	agent->heartbeat_interval_sec = 60;
	agent->tool_timeout_sec = 5;
	agent->max_tool_output_bytes = 8192;
	snprintf(agent->policy_profile, sizeof(agent->policy_profile), "%s", "read_only");
	snprintf(agent->default_conversation_id,
		 sizeof(agent->default_conversation_id), "%s", "default");
	snprintf(agent->db_path, sizeof(agent->db_path), "%s", EDGEPULSE_DB_PATH);
	init_agent_model_config(model, 0);
}

static void finalize_agent_model_config(struct agent_model_config *model)
{
	model->configured = model->enabled && model->base_url[0] != '\0' &&
		model->model[0] != '\0' &&
		(strncmp(model->base_url, "http://127.0.0.1", 16) == 0 ||
		 strncmp(model->base_url, "http://localhost", 16) == 0 ||
		 model->api_key[0] != '\0' ||
		 (model->api_key_env[0] != '\0' && getenv(model->api_key_env)));
}

static void sort_agent_models(struct agent_model_config *models, int count)
{
	for (int i = 0; i < count; i++) {
		for (int j = i + 1; j < count; j++) {
			if (models[j].priority < models[i].priority) {
				struct agent_model_config tmp = models[i];
				models[i] = models[j];
				models[j] = tmp;
			}
		}
	}
}

static int read_agent_config_models(struct agent_config *agent,
				    struct agent_model_config *models,
				    int max_models, int *model_count)
{
	const char *path = getenv("EDGEPULSE_CONFIG_PATH");
	FILE *fp;
	char line[512];
	char section_type[64] = "";
	char section_name[64] = "";
	struct agent_model_config primary;
	int current_model = -1;
	int count = 0;

	init_agent_config(agent, &primary);
	if (models && max_models > 0)
		memset(models, 0, (size_t)max_models * sizeof(models[0]));
	if (model_count)
		*model_count = 0;
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

			current_model = -1;
			if (strcmp(section_type, "model") == 0 && models && count < max_models) {
				current_model = count;
				init_agent_model_config(&models[count], count);
				models[count].present = 1;
				snprintf(models[count].name, sizeof(models[count].name), "%s",
					 section_name[0] ? section_name : "model");
				count++;
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

		if (strcmp(section_type, "edgepulse") == 0 &&
		    strcmp(section_name, "main") == 0) {
			if (strcmp(key, "db_path") == 0 && value[0] != '\0')
				snprintf(agent->db_path, sizeof(agent->db_path), "%s", value);
		} else if (strcmp(section_type, "agent") == 0) {
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
			else if (strcmp(key, "chat_enabled") == 0)
				agent->chat_enabled = parse_bool_value(value, agent->chat_enabled);
			else if (strcmp(key, "mcp_enabled") == 0)
				agent->mcp_enabled = parse_bool_value(value, agent->mcp_enabled);
			else if (strcmp(key, "request_timeout_sec") == 0)
				agent->request_timeout_sec = parse_int_value(value, agent->request_timeout_sec);
			else if (strcmp(key, "heartbeat_interval_sec") == 0)
				agent->heartbeat_interval_sec = parse_int_value(value, agent->heartbeat_interval_sec);
			else if (strcmp(key, "tool_timeout_sec") == 0)
				agent->tool_timeout_sec = parse_int_value(value, agent->tool_timeout_sec);
			else if (strcmp(key, "max_tool_output_bytes") == 0)
				agent->max_tool_output_bytes = parse_int_value(value, agent->max_tool_output_bytes);
			else if (strcmp(key, "policy_profile") == 0)
				snprintf(agent->policy_profile, sizeof(agent->policy_profile), "%s", value);
			else if (strcmp(key, "default_conversation_id") == 0)
				snprintf(agent->default_conversation_id,
					 sizeof(agent->default_conversation_id), "%s",
					 value[0] ? value : "default");
		} else if (strcmp(section_type, "model") == 0 && current_model >= 0) {
			struct agent_model_config *model = &models[current_model];

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
			else if (strcmp(key, "max_tokens") == 0)
				model->max_tokens = parse_int_value(value, model->max_tokens);
			else if (strcmp(key, "no_think") == 0)
				model->no_think = parse_bool_value(value, model->no_think);
			else if (strcmp(key, "priority") == 0)
				model->priority = parse_int_value(value, model->priority);
		}
	}

	fclose(fp);
	for (int i = 0; i < count; i++)
		finalize_agent_model_config(&models[i]);
	sort_agent_models(models, count);
	if (model_count)
		*model_count = count;
	return 0;
}

static int read_agent_config(struct agent_config *agent,
			     struct agent_model_config *model)
{
	struct agent_model_config models[AGENT_MAX_MODELS];
	int model_count = 0;
	int rc = read_agent_config_models(agent, models, AGENT_MAX_MODELS,
					  &model_count);

	if (model_count > 0)
		*model = models[0];
	else
		init_agent_model_config(model, 0);
	return rc;
}

static const char *agent_model_status(const struct agent_config *agent,
				      const struct agent_model_config *model)
				      __attribute__((unused));
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

static const char *agent_models_status(const struct agent_config *agent,
				       const struct agent_model_config *models,
				       int model_count)
{
	if (!agent->enabled)
		return "agent_disabled";
	if (agent->local_only)
		return "local_only";
	if (model_count <= 0)
		return "not_configured";
	for (int i = 0; i < model_count; i++) {
		if (models[i].configured)
			return "configured";
	}
	for (int i = 0; i < model_count; i++) {
		if (models[i].enabled)
			return "missing_credentials_or_model";
	}
	return "model_disabled";
}

static int agent_config_is_valid_url(const char *value)
{
	return !value || value[0] == '\0' ||
		strncmp(value, "https://", 8) == 0 ||
		strncmp(value, "http://127.0.0.1", 16) == 0 ||
		strncmp(value, "http://localhost", 16) == 0;
}

static int agent_config_has_warnings(const struct agent_config *agent,
				     const struct agent_model_config *model)
{
	if (strcmp(agent->policy_profile, "read_only") != 0 &&
	    strcmp(agent->policy_profile, "operator_confirmed") != 0)
		return 1;
	if (agent->request_timeout_sec < 1 || agent->request_timeout_sec > 600)
		return 1;
	if (agent->tool_timeout_sec < 1 || agent->tool_timeout_sec > 60)
		return 1;
	if (agent->max_tool_output_bytes < 1024 || agent->max_tool_output_bytes > 65536)
		return 1;
	if (!agent_config_is_valid_url(model->base_url))
		return 1;
	if (model->enabled && model->model[0] == '\0')
		return 1;
	if (model->max_tokens < 128 || model->max_tokens > 8192)
		return 1;
	return 0;
}

static void print_agent_validation(const struct agent_config *agent,
				   const struct agent_model_config *model)
{
	int first = 1;

	printf("  \"validation\": [\n");

#define PRINT_VALIDATION_WARNING(msg) \
	do { \
		if (!first) \
			printf(",\n"); \
		printf("    { \"severity\": \"warning\", \"message\": "); \
		print_json_string(msg); \
		printf(" }"); \
		first = 0; \
	} while (0)

	if (strcmp(agent->policy_profile, "read_only") != 0 &&
	    strcmp(agent->policy_profile, "operator_confirmed") != 0)
		PRINT_VALIDATION_WARNING("Only the read_only and operator_confirmed policy profiles are supported by the current MVP.");
	if (agent->request_timeout_sec < 1 || agent->request_timeout_sec > 600)
		PRINT_VALIDATION_WARNING("request_timeout_sec should be between 1 and 600 seconds.");
	if (agent->tool_timeout_sec < 1 || agent->tool_timeout_sec > 60)
		PRINT_VALIDATION_WARNING("tool_timeout_sec should be between 1 and 60 seconds.");
	if (agent->max_tool_output_bytes < 1024 || agent->max_tool_output_bytes > 65536)
		PRINT_VALIDATION_WARNING("max_tool_output_bytes should be between 1024 and 65536 bytes.");
	if (!agent_config_is_valid_url(model->base_url))
		PRINT_VALIDATION_WARNING("Remote model base_url should use https, localhost, or 127.0.0.1.");
	if (model->enabled && model->model[0] == '\0')
		PRINT_VALIDATION_WARNING("Enabled model sections must include a model name.");
	if (model->max_tokens < 128 || model->max_tokens > 8192)
		PRINT_VALIDATION_WARNING("model max_tokens should be between 128 and 8192.");

#undef PRINT_VALIDATION_WARNING

	if (first)
		printf("    { \"severity\": \"ok\", \"message\": \"Agent configuration is valid for the current MVP.\" }");

	printf("\n");
	printf("  ]");
}

static void agent_make_request_id(char *buffer, size_t size)
{
	snprintf(buffer, size, "%lld-%ld", (long long)time(NULL), (long)getpid());
}

static int agent_store_audit(const char *db_path, const char *request_id,
			     const char *event_type, const char *detail)
{
	static const char *sql =
		"INSERT INTO agent_audit_log(created_at, request_id, event_type, detail) "
		"VALUES(strftime('%s','now'), ?, ?, ?);";
	sqlite3 *db = NULL;
	sqlite3_stmt *stmt = NULL;
	int rc;

	if (edgepulse_init_database(db_path) != 0)
		return -1;
	if (sqlite3_open(db_path, &db) != SQLITE_OK)
		return -1;
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
		sqlite3_close(db);
		return -1;
	}

	sqlite3_bind_text(stmt, 1, request_id, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 2, event_type, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 3, detail, -1, SQLITE_TRANSIENT);
	rc = sqlite3_step(stmt);

	sqlite3_finalize(stmt);
	sqlite3_close(db);
	return rc == SQLITE_DONE ? 0 : -1;
}

static int agent_store_memory(const char *db_path, const char *content)
{
	static const char *sql =
		"INSERT INTO agent_memory(created_at, source, sensitivity, ttl_sec, content) "
		"VALUES(strftime('%s','now'), 'agent', 'normal', 0, ?);";
	sqlite3 *db = NULL;
	sqlite3_stmt *stmt = NULL;
	int rc;

	if (edgepulse_init_database(db_path) != 0)
		return -1;
	if (sqlite3_open(db_path, &db) != SQLITE_OK)
		return -1;
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
		sqlite3_close(db);
		return -1;
	}

	sqlite3_bind_text(stmt, 1, content, -1, SQLITE_TRANSIENT);
	rc = sqlite3_step(stmt);

	sqlite3_finalize(stmt);
	sqlite3_close(db);
	return rc == SQLITE_DONE ? 0 : -1;
}

static int agent_store_request(const char *db_path, const char *request_id,
			       const char *question, const char *model_status,
			       const char *answer)
{
	static const char *sql =
		"INSERT INTO agent_requests(request_id, created_at, status, question, model_status, answer) "
		"VALUES(?, strftime('%s','now'), 'ok', ?, ?, ?) "
		"ON CONFLICT(request_id) DO UPDATE SET "
		"status=excluded.status, question=excluded.question, "
		"model_status=excluded.model_status, answer=excluded.answer;";
	sqlite3 *db = NULL;
	sqlite3_stmt *stmt = NULL;
	int rc;

	if (edgepulse_init_database(db_path) != 0)
		return -1;
	if (sqlite3_open(db_path, &db) != SQLITE_OK)
		return -1;
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
		sqlite3_close(db);
		return -1;
	}

	sqlite3_bind_text(stmt, 1, request_id, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 2, question, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 3, model_status, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 4, answer, -1, SQLITE_TRANSIENT);
	rc = sqlite3_step(stmt);

	sqlite3_finalize(stmt);
	sqlite3_close(db);
	return rc == SQLITE_DONE ? 0 : -1;
}

static int agent_store_conversation_messages(const char *db_path,
					     const char *conversation_id,
					     const char *request_id,
					     const char *question,
					     const char *model_status,
					     const char *answer)
{
	static const char *conversation_sql =
		"INSERT INTO agent_conversations(conversation_id, created_at, updated_at, title) "
		"VALUES(?, strftime('%s','now'), strftime('%s','now'), ?) "
		"ON CONFLICT(conversation_id) DO UPDATE SET updated_at=strftime('%s','now');";
	static const char *message_sql =
		"INSERT INTO agent_messages(conversation_id, request_id, created_at, role, content, model_status) "
		"VALUES(?, ?, strftime('%s','now'), ?, ?, ?);";
	sqlite3 *db = NULL;
	sqlite3_stmt *stmt = NULL;
	const char *cid = conversation_id && conversation_id[0] ? conversation_id : "default";
	int rc = -1;

	if (edgepulse_init_database(db_path) != 0)
		return -1;
	if (sqlite3_open(db_path, &db) != SQLITE_OK)
		return -1;

	if (sqlite3_prepare_v2(db, conversation_sql, -1, &stmt, NULL) != SQLITE_OK)
		goto out;
	sqlite3_bind_text(stmt, 1, cid, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 2, question ? question : "", -1, SQLITE_TRANSIENT);
	if (sqlite3_step(stmt) != SQLITE_DONE)
		goto out;
	sqlite3_finalize(stmt);
	stmt = NULL;

	if (sqlite3_prepare_v2(db, message_sql, -1, &stmt, NULL) != SQLITE_OK)
		goto out;
	sqlite3_bind_text(stmt, 1, cid, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 2, request_id, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 3, "user", -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 4, question ? question : "", -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 5, model_status ? model_status : "", -1, SQLITE_TRANSIENT);
	if (sqlite3_step(stmt) != SQLITE_DONE)
		goto out;
	sqlite3_finalize(stmt);
	stmt = NULL;

	if (sqlite3_prepare_v2(db, message_sql, -1, &stmt, NULL) != SQLITE_OK)
		goto out;
	sqlite3_bind_text(stmt, 1, cid, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 2, request_id, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 3, "assistant", -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 4, answer ? answer : "", -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 5, model_status ? model_status : "", -1, SQLITE_TRANSIENT);
	rc = sqlite3_step(stmt) == SQLITE_DONE ? 0 : -1;

out:
	if (stmt)
		sqlite3_finalize(stmt);
	sqlite3_close(db);
	return rc;
}

static int agent_command_allowed(char *const argv[])
{
	if (!argv || !argv[0])
		return 0;
	if (strcmp(argv[0], "uname") == 0)
		return argv[1] && strcmp(argv[1], "-a") == 0 && !argv[2];
	if (strcmp(argv[0], "uptime") == 0)
		return !argv[1];
	if (strcmp(argv[0], "ubus") == 0) {
		if (!argv[1] || strcmp(argv[1], "call") != 0 || !argv[2] || !argv[3])
			return 0;
		if (strcmp(argv[2], "system") == 0 &&
		    (strcmp(argv[3], "board") == 0 || strcmp(argv[3], "info") == 0) &&
		    !argv[4])
			return 1;
		if (strcmp(argv[2], "network.interface") == 0 &&
		    strcmp(argv[3], "dump") == 0 && !argv[4])
			return 1;
		if (strcmp(argv[2], "network.wireless") == 0 &&
		    strcmp(argv[3], "status") == 0 && !argv[4])
			return 1;
		return 0;
	}
	if (strcmp(argv[0], "logread") == 0) {
		if (argv[1] && strcmp(argv[1], "-l") == 0 && argv[2] && !argv[3]) {
			int parsed;
			return edgepulse_parse_positive_int(argv[2], &parsed) == 0 &&
				parsed <= 200;
		}
		return 0;
	}
	if (strcmp(argv[0], "uci") == 0)
		return argv[1] && strcmp(argv[1], "show") == 0 &&
			argv[2] && strcmp(argv[2], "edgepulse") == 0 && !argv[3];
	return 0;
}

static int agent_mutating_command_allowed(char *const argv[])
{
	static const char ssid_prefix[] = "wireless.@wifi-iface[0].ssid=";
	static const char key_prefix[] = "wireless.@wifi-iface[0].key=";
	static const char encryption_prefix[] = "wireless.@wifi-iface[0].encryption=";

	if (!argv || !argv[0])
		return 0;
	if ((strcmp(argv[0], "ifdown") == 0 || strcmp(argv[0], "ifup") == 0) &&
	    argv[1] && strcmp(argv[1], "wan") == 0 && !argv[2])
		return 1;
	if (strcmp(argv[0], "wifi") == 0 && argv[1] &&
	    strcmp(argv[1], "reload") == 0 && !argv[2])
		return 1;
	if (strcmp(argv[0], "uci") == 0 && argv[1]) {
		if (strcmp(argv[1], "commit") == 0 && argv[2] &&
		    strcmp(argv[2], "wireless") == 0 && !argv[3])
			return 1;
		if (strcmp(argv[1], "set") == 0 && argv[2] && !argv[3]) {
			return strncmp(argv[2], ssid_prefix, strlen(ssid_prefix)) == 0 ||
				strncmp(argv[2], key_prefix, strlen(key_prefix)) == 0 ||
				strncmp(argv[2], encryption_prefix,
					strlen(encryption_prefix)) == 0 ||
				strcmp(argv[2], "wireless.@wifi-iface[0].disabled=0") == 0;
		}
	}
	return 0;
}

static int agent_run_policy_command(const char *name, char *const argv[],
				    int timeout_sec, int output_limit,
				    int mutation_allowed,
				    struct agent_tool_result *result)
{
	int pipefd[2];
	pid_t pid;
	time_t start;
	size_t used = 0;
	int status = 0;

	memset(result, 0, sizeof(*result));
	snprintf(result->name, sizeof(result->name), "%s", name);
	snprintf(result->status, sizeof(result->status), "%s", "blocked");
	result->exit_code = -1;

	if ((!mutation_allowed && !agent_command_allowed(argv)) ||
	    (mutation_allowed && !agent_mutating_command_allowed(argv))) {
		snprintf(result->output, sizeof(result->output),
			 "Command blocked by policy allowlist.");
		return -1;
	}

	if (output_limit <= 0 || output_limit > (int)sizeof(result->output) - 1)
		output_limit = (int)sizeof(result->output) - 1;

	if (pipe(pipefd) != 0) {
		snprintf(result->status, sizeof(result->status), "%s", "error");
		snprintf(result->output, sizeof(result->output), "%s", strerror(errno));
		return -1;
	}

	pid = fork();
	if (pid < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		snprintf(result->status, sizeof(result->status), "%s", "error");
		snprintf(result->output, sizeof(result->output), "%s", strerror(errno));
		return -1;
	}

	if (pid == 0) {
		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		dup2(pipefd[1], STDERR_FILENO);
		close(pipefd[1]);
		execvp(argv[0], argv);
		_exit(127);
	}

	close(pipefd[1]);
	fcntl(pipefd[0], F_SETFL, fcntl(pipefd[0], F_GETFL, 0) | O_NONBLOCK);
	start = time(NULL);

	while (1) {
		char buffer[256];
		ssize_t nread = read(pipefd[0], buffer, sizeof(buffer));
		pid_t waited;

		if (nread > 0) {
			size_t copy = (size_t)nread;
			if (copy > (size_t)output_limit - used)
				copy = (size_t)output_limit - used;
			if (copy > 0) {
				memcpy(result->output + used, buffer, copy);
				used += copy;
				result->output[used] = '\0';
			}
		}

		waited = waitpid(pid, &status, WNOHANG);
		if (waited == pid)
			break;

		if (timeout_sec > 0 && time(NULL) - start >= timeout_sec) {
			kill(pid, SIGKILL);
			waitpid(pid, &status, 0);
			snprintf(result->status, sizeof(result->status), "%s", "timeout");
			result->elapsed_ms = (long)(time(NULL) - start) * 1000L;
			close(pipefd[0]);
			return -1;
		}

		usleep(100000);
	}

	while (used < (size_t)output_limit) {
		char buffer[256];
		ssize_t nread = read(pipefd[0], buffer, sizeof(buffer));
		size_t copy;

		if (nread <= 0)
			break;
		copy = (size_t)nread;
		if (copy > (size_t)output_limit - used)
			copy = (size_t)output_limit - used;
		memcpy(result->output + used, buffer, copy);
		used += copy;
		result->output[used] = '\0';
	}

	close(pipefd[0]);
	result->elapsed_ms = (long)(time(NULL) - start) * 1000L;
	if (WIFEXITED(status)) {
		result->exit_code = WEXITSTATUS(status);
		snprintf(result->status, sizeof(result->status), "%s",
			 result->exit_code == 0 ? "ok" : "error");
	} else {
		snprintf(result->status, sizeof(result->status), "%s", "error");
	}

	return result->exit_code == 0 ? 0 : -1;
}

static int agent_run_read_only_command(const char *name, char *const argv[],
				       int timeout_sec, int output_limit,
				       struct agent_tool_result *result)
{
	return agent_run_policy_command(name, argv, timeout_sec, output_limit,
					0, result);
}

static void print_agent_tool_json_mode(const struct agent_tool_result *result,
				       const char *mode)
{
	printf("    { \"name\": ");
	print_json_string(result->name);
	printf(", \"mode\": ");
	print_json_string(mode);
	printf(", \"status\": ");
	print_json_string(result->status);
	printf(", \"exit_code\": %d, \"elapsed_ms\": %ld, \"output\": ",
	       result->exit_code, result->elapsed_ms);
	print_json_string(result->output);
	printf(" }");
}

static void print_agent_tool_json(const struct agent_tool_result *result)
{
	print_agent_tool_json_mode(result, "read_only");
}

static void agent_build_model_request(const struct agent_config *agent,
				      const struct agent_model_config *model,
				      const char *role,
				      struct agent_model_request *request)
{
	memset(request, 0, sizeof(*request));
	snprintf(request->route_role, sizeof(request->route_role), "%s", role);
	snprintf(request->provider, sizeof(request->provider), "%s",
		 model->name[0] ? model->name : "none");
	snprintf(request->model, sizeof(request->model), "%s", model->model);

	if (!agent->enabled) {
		snprintf(request->status, sizeof(request->status), "%s", "agent_disabled");
		return;
	}
	if (agent->local_only) {
		snprintf(request->status, sizeof(request->status), "%s", "local_only");
		return;
	}
	if (!model->configured) {
		snprintf(request->status, sizeof(request->status), "%s", "not_configured");
		return;
	}
	if (model->role[0] != '\0' && !strstr(model->role, role)) {
		snprintf(request->status, sizeof(request->status), "%s", "role_not_matched");
		return;
	}

	snprintf(request->endpoint, sizeof(request->endpoint), "%s/chat/completions",
		 model->base_url);
	snprintf(request->status, sizeof(request->status), "%s", "ready");
}

static void print_agent_model_request_json(const struct agent_model_request *request)
{
	printf("  \"model_request\": {\n");
	printf("    \"route_role\": ");
	print_json_string(request->route_role);
	printf(",\n");
	printf("    \"provider\": ");
	print_json_string(request->provider);
	printf(",\n");
	printf("    \"model\": ");
	print_json_string(request->model);
	printf(",\n");
	printf("    \"endpoint\": ");
	print_json_string(request->endpoint);
	printf(",\n");
	printf("    \"api_key\": \"redacted\",\n");
	printf("    \"status\": ");
	print_json_string(request->status);
	printf("\n");
	printf("  }");
}

static int parse_local_http_url(const char *url, char *host, size_t host_size,
				char *port, size_t port_size,
				char *path, size_t path_size)
{
	const char *cursor;
	const char *slash;
	const char *colon;
	size_t host_len;

	if (!url || strncmp(url, "http://", 7) != 0)
		return -1;

	cursor = url + 7;
	slash = strchr(cursor, '/');
	if (!slash)
		slash = cursor + strlen(cursor);

	colon = memchr(cursor, ':', (size_t)(slash - cursor));
	if (colon) {
		host_len = (size_t)(colon - cursor);
		snprintf(port, port_size, "%.*s", (int)(slash - colon - 1), colon + 1);
	} else {
		host_len = (size_t)(slash - cursor);
		snprintf(port, port_size, "%s", "80");
	}

	if (host_len == 0 || host_len >= host_size || port[0] == '\0')
		return -1;
	snprintf(host, host_size, "%.*s", (int)host_len, cursor);

	if (strcmp(host, "127.0.0.1") != 0 && strcmp(host, "localhost") != 0)
		return -1;

	if (*slash)
		snprintf(path, path_size, "%s", slash);
	else
		snprintf(path, path_size, "%s", "/");

	return 0;
}

static void build_model_payload(char *payload, size_t size,
				const struct agent_model_config *model,
				const char *question)
{
	char escaped_question[2048];
	const char *system_prompt;
	char *out = escaped_question;
	size_t remaining = sizeof(escaped_question);
	const unsigned char *p = (const unsigned char *)(question ? question : "");

	while (*p && remaining > 2) {
		if (*p == '"' || *p == '\\') {
			*out++ = '\\';
			*out++ = (char)*p++;
			remaining -= 2;
		} else if (*p == '\n' || *p == '\r' || *p == '\t') {
			*out++ = ' ';
			p++;
			remaining--;
		} else if (*p < 32) {
			p++;
		} else {
			*out++ = (char)*p++;
			remaining--;
		}
	}
	*out = '\0';

	system_prompt = model->no_think ?
		"EdgePulse OpenWrt diagnostics. Final answer only. No reasoning. /no_think" :
		"EdgePulse OpenWrt diagnostics. Return a concise final answer.";

	snprintf(payload, size,
		 "{\"model\":\"%s\",\"messages\":[{\"role\":\"system\",\"content\":\"%s\"},{\"role\":\"user\",\"content\":\"%s\"}],\"stream\":false,\"temperature\":0,\"max_tokens\":%d}",
		 model->model, system_prompt, escaped_question,
		 model->max_tokens > 0 ? model->max_tokens : 2048);
}

static const char *agent_model_api_key(const struct agent_model_config *model)
{
	const char *env_value;

	if (model->api_key[0] != '\0')
		return model->api_key;
	if (model->api_key_env[0] == '\0')
		return "";
	env_value = getenv(model->api_key_env);
	return env_value ? env_value : "";
}

static int extract_openai_message_content(const char *body, char *out, size_t out_size)
{
	const char *key;
	const char *cursor;
	size_t used = 0;

	if (!body || !out || out_size == 0)
		return -1;

	key = strstr(body, "\"content\"");
	if (!key)
		return -1;
	cursor = strchr(key + 9, ':');
	if (!cursor)
		return -1;
	cursor++;
	while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r')
		cursor++;
	if (*cursor != '"')
		return -1;
	cursor++;

	while (*cursor && *cursor != '"' && used + 1 < out_size) {
		if (*cursor == '\\' && cursor[1]) {
			cursor++;
			switch (*cursor) {
			case 'n':
				out[used++] = '\n';
				break;
			case 'r':
				out[used++] = '\r';
				break;
			case 't':
				out[used++] = '\t';
				break;
			default:
				out[used++] = *cursor;
				break;
			}
			cursor++;
			continue;
		}
		out[used++] = *cursor++;
	}

	out[used] = '\0';
	return used > 0 ? 0 : -1;
}

static int extract_openai_finish_reason(const char *body, char *out, size_t out_size)
{
	const char *key;
	const char *cursor;
	size_t used = 0;

	if (!body || !out || out_size == 0)
		return -1;

	key = strstr(body, "\"finish_reason\"");
	if (!key)
		return -1;
	cursor = strchr(key + 15, ':');
	if (!cursor)
		return -1;
	cursor++;
	while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r')
		cursor++;
	if (*cursor != '"')
		return -1;
	cursor++;

	while (*cursor && *cursor != '"' && used + 1 < out_size)
		out[used++] = *cursor++;

	out[used] = '\0';
	return used > 0 ? 0 : -1;
}

static int openai_has_reasoning_content(const char *body)
{
	const char *key;
	const char *cursor;

	if (!body)
		return 0;

	key = strstr(body, "\"reasoning_content\"");
	if (!key)
		return 0;
	cursor = strchr(key + 19, ':');
	if (!cursor)
		return 0;
	cursor++;
	while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r')
		cursor++;
	if (*cursor != '"')
		return 0;
	cursor++;
	return *cursor != '"';
}

static int agent_call_local_model(const struct agent_model_request *request,
				  const struct agent_model_config *model,
				  const char *question,
				  struct agent_model_response *response)
{
	struct addrinfo hints;
	struct addrinfo *result = NULL;
	struct addrinfo *rp;
	char host[64];
	char port[16];
	char path[192];
	char payload[4096];
	char header[5120];
	char auth_header[384] = "";
	char buffer[1024];
	struct timeval timeout;
	const char *api_key = agent_model_api_key(model);
	int sock = -1;
	ssize_t nread;
	size_t used = 0;

	if (parse_local_http_url(request->endpoint, host, sizeof(host), port,
				 sizeof(port), path, sizeof(path)) != 0) {
		snprintf(response->status, sizeof(response->status), "%s",
			 "unsupported_transport");
		return -1;
	}

	build_model_payload(payload, sizeof(payload), model, question);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(host, port, &hints, &result) != 0) {
		snprintf(response->status, sizeof(response->status), "%s",
			 "resolve_error");
		return -1;
	}

	for (rp = result; rp; rp = rp->ai_next) {
		sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sock < 0)
			continue;
		timeout.tv_sec = model->timeout_sec > 0 ? model->timeout_sec : 30;
		timeout.tv_usec = 0;
		setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
		setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
		if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0)
			break;
		close(sock);
		sock = -1;
	}
	freeaddrinfo(result);

	if (sock < 0) {
		snprintf(response->status, sizeof(response->status), "%s",
			 "connect_error");
		return -1;
	}

	if (api_key[0] != '\0')
		snprintf(auth_header, sizeof(auth_header),
			 "Authorization: Bearer %s\r\n", api_key);

	snprintf(header, sizeof(header),
		 "POST %s HTTP/1.1\r\n"
		 "Host: %s:%s\r\n"
		 "Content-Type: application/json\r\n"
		 "%s"
		 "Content-Length: %zu\r\n"
		 "Connection: close\r\n\r\n%s",
		 path, host, port, auth_header, strlen(payload), payload);

	if (write(sock, header, strlen(header)) < 0) {
		close(sock);
		snprintf(response->status, sizeof(response->status), "%s",
			 "write_error");
		return -1;
	}

	while ((nread = read(sock, buffer, sizeof(buffer))) > 0) {
		size_t copy = (size_t)nread;
		if (copy > sizeof(response->text) - used - 1)
			copy = sizeof(response->text) - used - 1;
		if (copy > 0) {
			memcpy(response->text + used, buffer, copy);
			used += copy;
			response->text[used] = '\0';
		}
	}
	close(sock);

	if (strncmp(response->text, "HTTP/1.1 ", 9) == 0 ||
	    strncmp(response->text, "HTTP/1.0 ", 9) == 0) {
		char *body;

		response->http_status = atoi(response->text + 9);
		body = strstr(response->text, "\r\n\r\n");
		if (body) {
			body += 4;
			memmove(response->text, body, strlen(body) + 1);
		}
	}

	snprintf(response->status, sizeof(response->status), "%s",
		 response->http_status >= 200 && response->http_status < 300 ?
		 "ok" : "http_error");
	return strcmp(response->status, "ok") == 0 ? 0 : -1;
}

static int agent_call_uclient_model(const struct agent_model_request *request,
				    const struct agent_model_config *model,
				    const char *question,
				    struct agent_model_response *response)
{
	char payload[4096];
	char post_path[] = "/tmp/edgepulse-agent-payload.XXXXXX";
	char timeout_arg[16];
	char auth_arg[384];
	char content_type_arg[] = "--header=Content-Type: application/json";
	const char *api_key = agent_model_api_key(model);
	const char *argv[14];
	int post_fd;
	int pipe_fd[2];
	int argc = 0;
	pid_t pid;
	ssize_t nread;
	size_t used = 0;
	int status = 0;
	char buffer[512];

	build_model_payload(payload, sizeof(payload), model, question);
	post_fd = mkstemp(post_path);
	if (post_fd < 0) {
		snprintf(response->status, sizeof(response->status), "%s",
			 "tempfile_error");
		return -1;
	}
	if (write(post_fd, payload, strlen(payload)) < 0) {
		close(post_fd);
		unlink(post_path);
		snprintf(response->status, sizeof(response->status), "%s",
			 "write_error");
		return -1;
	}
	close(post_fd);

	if (pipe(pipe_fd) != 0) {
		unlink(post_path);
		snprintf(response->status, sizeof(response->status), "%s",
			 "pipe_error");
		return -1;
	}

	snprintf(timeout_arg, sizeof(timeout_arg), "%d",
		 model->timeout_sec > 0 ? model->timeout_sec : 30);
	argv[argc++] = "uclient-fetch";
	argv[argc++] = "-q";
	argv[argc++] = "-T";
	argv[argc++] = timeout_arg;
	argv[argc++] = "-O";
	argv[argc++] = "-";
	argv[argc++] = "--post-file";
	argv[argc++] = post_path;
	argv[argc++] = content_type_arg;
	if (api_key[0] != '\0') {
		snprintf(auth_arg, sizeof(auth_arg),
			 "--header=Authorization: Bearer %s", api_key);
		argv[argc++] = auth_arg;
	}
	argv[argc++] = request->endpoint;
	argv[argc] = NULL;

	pid = fork();
	if (pid < 0) {
		close(pipe_fd[0]);
		close(pipe_fd[1]);
		unlink(post_path);
		snprintf(response->status, sizeof(response->status), "%s",
			 "fork_error");
		return -1;
	}

	if (pid == 0) {
		close(pipe_fd[0]);
		dup2(pipe_fd[1], STDOUT_FILENO);
		dup2(pipe_fd[1], STDERR_FILENO);
		close(pipe_fd[1]);
		execvp("uclient-fetch", (char *const *)argv);
		_exit(127);
	}

	close(pipe_fd[1]);
	while ((nread = read(pipe_fd[0], buffer, sizeof(buffer))) > 0) {
		size_t copy = (size_t)nread;
		if (copy > sizeof(response->text) - used - 1)
			copy = sizeof(response->text) - used - 1;
		if (copy > 0) {
			memcpy(response->text + used, buffer, copy);
			used += copy;
			response->text[used] = '\0';
		}
	}
	close(pipe_fd[0]);
	waitpid(pid, &status, 0);
	unlink(post_path);

	if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
		response->http_status = 200;
		snprintf(response->status, sizeof(response->status), "%s", "ok");
		return 0;
	}

	response->http_status = 0;
	snprintf(response->status, sizeof(response->status), "%s",
		 WIFEXITED(status) && WEXITSTATUS(status) == 127 ?
		 "transport_unavailable" : "fetch_error");
	return -1;
}

static int agent_fetch_remote_models(const struct agent_model_config *model,
				     char *body, size_t body_size,
				     char *status_out, size_t status_size)
{
	char timeout_arg[16];
	char auth_arg[384];
	char endpoint[AGENT_STR_SIZE + 32];
	const char *api_key = agent_model_api_key(model);
	const char *argv[12];
	int pipe_fd[2];
	int argc = 0;
	pid_t pid;
	ssize_t nread;
	size_t used = 0;
	int status = 0;
	char buffer[512];

	if (!body || body_size == 0 || !status_out || status_size == 0)
		return -1;
	body[0] = '\0';
	status_out[0] = '\0';

	if (model->base_url[0] == '\0') {
		snprintf(status_out, status_size, "%s", "missing_base_url");
		return -1;
	}

	snprintf(endpoint, sizeof(endpoint), "%s/models", model->base_url);
	snprintf(timeout_arg, sizeof(timeout_arg), "%d",
		 model->timeout_sec > 0 ? model->timeout_sec : 30);

	argv[argc++] = "uclient-fetch";
	argv[argc++] = "-q";
	argv[argc++] = "-T";
	argv[argc++] = timeout_arg;
	argv[argc++] = "-O";
	argv[argc++] = "-";
	if (api_key[0] != '\0') {
		snprintf(auth_arg, sizeof(auth_arg),
			 "--header=Authorization: Bearer %s", api_key);
		argv[argc++] = auth_arg;
	}
	argv[argc++] = endpoint;
	argv[argc] = NULL;

	if (pipe(pipe_fd) != 0) {
		snprintf(status_out, status_size, "%s", "pipe_error");
		return -1;
	}

	pid = fork();
	if (pid < 0) {
		close(pipe_fd[0]);
		close(pipe_fd[1]);
		snprintf(status_out, status_size, "%s", "fork_error");
		return -1;
	}

	if (pid == 0) {
		close(pipe_fd[0]);
		dup2(pipe_fd[1], STDOUT_FILENO);
		dup2(pipe_fd[1], STDERR_FILENO);
		close(pipe_fd[1]);
		execvp("uclient-fetch", (char *const *)argv);
		_exit(127);
	}

	close(pipe_fd[1]);
	while ((nread = read(pipe_fd[0], buffer, sizeof(buffer))) > 0) {
		size_t copy = (size_t)nread;
		if (copy > body_size - used - 1)
			copy = body_size - used - 1;
		if (copy > 0) {
			memcpy(body + used, buffer, copy);
			used += copy;
			body[used] = '\0';
		}
	}
	close(pipe_fd[0]);
	waitpid(pid, &status, 0);

	if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
		snprintf(status_out, status_size, "%s", "ok");
		return 0;
	}

	snprintf(status_out, status_size, "%s",
		 WIFEXITED(status) && WEXITSTATUS(status) == 127 ?
		 "transport_unavailable" : "fetch_error");
	return -1;
}

static int agent_call_model_once(const struct agent_model_request *request,
				 const struct agent_model_config *model,
				 const char *question,
				 struct agent_model_response *response)
{
	if (strncmp(request->endpoint, "http://127.0.0.1", 16) == 0 ||
	    strncmp(request->endpoint, "http://localhost", 16) == 0)
		return agent_call_local_model(request, model, question, response);

	return agent_call_uclient_model(request, model, question, response);
}

static void agent_call_model_with_retries(const struct agent_model_request *request,
					  const struct agent_model_config *model,
					  const char *question,
					  struct agent_model_response *response)
{
	int max_attempts = model->retry_count > 0 ? model->retry_count + 1 : 1;

	memset(response, 0, sizeof(*response));
	snprintf(response->status, sizeof(response->status), "%s", request->status);

	if (strcmp(request->status, "ready") != 0)
		return;

	for (int i = 0; i < max_attempts; i++) {
		response->attempts++;
		if (agent_call_model_once(request, model, question, response) == 0) {
			extract_openai_finish_reason(response->text,
						     response->finish_reason,
						     sizeof(response->finish_reason));
			response->reasoning_present =
				openai_has_reasoning_content(response->text);
			return;
		}
		if (strcmp(response->status, "unsupported_transport") == 0)
			return;
	}
}

static void print_agent_model_response_json(const struct agent_model_response *response)
{
	printf("  \"model_response\": {\n");
	printf("    \"status\": ");
	print_json_string(response->status);
	printf(",\n");
	printf("    \"attempts\": %d,\n", response->attempts);
	printf("    \"http_status\": %d,\n", response->http_status);
	printf("    \"finish_reason\": ");
	print_json_string(response->finish_reason);
	printf(",\n");
	printf("    \"reasoning_present\": %s,\n",
	       response->reasoning_present ? "true" : "false");
	printf("    \"body_preview\": ");
	print_json_string(response->text);
	printf("\n");
	printf("  }");
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

static void print_agent_model_config_json(const struct agent_model_config *model)
{
	printf("{\n");
	printf("      \"present\": %s,\n", model->present ? "true" : "false");
	printf("      \"enabled\": %s,\n", model->enabled ? "true" : "false");
	printf("      \"configured\": %s,\n", model->configured ? "true" : "false");
	printf("      \"name\": ");
	print_json_string(model->name);
	printf(",\n");
	printf("      \"priority\": %d,\n", model->priority);
	printf("      \"role\": ");
	print_json_string(model->role);
	printf(",\n");
	printf("      \"base_url\": ");
	print_json_string(model->base_url);
	printf(",\n");
	printf("      \"model\": ");
	print_json_string(model->model);
	printf(",\n");
	printf("      \"api_key_source_configured\": %s,\n",
	       (model->api_key[0] != '\0' || model->api_key_env[0] != '\0') ? "true" : "false");
	printf("      \"api_key_available\": %s,\n",
	       (model->api_key[0] != '\0' ||
		(model->api_key_env[0] != '\0' && getenv(model->api_key_env))) ? "true" : "false");
	printf("      \"api_key_env\": ");
	print_json_string(model->api_key_env);
	printf(",\n");
	printf("      \"timeout_sec\": %d,\n", model->timeout_sec);
	printf("      \"retry_count\": %d,\n", model->retry_count);
	printf("      \"max_tokens\": %d,\n", model->max_tokens);
	printf("      \"no_think\": %s\n", model->no_think ? "true" : "false");
	printf("    }");
}

static int EDGEPULSE_AGENT_UNUSED print_agent_status(void)
{
	struct agent_config agent;
	struct agent_model_config models[AGENT_MAX_MODELS];
	struct agent_model_config model;
	int model_count = 0;
	int config_rc = read_agent_config_models(&agent, models, AGENT_MAX_MODELS,
						 &model_count);
	const char *status = agent_models_status(&agent, models, model_count);

	if (model_count > 0)
		model = models[0];
	else
		init_agent_model_config(&model, 0);

	printf("{\n");
	printf("  \"agent\": {\n");
	printf("    \"enabled\": %s,\n", agent.enabled ? "true" : "false");
	printf("    \"local_only\": %s,\n", agent.local_only ? "true" : "false");
	printf("    \"memory_enabled\": %s,\n", agent.memory_enabled ? "true" : "false");
	printf("    \"shell_enabled\": %s,\n", agent.shell_enabled ? "true" : "false");
	printf("    \"ubus_enabled\": %s,\n", agent.ubus_enabled ? "true" : "false");
	printf("    \"chat_enabled\": %s,\n", agent.chat_enabled ? "true" : "false");
	printf("    \"mcp_enabled\": %s,\n", agent.mcp_enabled ? "true" : "false");
	printf("    \"policy_profile\": ");
	print_json_string(agent.policy_profile);
	printf(",\n");
	printf("    \"default_conversation_id\": ");
	print_json_string(agent.default_conversation_id);
	printf(",\n");
	printf("    \"db_path\": ");
	print_json_string(agent.db_path);
	printf(",\n");
	printf("    \"request_timeout_sec\": %d,\n", agent.request_timeout_sec);
	printf("    \"heartbeat_interval_sec\": %d,\n", agent.heartbeat_interval_sec);
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
	printf("    \"priority\": %d,\n", model.priority);
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
	printf("    \"retry_count\": %d,\n", model.retry_count);
	printf("    \"max_tokens\": %d,\n", model.max_tokens);
	printf("    \"no_think\": %s\n", model.no_think ? "true" : "false");
	printf("  },\n");
	printf("  \"models\": [\n");
	for (int i = 0; i < model_count; i++) {
		if (i > 0)
			printf(",\n");
		printf("    ");
		print_agent_model_config_json(&models[i]);
	}
	printf("\n");
	printf("  ],\n");
	printf("  \"status\": ");
	print_json_string(status);
	printf(",\n");
	printf("  \"config_status\": ");
	print_json_string(config_rc == 0 ? "loaded" : "defaults");
	printf(",\n");
	print_agent_validation(&agent, &model);
	printf("\n");
	printf("}\n");

	return 0;
}

static int EDGEPULSE_AGENT_UNUSED print_agent_models_list(void)
{
	struct agent_config agent;
	struct agent_model_config models[AGENT_MAX_MODELS];
	int model_count = 0;

	read_agent_config_models(&agent, models, AGENT_MAX_MODELS, &model_count);

	printf("{\n");
	printf("  \"status\": \"ok\",\n");
	printf("  \"models\": [\n");
	for (int i = 0; i < model_count; i++) {
		if (i > 0)
			printf(",\n");
		printf("    ");
		print_agent_model_config_json(&models[i]);
	}
	printf("\n");
	printf("  ]\n");
	printf("}\n");
	return 0;
}

static void print_remote_model_ids(const char *body)
{
	const char *cursor = body;
	int first = 1;

	printf("  \"models\": [\n");
	while (cursor && (cursor = strstr(cursor, "\"id\"")) != NULL) {
		const char *value = strchr(cursor + 4, ':');
		char id[256];
		size_t used = 0;

		if (!value)
			break;
		value++;
		while (*value == ' ' || *value == '\t' || *value == '\n' || *value == '\r')
			value++;
		if (*value != '"') {
			cursor = value;
			continue;
		}
		value++;
		while (*value && *value != '"' && used + 1 < sizeof(id)) {
			if (*value == '\\' && value[1])
				value++;
			id[used++] = *value++;
		}
		id[used] = '\0';
		if (used == 0) {
			cursor = value;
			continue;
		}
		if (!first)
			printf(",\n");
		printf("    { \"id\": ");
		print_json_string(id);
		printf(" }");
		first = 0;
		cursor = value;
	}
	printf("\n");
	printf("  ]");
}

static int EDGEPULSE_AGENT_UNUSED print_agent_remote_models(const char *section)
{
	struct agent_config agent;
	struct agent_model_config models[AGENT_MAX_MODELS];
	const struct agent_model_config *model = NULL;
	int model_count = 0;
	char body[8192];
	char fetch_status[64];

	read_agent_config_models(&agent, models, AGENT_MAX_MODELS, &model_count);
	for (int i = 0; i < model_count; i++) {
		if ((section && section[0] != '\0' && strcmp(models[i].name, section) == 0) ||
		    ((!section || section[0] == '\0') && models[i].enabled)) {
			model = &models[i];
			break;
		}
	}
	if (!model && model_count > 0)
		model = &models[0];

	printf("{\n");
	if (!model) {
		printf("  \"status\": \"not_configured\",\n");
		printf("  \"models\": []\n");
		printf("}\n");
		return 0;
	}

	agent_fetch_remote_models(model, body, sizeof(body), fetch_status,
				  sizeof(fetch_status));
	printf("  \"status\": ");
	print_json_string(fetch_status);
	printf(",\n");
	printf("  \"provider\": ");
	print_json_string(model->name);
	printf(",\n");
	printf("  \"base_url\": ");
	print_json_string(model->base_url);
	printf(",\n");
	printf("  \"api_key\": \"redacted\",\n");
	print_remote_model_ids(body);
	printf(",\n");
	printf("  \"body_preview\": ");
	print_json_string(body);
	printf("\n");
	printf("}\n");
	return strcmp(fetch_status, "ok") == 0 ? 0 : 1;
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

static int EDGEPULSE_AGENT_UNUSED print_agent_diagnose_conversation(
	const char *question, const char *conversation_id)
{
	struct agent_config agent;
	struct agent_model_config models[AGENT_MAX_MODELS];
	struct agent_model_config model;
	struct edgepulse_snapshot snapshot;
	const char *model_status;
	const char *fallback_answer;
	double memory_ratio;
	char request_id[64];
	char memory_summary[512];
	char answer[1024];
	char model_prompt[2048];
	char effective_conversation_id[64];
	struct agent_tool_result uname_result;
	struct agent_tool_result uptime_result;
	struct agent_tool_result ubus_board_result;
	struct agent_tool_result ubus_info_result;
	struct agent_tool_result ubus_network_result;
	struct agent_model_request model_request;
	struct agent_model_response model_response;
	char model_attempt_summary[512] = "";
	int have_uname = 0;
	int have_uptime = 0;
	int have_ubus_board = 0;
	int have_ubus_info = 0;
	int have_ubus_network = 0;
	char *const uname_argv[] = { "uname", "-a", NULL };
	char *const uptime_argv[] = { "uptime", NULL };
	char *const ubus_board_argv[] = { "ubus", "call", "system", "board", NULL };
	char *const ubus_info_argv[] = { "ubus", "call", "system", "info", NULL };
	char *const ubus_network_argv[] = { "ubus", "call", "network.interface", "dump", NULL };

	answer[0] = '\0';
	int model_count = 0;
	read_agent_config_models(&agent, models, AGENT_MAX_MODELS, &model_count);
	snprintf(effective_conversation_id, sizeof(effective_conversation_id), "%s",
		 conversation_id && conversation_id[0] ? conversation_id :
		 agent.default_conversation_id[0] ? agent.default_conversation_id :
		 "default");
	if (model_count > 0)
		model = models[0];
	else
		init_agent_model_config(&model, 0);
	model_status = agent_models_status(&agent, models, model_count);
	agent_make_request_id(request_id, sizeof(request_id));

	if (!agent.enabled) {
		agent_syslog(LOG_INFO, "request skipped status=disabled");
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

	snprintf(memory_summary, sizeof(memory_summary),
		 "Diagnostic request %s: load %.2f/%.2f/%.2f, memory %.2f%%, model_status=%s",
		 request_id, snapshot.load1, snapshot.load5, snapshot.load15,
		 memory_ratio * 100.0, model_status);
	agent_store_audit(agent.db_path, request_id, "request.started",
			  question ? question : "");
	agent_syslog(LOG_INFO, "request started request_id=%s model_status=%s local_only=%s policy=%s",
		     request_id, model_status, agent.local_only ? "true" : "false",
		     agent.policy_profile);
	agent_store_audit(agent.db_path, request_id, "tool.edgepulse_snapshot",
			  "Collected local read-only telemetry snapshot.");
	agent_syslog(LOG_INFO, "tool request_id=%s name=edgepulse_snapshot status=ok",
		     request_id);
	if (agent.shell_enabled) {
		agent_run_read_only_command("shell.uname", uname_argv,
					    agent.tool_timeout_sec,
					    agent.max_tool_output_bytes,
					    &uname_result);
		have_uname = 1;
		agent_store_audit(agent.db_path, request_id, "tool.shell.uname",
				  uname_result.status);
		agent_syslog(LOG_INFO, "tool request_id=%s name=shell.uname status=%s exit_code=%d",
			     request_id, uname_result.status, uname_result.exit_code);
		agent_run_read_only_command("shell.uptime", uptime_argv,
					    agent.tool_timeout_sec,
					    agent.max_tool_output_bytes,
					    &uptime_result);
		have_uptime = 1;
		agent_store_audit(agent.db_path, request_id, "tool.shell.uptime",
				  uptime_result.status);
		agent_syslog(LOG_INFO, "tool request_id=%s name=shell.uptime status=%s exit_code=%d",
			     request_id, uptime_result.status, uptime_result.exit_code);
	}
	if (agent.ubus_enabled) {
		agent_run_read_only_command("ubus.system.board", ubus_board_argv,
					    agent.tool_timeout_sec,
					    agent.max_tool_output_bytes,
					    &ubus_board_result);
		have_ubus_board = 1;
		agent_store_audit(agent.db_path, request_id, "tool.ubus.system.board",
				  ubus_board_result.status);
		agent_syslog(LOG_INFO, "tool request_id=%s name=ubus.system.board status=%s exit_code=%d",
			     request_id, ubus_board_result.status,
			     ubus_board_result.exit_code);
		agent_run_read_only_command("ubus.system.info", ubus_info_argv,
					    agent.tool_timeout_sec,
					    agent.max_tool_output_bytes,
					    &ubus_info_result);
		have_ubus_info = 1;
		agent_store_audit(agent.db_path, request_id, "tool.ubus.system.info",
				  ubus_info_result.status);
		agent_syslog(LOG_INFO, "tool request_id=%s name=ubus.system.info status=%s exit_code=%d",
			     request_id, ubus_info_result.status,
			     ubus_info_result.exit_code);
		agent_run_read_only_command("ubus.network.interface.dump",
					    ubus_network_argv,
					    agent.tool_timeout_sec,
					    agent.max_tool_output_bytes,
					    &ubus_network_result);
		have_ubus_network = 1;
		agent_store_audit(agent.db_path, request_id,
				  "tool.ubus.network.interface.dump",
				  ubus_network_result.status);
		agent_syslog(LOG_INFO, "tool request_id=%s name=ubus.network.interface.dump status=%s exit_code=%d",
			     request_id, ubus_network_result.status,
			     ubus_network_result.exit_code);
	}

	snprintf(model_prompt, sizeof(model_prompt),
		 "Write one short status sentence from these fields only: uptime_sec=%.0f, load1=%.2f, load5=%.2f, load15=%.2f, memory_used_pct=%.2f, tool_uname=%s, tool_uptime=%s, tool_board=%s, tool_system=%s, tool_network=%s.",
		 snapshot.uptime_sec, snapshot.load1,
		 snapshot.load5, snapshot.load15, memory_ratio * 100.0,
		 have_uname ? uname_result.status : "skipped",
		 have_uptime ? uptime_result.status : "skipped",
		 have_ubus_board ? ubus_board_result.status : "skipped",
		 have_ubus_info ? ubus_info_result.status : "skipped",
		 have_ubus_network ? ubus_network_result.status : "skipped");

	memset(&model_request, 0, sizeof(model_request));
	memset(&model_response, 0, sizeof(model_response));
	snprintf(model_response.status, sizeof(model_response.status), "%s",
		 model_status);
	for (int i = 0; i < (model_count > 0 ? model_count : 1); i++) {
		char extracted_answer[sizeof(answer)];
		int usable_answer = 0;

		if (model_count > 0)
			model = models[i];
		agent_build_model_request(&agent, &model, "analyzer", &model_request);
		agent_call_model_with_retries(&model_request, &model, model_prompt,
					      &model_response);
		agent_store_audit(agent.db_path, request_id, "model.route",
				  model_request.status);
		agent_store_audit(agent.db_path, request_id, "model.call",
				  model_response.status);
		agent_syslog(strcmp(model_response.status, "ok") == 0 ? LOG_INFO : LOG_WARNING,
			     "model request_id=%s provider=%s model=%s priority=%d route=%s status=%s attempts=%d http_status=%d finish_reason=%s reasoning_present=%s no_think=%s max_tokens=%d",
			     request_id, model_request.provider[0] ? model_request.provider : "-",
			     model_request.model[0] ? model_request.model : "-",
			     model.priority, model_request.status, model_response.status,
			     model_response.attempts, model_response.http_status,
			     model_response.finish_reason[0] ? model_response.finish_reason : "-",
			     model_response.reasoning_present ? "true" : "false",
			     model.no_think ? "true" : "false", model.max_tokens);

		if (model_attempt_summary[0] == '\0') {
			snprintf(model_attempt_summary, sizeof(model_attempt_summary),
				 "%s:%s", model.name[0] ? model.name : "-",
				 model_response.status);
		} else {
			size_t used = strlen(model_attempt_summary);
			snprintf(model_attempt_summary + used,
				 sizeof(model_attempt_summary) - used,
				 ",%s:%s", model.name[0] ? model.name : "-",
				 model_response.status);
		}

		if (strcmp(model_response.status, "ok") == 0 &&
		    extract_openai_message_content(model_response.text,
						   extracted_answer,
						   sizeof(extracted_answer)) == 0) {
			snprintf(answer, sizeof(answer), "%s", extracted_answer);
			usable_answer = 1;
		}
		if (usable_answer ||
		    strcmp(model_request.status, "agent_disabled") == 0 ||
		    strcmp(model_request.status, "local_only") == 0)
			break;
	}
	if (strcmp(model_response.status, "ok") == 0 &&
	    answer[0] != '\0') {
		/* Use the model's assistant message as the user-facing answer. */
	} else {
		if (strcmp(model_response.status, "ok") == 0) {
			snprintf(answer, sizeof(answer),
				 "The configured OpenAI-compatible model endpoint returned HTTP 200, but no assistant content was available. Local summary: uptime %.0f seconds, load %.2f/%.2f/%.2f, memory used %.2f%%. Tool evidence is included for grounding.",
				 snapshot.uptime_sec, snapshot.load1, snapshot.load5,
				 snapshot.load15, memory_ratio * 100.0);
		} else if (strcmp(model_status, "configured") == 0) {
			fallback_answer = "The model backend is configured, but the model call did not complete successfully. The local read-only telemetry and tool evidence are included for fallback diagnostics.";
			snprintf(answer, sizeof(answer), "%s", fallback_answer);
		} else {
			fallback_answer = "The AI agent MVP ran a local read-only diagnostic. Configure and enable a model backend to add model reasoning; local telemetry and policy findings are included in this response.";
			snprintf(answer, sizeof(answer), "%s", fallback_answer);
		}
	}
	agent_store_audit(agent.db_path, request_id, "policy.read_only",
			  agent_config_has_warnings(&agent, &model) ?
			  "Read-only policy active with configuration warnings." :
			  "Read-only policy active.");
	agent_syslog(LOG_INFO, "policy request_id=%s profile=%s mode=read_only",
		     request_id, agent.policy_profile);
	if (agent.memory_enabled)
		agent_store_memory(agent.db_path, memory_summary);
	agent_store_request(agent.db_path, request_id, question ? question : "",
			    model_status, answer);
	agent_store_conversation_messages(agent.db_path, effective_conversation_id, request_id,
					  question ? question : "", model_status,
					  answer);
	agent_syslog(LOG_INFO, "request completed request_id=%s model_status=%s answer_source=%s",
		     request_id, model_status,
		     (strcmp(model_response.status, "ok") == 0 && answer[0] != '\0') ?
		     "model_or_response" : "fallback");

	printf("{\n");
	printf("  \"status\": \"ok\",\n");
	printf("  \"request_id\": ");
	print_json_string(request_id);
	printf(",\n");
	printf("  \"conversation_id\": ");
	print_json_string(effective_conversation_id);
	printf(",\n");
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
	printf("    { \"name\": \"edgepulse_snapshot\", \"mode\": \"read_only\", \"status\": \"ok\" }");
	if (have_uname) {
		printf(",\n");
		print_agent_tool_json(&uname_result);
	}
	if (have_uptime) {
		printf(",\n");
		print_agent_tool_json(&uptime_result);
	}
	if (have_ubus_board) {
		printf(",\n");
		print_agent_tool_json(&ubus_board_result);
	}
	if (have_ubus_info) {
		printf(",\n");
		print_agent_tool_json(&ubus_info_result);
	}
	if (have_ubus_network) {
		printf(",\n");
		print_agent_tool_json(&ubus_network_result);
	}
	printf("\n");
	printf("  ],\n");
	print_agent_model_request_json(&model_request);
	printf(",\n");
	print_agent_model_response_json(&model_response);
	printf(",\n");
	printf("  \"model_failover\": {\n");
	printf("    \"attempts\": ");
	print_json_string(model_attempt_summary);
	printf(",\n");
	printf("    \"selected_provider\": ");
	print_json_string(model.name);
	printf(",\n");
	printf("    \"selected_priority\": %d\n", model.priority);
	printf("  },\n");
	print_agent_findings(&snapshot, &agent);
	printf("  \"snapshot\": {\n");
	printf("    \"uptime_sec\": %.2f,\n", snapshot.uptime_sec);
	printf("    \"load1\": %.2f,\n", snapshot.load1);
	printf("    \"load5\": %.2f,\n", snapshot.load5);
	printf("    \"load15\": %.2f,\n", snapshot.load15);
	printf("    \"memory_used_ratio\": %.4f\n", memory_ratio);
	printf("  },\n");
	printf("  \"answer\": ");
	print_json_string(answer);
	printf("\n");
	printf("}\n");

	return 0;
}

static int EDGEPULSE_AGENT_UNUSED print_agent_diagnose(const char *question)
{
	return print_agent_diagnose_conversation(question, "default");
}

static int EDGEPULSE_AGENT_UNUSED print_agent_memory_list(void)
{
	static const char *sql =
		"SELECT id, created_at, source, sensitivity, ttl_sec, content "
		"FROM agent_memory ORDER BY created_at DESC, id DESC LIMIT 50;";
	struct agent_config agent;
	struct agent_model_config model;
	sqlite3 *db = NULL;
	sqlite3_stmt *stmt = NULL;
	int first = 1;
	int rc;

	read_agent_config(&agent, &model);
	if (edgepulse_init_database(agent.db_path) != 0 ||
	    sqlite3_open(agent.db_path, &db) != SQLITE_OK)
		goto unavailable;
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
		goto unavailable;

	printf("{\n");
	printf("  \"memory\": [\n");
	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		if (!first)
			printf(",\n");
		first = 0;
		printf("    { \"id\": %lld, \"created_at\": %lld, \"source\": ",
		       sqlite3_column_int64(stmt, 0),
		       sqlite3_column_int64(stmt, 1));
		print_json_string((const char *)sqlite3_column_text(stmt, 2));
		printf(", \"sensitivity\": ");
		print_json_string((const char *)sqlite3_column_text(stmt, 3));
		printf(", \"ttl_sec\": %d, \"content\": ",
		       sqlite3_column_int(stmt, 4));
		print_json_string((const char *)sqlite3_column_text(stmt, 5));
		printf(" }");
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
	printf("{ \"memory\": [], \"status\": \"unavailable\" }\n");
	return 0;
}

static int EDGEPULSE_AGENT_UNUSED print_agent_audit_list(void)
{
	static const char *sql =
		"SELECT id, created_at, request_id, event_type, detail "
		"FROM agent_audit_log ORDER BY created_at DESC, id DESC LIMIT 100;";
	struct agent_config agent;
	struct agent_model_config model;
	sqlite3 *db = NULL;
	sqlite3_stmt *stmt = NULL;
	int first = 1;
	int rc;

	read_agent_config(&agent, &model);
	if (edgepulse_init_database(agent.db_path) != 0 ||
	    sqlite3_open(agent.db_path, &db) != SQLITE_OK)
		goto unavailable;
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
		goto unavailable;

	printf("{\n");
	printf("  \"audit\": [\n");
	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		if (!first)
			printf(",\n");
		first = 0;
		printf("    { \"id\": %lld, \"created_at\": %lld, \"request_id\": ",
		       sqlite3_column_int64(stmt, 0),
		       sqlite3_column_int64(stmt, 1));
		print_json_string((const char *)sqlite3_column_text(stmt, 2));
		printf(", \"event_type\": ");
		print_json_string((const char *)sqlite3_column_text(stmt, 3));
		printf(", \"detail\": ");
		print_json_string((const char *)sqlite3_column_text(stmt, 4));
		printf(" }");
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
	printf("{ \"audit\": [], \"status\": \"unavailable\" }\n");
	return 0;
}

static int EDGEPULSE_AGENT_UNUSED print_agent_chat_list(const char *conversation_id)
{
	static const char *conversation_sql =
		"SELECT conversation_id, created_at, updated_at, title "
		"FROM agent_conversations ORDER BY updated_at DESC LIMIT 25;";
	static const char *message_sql =
		"SELECT id, request_id, created_at, role, content, model_status "
		"FROM agent_messages WHERE conversation_id = ? "
		"ORDER BY created_at ASC, id ASC LIMIT 100;";
	struct agent_config agent;
	struct agent_model_config model;
	sqlite3 *db = NULL;
	sqlite3_stmt *stmt = NULL;
	int first = 1;
	int rc;

	read_agent_config(&agent, &model);
	if (edgepulse_init_database(agent.db_path) != 0 ||
	    sqlite3_open(agent.db_path, &db) != SQLITE_OK)
		goto unavailable;

	if (conversation_id && conversation_id[0]) {
		if (sqlite3_prepare_v2(db, message_sql, -1, &stmt, NULL) != SQLITE_OK)
			goto unavailable;
		sqlite3_bind_text(stmt, 1, conversation_id, -1, SQLITE_TRANSIENT);

		printf("{\n");
		printf("  \"conversation_id\": ");
		print_json_string(conversation_id);
		printf(",\n");
		printf("  \"messages\": [\n");
		while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
			if (!first)
				printf(",\n");
			first = 0;
			printf("    { \"id\": %lld, \"request_id\": ",
			       sqlite3_column_int64(stmt, 0));
			print_json_string((const char *)sqlite3_column_text(stmt, 1));
			printf(", \"created_at\": %lld, \"role\": ",
			       sqlite3_column_int64(stmt, 2));
			print_json_string((const char *)sqlite3_column_text(stmt, 3));
			printf(", \"content\": ");
			print_json_string((const char *)sqlite3_column_text(stmt, 4));
			printf(", \"model_status\": ");
			print_json_string((const char *)sqlite3_column_text(stmt, 5));
			printf(" }");
		}
		printf("\n");
		printf("  ]\n");
		printf("}\n");
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		return rc == SQLITE_DONE ? 0 : 1;
	}

	if (sqlite3_prepare_v2(db, conversation_sql, -1, &stmt, NULL) != SQLITE_OK)
		goto unavailable;
	printf("{\n");
	printf("  \"conversations\": [\n");
	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		if (!first)
			printf(",\n");
		first = 0;
		printf("    { \"conversation_id\": ");
		print_json_string((const char *)sqlite3_column_text(stmt, 0));
		printf(", \"created_at\": %lld, \"updated_at\": %lld, \"title\": ",
		       sqlite3_column_int64(stmt, 1),
		       sqlite3_column_int64(stmt, 2));
		print_json_string((const char *)sqlite3_column_text(stmt, 3));
		printf(" }");
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
	printf("{ \"conversations\": [], \"messages\": [], \"status\": \"unavailable\" }\n");
	return 0;
}

static int EDGEPULSE_AGENT_UNUSED delete_agent_memory(const char *id)
{
	struct agent_config agent;
	struct agent_model_config model;
	sqlite3 *db = NULL;
	sqlite3_stmt *stmt = NULL;
	const char *sql_one = "DELETE FROM agent_memory WHERE id = ?;";
	const char *sql_all = "DELETE FROM agent_memory;";
	long parsed_id;
	char *end = NULL;
	int rc;

	if (!id || id[0] == '\0') {
		fprintf(stderr, "edgepulse-ctl: memory delete requires an id or all\n");
		return 2;
	}

	read_agent_config(&agent, &model);
	if (edgepulse_init_database(agent.db_path) != 0 ||
	    sqlite3_open(agent.db_path, &db) != SQLITE_OK)
		return 1;

	if (strcmp(id, "all") == 0) {
		rc = sqlite3_exec(db, sql_all, NULL, NULL, NULL);
		sqlite3_close(db);
		printf("{ \"status\": %s }\n", rc == SQLITE_OK ? "\"deleted\"" : "\"error\"");
		return rc == SQLITE_OK ? 0 : 1;
	}

	parsed_id = strtol(id, &end, 10);
	if (parsed_id <= 0 || !end || *end != '\0') {
		sqlite3_close(db);
		fprintf(stderr, "edgepulse-ctl: invalid memory id: %s\n", id);
		return 2;
	}

	if (sqlite3_prepare_v2(db, sql_one, -1, &stmt, NULL) != SQLITE_OK) {
		sqlite3_close(db);
		return 1;
	}
	sqlite3_bind_int64(stmt, 1, parsed_id);
	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	sqlite3_close(db);
	printf("{ \"status\": %s, \"id\": %ld }\n",
	       rc == SQLITE_DONE ? "\"deleted\"" : "\"error\"", parsed_id);
	return rc == SQLITE_DONE ? 0 : 1;
}

static int agent_arg_has_confirm(int argc, char **argv)
{
	for (int i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--confirm") == 0)
			return 1;
	}
	return 0;
}

static const char *agent_arg_value(int argc, char **argv, const char *name)
{
	for (int i = 0; i + 1 < argc; i++) {
		if (strcmp(argv[i], name) == 0)
			return argv[i + 1];
	}
	return NULL;
}

static int agent_value_is_safe(const char *value, size_t max_len)
{
	size_t len;

	if (!value)
		return 0;
	len = strlen(value);
	if (len == 0 || len > max_len)
		return 0;
	for (size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)value[i];
		if (c < 32 || c == '\'' || c == '"' || c == '\\')
			return 0;
	}
	return 1;
}

static int agent_policy_allows_mutation(const struct agent_config *agent)
{
	return strcmp(agent->policy_profile, "operator_confirmed") == 0;
}

static void print_agent_confirmation_required(const char *action,
					      const char *reason)
{
	printf("{\n");
	printf("  \"status\": \"confirmation_required\",\n");
	printf("  \"action\": ");
	print_json_string(action);
	printf(",\n");
	printf("  \"reason\": ");
	print_json_string(reason);
	printf(",\n");
	printf("  \"required_policy_profile\": \"operator_confirmed\",\n");
	printf("  \"required_flag\": \"--confirm\"\n");
	printf("}\n");
}

static void print_agent_action_result(const char *action,
				      const char *request_id,
				      struct agent_tool_result *results,
				      int count)
{
	int ok = 1;

	for (int i = 0; i < count; i++) {
		if (strcmp(results[i].status, "ok") != 0)
			ok = 0;
	}

	printf("{\n");
	printf("  \"status\": ");
	print_json_string(ok ? "ok" : "error");
	printf(",\n");
	printf("  \"request_id\": ");
	print_json_string(request_id);
	printf(",\n");
	printf("  \"action\": ");
	print_json_string(action);
	printf(",\n");
	printf("  \"tools\": [\n");
	for (int i = 0; i < count; i++) {
		const char *mode =
			(strncmp(results[i].name, "uci.", 4) == 0 ||
			 strncmp(results[i].name, "netifd.", 7) == 0 ||
			 strcmp(results[i].name, "wifi.reload") == 0) ?
			"confirmed_mutation" : "read_only";
		if (i > 0)
			printf(",\n");
		printf("    ");
		print_agent_tool_json_mode(&results[i], mode);
	}
	printf("\n");
	printf("  ],\n");
	printf("  \"answer\": ");
	if (ok)
		print_json_string("Action completed. Review tool output and OpenWrt status to confirm the requested state.");
	else
		print_json_string("Action did not fully complete. Review tool status and output for the failing step.");
	printf("\n");
	printf("}\n");
}

static int EDGEPULSE_AGENT_UNUSED print_agent_action(int argc, char **argv)
{
	struct agent_config agent;
	struct agent_model_config model;
	char request_id[64];
	const char *action;
	struct agent_tool_result results[8];
	int result_count = 0;

	if (argc < 4) {
		fprintf(stderr, "Usage: edgepulse-ctl agent action <status|wifi-status|logs-recent|reconnect-wan|wifi-set> [--confirm] [options]\n");
		return 2;
	}

	read_agent_config(&agent, &model);
	action = argv[3];
	agent_make_request_id(request_id, sizeof(request_id));

	if (!agent.enabled) {
		printf("{ \"status\": \"disabled\", \"answer\": \"EdgePulse AI agent is disabled.\" }\n");
		return 0;
	}

	agent_store_audit(agent.db_path, request_id, "action.requested", action);
	agent_syslog(LOG_INFO, "action requested request_id=%s action=%s policy=%s",
		     request_id, action, agent.policy_profile);

	if (strcmp(action, "status") == 0 || strcmp(action, "wifi-status") == 0 ||
	    strcmp(action, "logs-recent") == 0) {
		char *const uptime_argv[] = { "uptime", NULL };
		char *const network_argv[] = { "ubus", "call", "network.interface", "dump", NULL };
		char *const wireless_argv[] = { "ubus", "call", "network.wireless", "status", NULL };
		char *const logs_argv[] = { "logread", "-l", "80", NULL };

		if (strcmp(action, "status") == 0) {
			agent_run_read_only_command("shell.uptime", uptime_argv,
						    agent.tool_timeout_sec,
						    agent.max_tool_output_bytes,
						    &results[result_count++]);
			agent_run_read_only_command("ubus.network.interface.dump",
						    network_argv,
						    agent.tool_timeout_sec,
						    agent.max_tool_output_bytes,
						    &results[result_count++]);
			agent_run_read_only_command("ubus.network.wireless.status",
						    wireless_argv,
						    agent.tool_timeout_sec,
						    agent.max_tool_output_bytes,
						    &results[result_count++]);
		} else if (strcmp(action, "wifi-status") == 0) {
			agent_run_read_only_command("ubus.network.wireless.status",
						    wireless_argv,
						    agent.tool_timeout_sec,
						    agent.max_tool_output_bytes,
						    &results[result_count++]);
		} else {
			agent_run_read_only_command("shell.logread", logs_argv,
						    agent.tool_timeout_sec,
						    agent.max_tool_output_bytes,
						    &results[result_count++]);
		}

		for (int i = 0; i < result_count; i++)
			agent_store_audit(agent.db_path, request_id, results[i].name,
					  results[i].status);
		print_agent_action_result(action, request_id, results, result_count);
		return 0;
	}

	if (strcmp(action, "reconnect-wan") == 0) {
		char *const ifdown_argv[] = { "ifdown", "wan", NULL };
		char *const ifup_argv[] = { "ifup", "wan", NULL };
		char *const network_argv[] = { "ubus", "call", "network.interface", "dump", NULL };

		if (!agent_policy_allows_mutation(&agent) ||
		    !agent_arg_has_confirm(argc - 4, argv + 4)) {
			print_agent_confirmation_required(action,
							  "Reconnecting WAN changes network state and may interrupt connectivity.");
			return 0;
		}

		agent_run_policy_command("netifd.ifdown.wan", ifdown_argv,
					 agent.tool_timeout_sec,
					 agent.max_tool_output_bytes, 1,
					 &results[result_count++]);
		agent_run_policy_command("netifd.ifup.wan", ifup_argv,
					 agent.tool_timeout_sec,
					 agent.max_tool_output_bytes, 1,
					 &results[result_count++]);
		agent_run_read_only_command("ubus.network.interface.dump",
					    network_argv,
					    agent.tool_timeout_sec,
					    agent.max_tool_output_bytes,
					    &results[result_count++]);
		for (int i = 0; i < result_count; i++)
			agent_store_audit(agent.db_path, request_id, results[i].name,
					  results[i].status);
		print_agent_action_result(action, request_id, results, result_count);
		return 0;
	}

	if (strcmp(action, "wifi-set") == 0) {
		const char *ssid = agent_arg_value(argc - 4, argv + 4, "--ssid");
		const char *key = agent_arg_value(argc - 4, argv + 4, "--key");
		const char *encryption = agent_arg_value(argc - 4, argv + 4, "--encryption");
		char ssid_arg[160];
		char key_arg[160];
		char encryption_arg[96];
		char disabled_arg[] = "wireless.@wifi-iface[0].disabled=0";
		char *uci_ssid_argv[] = { "uci", "set", ssid_arg, NULL };
		char *uci_key_argv[] = { "uci", "set", key_arg, NULL };
		char *uci_encryption_argv[] = { "uci", "set", encryption_arg, NULL };
		char *uci_disabled_argv[] = { "uci", "set", disabled_arg, NULL };
		char *const uci_commit_argv[] = { "uci", "commit", "wireless", NULL };
		char *const wifi_reload_argv[] = { "wifi", "reload", NULL };
		char *const wireless_argv[] = { "ubus", "call", "network.wireless", "status", NULL };

		if (!ssid || !agent_value_is_safe(ssid, 64)) {
			fprintf(stderr, "edgepulse-ctl: wifi-set requires a safe --ssid value up to 64 bytes\n");
			return 2;
		}
		if (key && (!agent_value_is_safe(key, 64) || strlen(key) < 8)) {
			fprintf(stderr, "edgepulse-ctl: --key must be 8-64 safe bytes when provided\n");
			return 2;
		}
		if (!encryption)
			encryption = key ? "psk2" : "none";
		if (strcmp(encryption, "none") != 0 && strcmp(encryption, "psk2") != 0 &&
		    strcmp(encryption, "sae-mixed") != 0) {
			fprintf(stderr, "edgepulse-ctl: --encryption must be none, psk2, or sae-mixed\n");
			return 2;
		}
		if (!agent_policy_allows_mutation(&agent) ||
		    !agent_arg_has_confirm(argc - 4, argv + 4)) {
			print_agent_confirmation_required(action,
							  "Changing Wi-Fi settings writes UCI wireless config and reloads Wi-Fi.");
			return 0;
		}

		snprintf(ssid_arg, sizeof(ssid_arg),
			 "wireless.@wifi-iface[0].ssid=%s", ssid);
		snprintf(encryption_arg, sizeof(encryption_arg),
			 "wireless.@wifi-iface[0].encryption=%s", encryption);
		snprintf(key_arg, sizeof(key_arg),
			 "wireless.@wifi-iface[0].key=%s", key ? key : "");

		agent_run_policy_command("uci.wireless.ssid", uci_ssid_argv,
					 agent.tool_timeout_sec,
					 agent.max_tool_output_bytes, 1,
					 &results[result_count++]);
		agent_run_policy_command("uci.wireless.encryption",
					 uci_encryption_argv,
					 agent.tool_timeout_sec,
					 agent.max_tool_output_bytes, 1,
					 &results[result_count++]);
		if (key) {
			agent_run_policy_command("uci.wireless.key", uci_key_argv,
						 agent.tool_timeout_sec,
						 agent.max_tool_output_bytes, 1,
						 &results[result_count++]);
		}
		agent_run_policy_command("uci.wireless.enable", uci_disabled_argv,
					 agent.tool_timeout_sec,
					 agent.max_tool_output_bytes, 1,
					 &results[result_count++]);
		agent_run_policy_command("uci.wireless.commit", uci_commit_argv,
					 agent.tool_timeout_sec,
					 agent.max_tool_output_bytes, 1,
					 &results[result_count++]);
		agent_run_policy_command("wifi.reload", wifi_reload_argv,
					 agent.tool_timeout_sec,
					 agent.max_tool_output_bytes, 1,
					 &results[result_count++]);
		agent_run_read_only_command("ubus.network.wireless.status",
					    wireless_argv,
					    agent.tool_timeout_sec,
					    agent.max_tool_output_bytes,
					    &results[result_count++]);
		for (int i = 0; i < result_count; i++)
			agent_store_audit(agent.db_path, request_id, results[i].name,
					  results[i].status);
		print_agent_action_result(action, request_id, results, result_count);
		return 0;
	}

	fprintf(stderr, "Usage: edgepulse-ctl agent action <status|wifi-status|logs-recent|reconnect-wan|wifi-set> [--confirm] [options]\n");
	return 2;
}

static int EDGEPULSE_AGENT_UNUSED print_agent_policy(void)
{
	struct agent_config agent;
	struct agent_model_config model;

	read_agent_config(&agent, &model);
	printf("{\n");
	printf("  \"policy_profile\": ");
	print_json_string(agent.policy_profile);
	printf(",\n");
	printf("  \"mode\": ");
	print_json_string(agent_policy_allows_mutation(&agent) ?
			  "operator_confirmed" : "read_only");
	printf(",\n");
	printf("  \"allowed_tools\": [\"edgepulse_snapshot\", \"shell.uname\", \"shell.uptime\", \"shell.logread\", \"ubus.system.board\", \"ubus.system.info\", \"ubus.network.interface.dump\", \"ubus.network.wireless.status\"],\n");
	printf("  \"confirmed_actions\": [\"reconnect-wan\", \"wifi-set\"],\n");
	printf("  \"blocked_categories\": [\"file_deletion\", \"package_install_remove\", \"firewall_change\", \"arbitrary_shell\", \"unconfirmed_mutation\"],\n");
	print_agent_validation(&agent, &model);
	printf("\n");
	printf("}\n");
	return 0;
}

static int EDGEPULSE_AGENT_UNUSED print_agent_mcp_methods(void)
{
	struct agent_config agent;
	struct agent_model_config model;

	read_agent_config(&agent, &model);
	printf("{\n");
	printf("  \"mcp_enabled\": %s,\n", agent.mcp_enabled ? "true" : "false");
	printf("  \"transport\": \"local_cli_adapter\",\n");
	printf("  \"methods\": [\n");
	printf("    { \"name\": \"edgepulse.status\", \"mode\": \"read_only\" },\n");
	printf("    { \"name\": \"edgepulse.agent.status\", \"mode\": \"read_only\" },\n");
	printf("    { \"name\": \"edgepulse.agent.chat.list\", \"mode\": \"read_only\" },\n");
	printf("    { \"name\": \"edgepulse.agent.chat.ask\", \"mode\": \"agent_request\" },\n");
	printf("    { \"name\": \"edgepulse.agent.action.run\", \"mode\": \"policy_gated\" },\n");
	printf("    { \"name\": \"edgepulse.agent.audit.list\", \"mode\": \"read_only\" },\n");
	printf("    { \"name\": \"edgepulse.ubus.status.network\", \"mode\": \"read_only\" },\n");
	printf("    { \"name\": \"edgepulse.ubus.status.wireless\", \"mode\": \"read_only\" },\n");
	printf("    { \"name\": \"edgepulse.uci.get.edgepulse\", \"mode\": \"read_only\" }\n");
	printf("  ]\n");
	printf("}\n");
	return 0;
}

static int print_agent_mcp_tool_call(const char *method, const char *tool_name,
				     char *const tool_argv[])
{
	struct agent_config agent;
	struct agent_model_config model;
	struct agent_tool_result result;
	char request_id[64];

	read_agent_config(&agent, &model);
	agent_make_request_id(request_id, sizeof(request_id));
	if (!agent.mcp_enabled) {
		printf("{ \"status\": \"disabled\", \"method\": ");
		print_json_string(method);
		printf(", \"answer\": \"EdgePulse local C MCP adapter is disabled by edgepulse.agent.mcp_enabled.\" }\n");
		return 0;
	}

	agent_store_audit(agent.db_path, request_id, "mcp.call", method);
	agent_run_read_only_command(tool_name, tool_argv, agent.tool_timeout_sec,
				    agent.max_tool_output_bytes, &result);
	agent_store_audit(agent.db_path, request_id, result.name, result.status);

	printf("{\n");
	printf("  \"status\": ");
	print_json_string(strcmp(result.status, "ok") == 0 ? "ok" : "error");
	printf(",\n");
	printf("  \"request_id\": ");
	print_json_string(request_id);
	printf(",\n");
	printf("  \"method\": ");
	print_json_string(method);
	printf(",\n");
	printf("  \"tool\":\n");
	print_agent_tool_json(&result);
	printf("\n");
	printf("}\n");
	return strcmp(result.status, "ok") == 0 ? 0 : 1;
}

static int EDGEPULSE_AGENT_UNUSED handle_agent_mcp_call(int argc, char **argv)
{
	struct agent_config agent;
	struct agent_model_config model;
	const char *method;

	if (argc < 5) {
		fprintf(stderr, "Usage: edgepulse-ctl agent mcp call <method> [args]\n");
		return 2;
	}

	read_agent_config(&agent, &model);
	method = argv[4];

	if (!agent.mcp_enabled) {
		printf("{ \"status\": \"disabled\", \"method\": ");
		print_json_string(method);
		printf(", \"answer\": \"EdgePulse local C MCP adapter is disabled by edgepulse.agent.mcp_enabled.\" }\n");
		return 0;
	}

	if (strcmp(method, "edgepulse.status") == 0)
		return print_status();
	if (strcmp(method, "edgepulse.agent.status") == 0)
		return print_agent_status();
	if (strcmp(method, "edgepulse.agent.audit.list") == 0)
		return print_agent_audit_list();
	if (strcmp(method, "edgepulse.agent.chat.list") == 0)
		return print_agent_chat_list(argc >= 6 ? argv[5] : NULL);
	if (strcmp(method, "edgepulse.agent.chat.ask") == 0) {
		if (argc < 7) {
			fprintf(stderr, "Usage: edgepulse-ctl agent mcp call edgepulse.agent.chat.ask <conversation_id> <message>\n");
			return 2;
		}
		return print_agent_diagnose_conversation(argv[6], argv[5]);
	}
	if (strcmp(method, "edgepulse.agent.action.run") == 0) {
		char *action_argv[16];
		int action_argc = 4;

		if (argc < 6) {
			fprintf(stderr, "Usage: edgepulse-ctl agent mcp call edgepulse.agent.action.run <action> [args]\n");
			return 2;
		}
		action_argv[0] = argv[0];
		action_argv[1] = argv[1];
		action_argv[2] = "action";
		action_argv[3] = argv[5];
		for (int i = 6; i < argc && action_argc < 15; i++)
			action_argv[action_argc++] = argv[i];
		action_argv[action_argc] = NULL;
		return print_agent_action(action_argc, action_argv);
	}
	if (strcmp(method, "edgepulse.ubus.status.network") == 0) {
		char *const ubus_network_argv[] = { "ubus", "call", "network.interface", "dump", NULL };
		return print_agent_mcp_tool_call(method, "ubus.network.interface.dump",
						 ubus_network_argv);
	}
	if (strcmp(method, "edgepulse.ubus.status.wireless") == 0) {
		char *const ubus_wireless_argv[] = { "ubus", "call", "network.wireless", "status", NULL };
		return print_agent_mcp_tool_call(method, "ubus.network.wireless.status",
						 ubus_wireless_argv);
	}
	if (strcmp(method, "edgepulse.uci.get.edgepulse") == 0) {
		char *const uci_argv[] = { "uci", "show", "edgepulse", NULL };
		return print_agent_mcp_tool_call(method, "uci.edgepulse.show",
						 uci_argv);
	}

	printf("{ \"status\": \"error\", \"method\": ");
	print_json_string(method);
	printf(", \"answer\": \"Unsupported EdgePulse local C MCP method.\" }\n");
	return 2;
}

static int EDGEPULSE_AGENT_UNUSED handle_agent_mcp_command(int argc, char **argv)
{
	if (argc >= 4 && strcmp(argv[3], "methods") == 0)
		return print_agent_mcp_methods();
	if (argc >= 4 && strcmp(argv[3], "call") == 0)
		return handle_agent_mcp_call(argc, argv);
	fprintf(stderr, "Usage: edgepulse-ctl agent mcp <methods|call> [method] [args]\n");
	return 2;
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
		fprintf(stderr, "Usage: edgepulse-ctl agent <status|diagnose|ask|chat|action|mcp|memory|models|audit|policy> [message]\n");
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

	if (strcmp(argv[2], "chat") == 0) {
		if (argc >= 4 && strcmp(argv[3], "ask") == 0) {
			if (argc < 6) {
				fprintf(stderr, "Usage: edgepulse-ctl agent chat ask <conversation_id> <message>\n");
				return 2;
			}
			return print_agent_diagnose_conversation(argv[5], argv[4]);
		}
		if (argc >= 4 && strcmp(argv[3], "list") == 0)
			return print_agent_chat_list(argc >= 5 ? argv[4] : NULL);
		fprintf(stderr, "Usage: edgepulse-ctl agent chat <ask|list> [conversation_id] [message]\n");
		return 2;
	}

	if (strcmp(argv[2], "action") == 0)
		return print_agent_action(argc, argv);

	if (strcmp(argv[2], "mcp") == 0)
		return handle_agent_mcp_command(argc, argv);

	if (strcmp(argv[2], "memory") == 0) {
		if (argc < 4) {
			fprintf(stderr, "Usage: edgepulse-ctl agent memory <list|delete> [id|all]\n");
			return 2;
		}
		if (strcmp(argv[3], "list") == 0)
			return print_agent_memory_list();
		if (strcmp(argv[3], "delete") == 0)
			return delete_agent_memory(argc >= 5 ? argv[4] : NULL);
		fprintf(stderr, "Usage: edgepulse-ctl agent memory <list|delete> [id|all]\n");
		return 2;
	}

	if (strcmp(argv[2], "models") == 0) {
		if (argc >= 4 && strcmp(argv[3], "list") == 0)
			return print_agent_models_list();
		if (argc >= 4 && strcmp(argv[3], "remote-list") == 0)
			return print_agent_remote_models(argc >= 5 ? argv[4] : NULL);
		fprintf(stderr, "Usage: edgepulse-ctl agent models <list|remote-list> [section]\n");
		return 2;
	}

	if (strcmp(argv[2], "policy") == 0) {
		if (argc >= 4 && strcmp(argv[3], "show") == 0)
			return print_agent_policy();
		fprintf(stderr, "Usage: edgepulse-ctl agent policy show\n");
		return 2;
	}

	if (strcmp(argv[2], "audit") == 0) {
		if (argc >= 4 && strcmp(argv[3], "list") == 0)
			return print_agent_audit_list();
		fprintf(stderr, "Usage: edgepulse-ctl agent audit list\n");
		return 2;
	}

	fprintf(stderr, "Usage: edgepulse-ctl agent <status|diagnose|ask|chat|action|mcp|memory|models|audit|policy> [message]\n");
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
