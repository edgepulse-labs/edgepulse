#include "edgepulse.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
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
	char db_path[AGENT_STR_SIZE];
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
	char text[1024];
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
	snprintf(agent->db_path, sizeof(agent->db_path), "%s", EDGEPULSE_DB_PATH);
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
		(strncmp(model->base_url, "http://127.0.0.1", 16) == 0 ||
		 strncmp(model->base_url, "http://localhost", 16) == 0 ||
		 model->api_key[0] != '\0' ||
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
	if (strcmp(agent->policy_profile, "read_only") != 0)
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

	if (strcmp(agent->policy_profile, "read_only") != 0)
		PRINT_VALIDATION_WARNING("Only the read_only policy profile is supported by the current MVP.");
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
		return 0;
	}
	return 0;
}

static int agent_run_read_only_command(const char *name, char *const argv[],
				       int timeout_sec, int output_limit,
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

	if (!agent_command_allowed(argv)) {
		snprintf(result->output, sizeof(result->output),
			 "Command blocked by read-only allowlist.");
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

static void print_agent_tool_json(const struct agent_tool_result *result)
{
	printf("    { \"name\": ");
	print_json_string(result->name);
	printf(", \"mode\": \"read_only\", \"status\": ");
	print_json_string(result->status);
	printf(", \"exit_code\": %d, \"elapsed_ms\": %ld, \"output\": ",
	       result->exit_code, result->elapsed_ms);
	print_json_string(result->output);
	printf(" }");
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

static void build_model_payload(char *payload, size_t size, const char *model,
				const char *question)
{
	char escaped_question[512];
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

	snprintf(payload, size,
		 "{\"model\":\"%s\",\"messages\":[{\"role\":\"system\",\"content\":\"You are EdgePulse, a read-only OpenWrt diagnostic assistant. Use only the provided local tool evidence.\"},{\"role\":\"user\",\"content\":\"%s\"}],\"stream\":false}",
		 model, escaped_question);
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
	char payload[1024];
	char header[1536];
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

	build_model_payload(payload, sizeof(payload), model->model, question);

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
		if (used >= sizeof(response->text) - 1)
			break;
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
		if (agent_call_local_model(request, model, question, response) == 0)
			return;
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
	printf("    \"db_path\": ");
	print_json_string(agent.db_path);
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
	printf(",\n");
	print_agent_validation(&agent, &model);
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
	const char *answer;
	double memory_ratio;
	char request_id[64];
	char memory_summary[512];
	struct agent_tool_result uname_result;
	struct agent_tool_result uptime_result;
	struct agent_tool_result ubus_board_result;
	struct agent_tool_result ubus_info_result;
	struct agent_tool_result ubus_network_result;
	struct agent_model_request model_request;
	struct agent_model_response model_response;
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

	read_agent_config(&agent, &model);
	model_status = agent_model_status(&agent, &model);
	agent_make_request_id(request_id, sizeof(request_id));

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
	agent_build_model_request(&agent, &model, "analyzer", &model_request);
	agent_call_model_with_retries(&model_request, &model, question,
				      &model_response);
	if (strcmp(model_response.status, "ok") == 0)
		answer = "The configured OpenAI-compatible model endpoint returned a response. The local read-only telemetry and tool evidence are included for grounding.";
	else if (strcmp(model_status, "configured") == 0)
		answer = "The model backend is configured, but the model call did not complete successfully. The local read-only telemetry and tool evidence are included for fallback diagnostics.";
	else
		answer = "The AI agent MVP ran a local read-only diagnostic. Configure and enable a model backend to add model reasoning; local telemetry and policy findings are included in this response.";

	snprintf(memory_summary, sizeof(memory_summary),
		 "Diagnostic request %s: load %.2f/%.2f/%.2f, memory %.2f%%, model_status=%s",
		 request_id, snapshot.load1, snapshot.load5, snapshot.load15,
		 memory_ratio * 100.0, model_status);
	agent_store_audit(agent.db_path, request_id, "request.started",
			  question ? question : "");
	agent_store_audit(agent.db_path, request_id, "tool.edgepulse_snapshot",
			  "Collected local read-only telemetry snapshot.");
	if (agent.shell_enabled) {
		agent_run_read_only_command("shell.uname", uname_argv,
					    agent.tool_timeout_sec,
					    agent.max_tool_output_bytes,
					    &uname_result);
		have_uname = 1;
		agent_store_audit(agent.db_path, request_id, "tool.shell.uname",
				  uname_result.status);
		agent_run_read_only_command("shell.uptime", uptime_argv,
					    agent.tool_timeout_sec,
					    agent.max_tool_output_bytes,
					    &uptime_result);
		have_uptime = 1;
		agent_store_audit(agent.db_path, request_id, "tool.shell.uptime",
				  uptime_result.status);
	}
	if (agent.ubus_enabled) {
		agent_run_read_only_command("ubus.system.board", ubus_board_argv,
					    agent.tool_timeout_sec,
					    agent.max_tool_output_bytes,
					    &ubus_board_result);
		have_ubus_board = 1;
		agent_store_audit(agent.db_path, request_id, "tool.ubus.system.board",
				  ubus_board_result.status);
		agent_run_read_only_command("ubus.system.info", ubus_info_argv,
					    agent.tool_timeout_sec,
					    agent.max_tool_output_bytes,
					    &ubus_info_result);
		have_ubus_info = 1;
		agent_store_audit(agent.db_path, request_id, "tool.ubus.system.info",
				  ubus_info_result.status);
		agent_run_read_only_command("ubus.network.interface.dump",
					    ubus_network_argv,
					    agent.tool_timeout_sec,
					    agent.max_tool_output_bytes,
					    &ubus_network_result);
		have_ubus_network = 1;
		agent_store_audit(agent.db_path, request_id,
				  "tool.ubus.network.interface.dump",
				  ubus_network_result.status);
	}
	agent_store_audit(agent.db_path, request_id, "model.route",
			  model_request.status);
	agent_store_audit(agent.db_path, request_id, "model.call",
			  model_response.status);
	agent_store_audit(agent.db_path, request_id, "policy.read_only",
			  agent_config_has_warnings(&agent, &model) ?
			  "Read-only policy active with configuration warnings." :
			  "Read-only policy active.");
	if (agent.memory_enabled)
		agent_store_memory(agent.db_path, memory_summary);
	agent_store_request(agent.db_path, request_id, question ? question : "",
			    model_status, answer);

	printf("{\n");
	printf("  \"status\": \"ok\",\n");
	printf("  \"request_id\": ");
	print_json_string(request_id);
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

static int EDGEPULSE_AGENT_UNUSED print_agent_policy(void)
{
	struct agent_config agent;
	struct agent_model_config model;

	read_agent_config(&agent, &model);
	printf("{\n");
	printf("  \"policy_profile\": ");
	print_json_string(agent.policy_profile);
	printf(",\n");
	printf("  \"mode\": \"read_only\",\n");
	printf("  \"allowed_tools\": [\"edgepulse_snapshot\", \"shell.uname\", \"shell.uptime\", \"ubus.system.board\", \"ubus.system.info\", \"ubus.network.interface.dump\"],\n");
	printf("  \"blocked_categories\": [\"file_deletion\", \"uci_mutation\", \"service_restart\", \"package_install_remove\", \"firewall_change\", \"arbitrary_shell\"],\n");
	print_agent_validation(&agent, &model);
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

	if (strcmp(argv[2], "policy") == 0) {
		if (argc >= 4 && strcmp(argv[3], "show") == 0)
			return print_agent_policy();
		fprintf(stderr, "Usage: edgepulse-ctl agent policy show\n");
		return 2;
	}

	fprintf(stderr, "Usage: edgepulse-ctl agent <status|diagnose|ask|memory|policy> [message]\n");
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
