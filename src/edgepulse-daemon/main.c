#include "edgepulse.h"

#include <errno.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef EDGEPULSE_ENABLE_AI_AGENT
#include <sqlite3.h>
#endif

#if defined(EDGEPULSE_ENABLE_AI_AGENT) && defined(EDGEPULSE_ENABLE_UBUS)
#include <libubox/blobmsg_json.h>
#include <libubox/uloop.h>
#include <libubus.h>
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

#ifdef EDGEPULSE_ENABLE_UBUS
static const char *agent_ubus_db_path = EDGEPULSE_DB_PATH;
static struct ubus_context *agent_ubus_ctx;
static struct uloop_timeout agent_heartbeat_timeout;
static int agent_heartbeat_interval_sec = 60;
static struct blob_buf agent_ubus_buf;

static int capture_agent_ctl(char *const argv[], char *out, size_t out_size)
{
	int pipefd[2];
	pid_t pid;
	ssize_t nread;
	size_t used = 0;
	int status = 0;

	if (!out || out_size == 0)
		return -1;
	out[0] = '\0';
	if (pipe(pipefd) != 0)
		return -1;

	pid = fork();
	if (pid < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		return -1;
	}
	if (pid == 0) {
		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		dup2(pipefd[1], STDERR_FILENO);
		close(pipefd[1]);
		execvp("edgepulse-ctl", argv);
		_exit(127);
	}

	close(pipefd[1]);
	while ((nread = read(pipefd[0], out + used, out_size - used - 1)) > 0) {
		used += (size_t)nread;
		out[used] = '\0';
		if (used + 1 >= out_size)
			break;
	}
	close(pipefd[0]);
	waitpid(pid, &status, 0);
	return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

static void agent_ubus_reply(struct ubus_context *ctx, struct ubus_request_data *req,
			     const char *method, int rc, const char *output)
{
	blob_buf_init(&agent_ubus_buf, 0);
	blobmsg_add_string(&agent_ubus_buf, "method", method);
	blobmsg_add_u32(&agent_ubus_buf, "exit_code", (uint32_t)rc);
	blobmsg_add_string(&agent_ubus_buf, "output", output ? output : "");
	ubus_send_reply(ctx, req, agent_ubus_buf.head);
}

static int agent_ubus_simple_call(struct ubus_context *ctx,
				  struct ubus_object *obj __attribute__((unused)),
				  struct ubus_request_data *req,
				  const char *method,
				  struct blob_attr *msg __attribute__((unused)))
{
	char output[16384];
	char *argv[] = { "edgepulse-ctl", "agent", NULL, NULL, NULL };
	int rc;

	argv[2] = (char *)method;
	if (strcmp(method, "policy.show") == 0) {
		argv[2] = "policy";
		argv[3] = "show";
	}
	if (strcmp(method, "audit.list") == 0) {
		argv[2] = "audit";
		argv[3] = "list";
	}
	if (strcmp(method, "skill.list") == 0) {
		argv[2] = "skill";
		argv[3] = "list";
	}
	rc = capture_agent_ctl(argv, output, sizeof(output));
	agent_ubus_reply(ctx, req, method, rc, output);
	return 0;
}

enum {
	CHAT_CONVERSATION_ID,
	CHAT_MESSAGE,
	CHAT_MAX
};

static const struct blobmsg_policy chat_ask_policy[CHAT_MAX] = {
	[CHAT_CONVERSATION_ID] = { .name = "conversation_id", .type = BLOBMSG_TYPE_STRING },
	[CHAT_MESSAGE] = { .name = "message", .type = BLOBMSG_TYPE_STRING },
};

static int agent_ubus_chat_ask(struct ubus_context *ctx,
			       struct ubus_object *obj __attribute__((unused)),
			       struct ubus_request_data *req,
			       const char *method,
			       struct blob_attr *msg)
{
	struct blob_attr *tb[CHAT_MAX];
	const char *conversation_id = "default";
	const char *message = "";
	char output[16384];
	char *argv[] = {
		"edgepulse-ctl", "agent", "chat", "ask",
		(char *)conversation_id, (char *)message, NULL
	};
	int rc;

	blobmsg_parse(chat_ask_policy, CHAT_MAX, tb, blob_data(msg), blob_len(msg));
	if (tb[CHAT_CONVERSATION_ID])
		conversation_id = blobmsg_get_string(tb[CHAT_CONVERSATION_ID]);
	if (tb[CHAT_MESSAGE])
		message = blobmsg_get_string(tb[CHAT_MESSAGE]);
	argv[4] = (char *)conversation_id;
	argv[5] = (char *)message;
	rc = capture_agent_ctl(argv, output, sizeof(output));
	agent_ubus_reply(ctx, req, method, rc, output);
	return 0;
}

static int agent_ubus_chat_list(struct ubus_context *ctx,
				struct ubus_object *obj __attribute__((unused)),
				struct ubus_request_data *req,
				const char *method,
				struct blob_attr *msg)
{
	struct blob_attr *tb[CHAT_MAX];
	const char *conversation_id = NULL;
	char output[16384];
	char *argv[] = {
		"edgepulse-ctl", "agent", "chat", "list", NULL, NULL
	};
	int rc;

	blobmsg_parse(chat_ask_policy, CHAT_MAX, tb, blob_data(msg), blob_len(msg));
	if (tb[CHAT_CONVERSATION_ID]) {
		conversation_id = blobmsg_get_string(tb[CHAT_CONVERSATION_ID]);
		argv[4] = (char *)conversation_id;
	}
	rc = capture_agent_ctl(argv, output, sizeof(output));
	agent_ubus_reply(ctx, req, method, rc, output);
	return 0;
}

enum {
	ACTION_NAME,
	ACTION_CONFIRM,
	ACTION_SSID,
	ACTION_KEY,
	ACTION_ENCRYPTION,
	ACTION_WIFI_INTERFACE,
	ACTION_CONTAINS,
	ACTION_LEVEL,
	ACTION_MAX
};

static const struct blobmsg_policy action_policy[ACTION_MAX] = {
	[ACTION_NAME] = { .name = "action", .type = BLOBMSG_TYPE_STRING },
	[ACTION_CONFIRM] = { .name = "confirm", .type = BLOBMSG_TYPE_BOOL },
	[ACTION_SSID] = { .name = "ssid", .type = BLOBMSG_TYPE_STRING },
	[ACTION_KEY] = { .name = "key", .type = BLOBMSG_TYPE_STRING },
	[ACTION_ENCRYPTION] = { .name = "encryption", .type = BLOBMSG_TYPE_STRING },
	[ACTION_WIFI_INTERFACE] = { .name = "wifi_interface", .type = BLOBMSG_TYPE_STRING },
	[ACTION_CONTAINS] = { .name = "contains", .type = BLOBMSG_TYPE_STRING },
	[ACTION_LEVEL] = { .name = "level", .type = BLOBMSG_TYPE_STRING },
};

enum {
	SKILL_ID,
	SKILL_CONFIRM,
	SKILL_SSID,
	SKILL_KEY,
	SKILL_ENCRYPTION,
	SKILL_MAX
};

static const struct blobmsg_policy skill_policy[SKILL_MAX] = {
	[SKILL_ID] = { .name = "skill_id", .type = BLOBMSG_TYPE_STRING },
	[SKILL_CONFIRM] = { .name = "confirm", .type = BLOBMSG_TYPE_BOOL },
	[SKILL_SSID] = { .name = "ssid", .type = BLOBMSG_TYPE_STRING },
	[SKILL_KEY] = { .name = "key", .type = BLOBMSG_TYPE_STRING },
	[SKILL_ENCRYPTION] = { .name = "encryption", .type = BLOBMSG_TYPE_STRING },
};

enum {
	MCP_NAME,
	MCP_CONVERSATION_ID,
	MCP_MESSAGE,
	MCP_ACTION,
	MCP_SKILL_ID,
	MCP_INTERFACE,
	MCP_WIFI_INTERFACE,
	MCP_CONFIRM,
	MCP_SSID,
	MCP_KEY,
	MCP_ENCRYPTION,
	MCP_CONTAINS,
	MCP_LEVEL,
	MCP_MAX
};

static const struct blobmsg_policy mcp_tool_policy[MCP_MAX] = {
	[MCP_NAME] = { .name = "name", .type = BLOBMSG_TYPE_STRING },
	[MCP_CONVERSATION_ID] = { .name = "conversation_id", .type = BLOBMSG_TYPE_STRING },
	[MCP_MESSAGE] = { .name = "message", .type = BLOBMSG_TYPE_STRING },
	[MCP_ACTION] = { .name = "action", .type = BLOBMSG_TYPE_STRING },
	[MCP_SKILL_ID] = { .name = "skill_id", .type = BLOBMSG_TYPE_STRING },
	[MCP_INTERFACE] = { .name = "interface", .type = BLOBMSG_TYPE_STRING },
	[MCP_WIFI_INTERFACE] = { .name = "wifi_interface", .type = BLOBMSG_TYPE_STRING },
	[MCP_CONFIRM] = { .name = "confirm", .type = BLOBMSG_TYPE_BOOL },
	[MCP_SSID] = { .name = "ssid", .type = BLOBMSG_TYPE_STRING },
	[MCP_KEY] = { .name = "key", .type = BLOBMSG_TYPE_STRING },
	[MCP_ENCRYPTION] = { .name = "encryption", .type = BLOBMSG_TYPE_STRING },
	[MCP_CONTAINS] = { .name = "contains", .type = BLOBMSG_TYPE_STRING },
	[MCP_LEVEL] = { .name = "level", .type = BLOBMSG_TYPE_STRING },
};

static int agent_ubus_skill_plan(struct ubus_context *ctx,
				 struct ubus_object *obj __attribute__((unused)),
				 struct ubus_request_data *req,
				 const char *method,
				 struct blob_attr *msg)
{
	struct blob_attr *tb[SKILL_MAX];
	char output[16384];
	char *argv[] = { "edgepulse-ctl", "agent", "skill", "plan", NULL, NULL };
	int rc;

	blobmsg_parse(skill_policy, SKILL_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[SKILL_ID]) {
		agent_ubus_reply(ctx, req, method, 2,
				 "{\"status\":\"error\",\"answer\":\"skill.plan requires skill_id\"}");
		return 0;
	}

	argv[4] = (char *)blobmsg_get_string(tb[SKILL_ID]);
	rc = capture_agent_ctl(argv, output, sizeof(output));
	agent_ubus_reply(ctx, req, method, rc, output);
	return 0;
}

static int agent_ubus_skill_run(struct ubus_context *ctx,
				struct ubus_object *obj __attribute__((unused)),
				struct ubus_request_data *req,
				const char *method,
				struct blob_attr *msg)
{
	struct blob_attr *tb[SKILL_MAX];
	char output[16384];
	char *argv[14];
	int argc = 0;
	int rc;

	blobmsg_parse(skill_policy, SKILL_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[SKILL_ID]) {
		agent_ubus_reply(ctx, req, method, 2,
				 "{\"status\":\"error\",\"answer\":\"skill.run requires skill_id\"}");
		return 0;
	}

	argv[argc++] = "edgepulse-ctl";
	argv[argc++] = "agent";
	argv[argc++] = "skill";
	argv[argc++] = "run";
	argv[argc++] = (char *)blobmsg_get_string(tb[SKILL_ID]);
	if (tb[SKILL_CONFIRM] && blobmsg_get_bool(tb[SKILL_CONFIRM]))
		argv[argc++] = "--confirm";
	if (tb[SKILL_SSID]) {
		argv[argc++] = "--ssid";
		argv[argc++] = (char *)blobmsg_get_string(tb[SKILL_SSID]);
	}
	if (tb[SKILL_KEY]) {
		argv[argc++] = "--key";
		argv[argc++] = (char *)blobmsg_get_string(tb[SKILL_KEY]);
	}
	if (tb[SKILL_ENCRYPTION]) {
		argv[argc++] = "--encryption";
		argv[argc++] = (char *)blobmsg_get_string(tb[SKILL_ENCRYPTION]);
	}
	argv[argc] = NULL;
	rc = capture_agent_ctl(argv, output, sizeof(output));
	agent_ubus_reply(ctx, req, method, rc, output);
	return 0;
}

static int agent_ubus_mcp_tools_list(struct ubus_context *ctx,
				     struct ubus_object *obj __attribute__((unused)),
				     struct ubus_request_data *req,
				     const char *method,
				     struct blob_attr *msg __attribute__((unused)))
{
	char output[16384];
	char *argv[] = { "edgepulse-ctl", "agent", "mcp", "methods", NULL };
	int rc = capture_agent_ctl(argv, output, sizeof(output));

	agent_ubus_reply(ctx, req, method, rc, output);
	return 0;
}

static int agent_ubus_mcp_tools_call(struct ubus_context *ctx,
				     struct ubus_object *obj __attribute__((unused)),
				     struct ubus_request_data *req,
				     const char *method,
				     struct blob_attr *msg)
{
	struct blob_attr *tb[MCP_MAX];
	char output[16384];
	char *argv[22];
	const char *name;
	int argc = 0;
	int rc;

	blobmsg_parse(mcp_tool_policy, MCP_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[MCP_NAME]) {
		agent_ubus_reply(ctx, req, method, 2,
				 "{\"status\":\"error\",\"answer\":\"mcp.tools.call requires name\"}");
		return 0;
	}

	name = blobmsg_get_string(tb[MCP_NAME]);
	argv[argc++] = "edgepulse-ctl";
	argv[argc++] = "agent";
	argv[argc++] = "mcp";
	argv[argc++] = "call";
	argv[argc++] = (char *)name;

	if (strcmp(name, "edgepulse.agent.chat.ask") == 0) {
		if (!tb[MCP_MESSAGE]) {
			agent_ubus_reply(ctx, req, method, 2,
					 "{\"status\":\"error\",\"answer\":\"edgepulse.agent.chat.ask requires message\"}");
			return 0;
		}
		argv[argc++] = tb[MCP_CONVERSATION_ID] ?
			(char *)blobmsg_get_string(tb[MCP_CONVERSATION_ID]) :
			"default";
		argv[argc++] = (char *)blobmsg_get_string(tb[MCP_MESSAGE]);
	} else if (strcmp(name, "edgepulse.agent.chat.list") == 0) {
		if (tb[MCP_CONVERSATION_ID])
			argv[argc++] = (char *)blobmsg_get_string(tb[MCP_CONVERSATION_ID]);
	} else if (strcmp(name, "edgepulse.agent.action.run") == 0) {
		if (!tb[MCP_ACTION]) {
			agent_ubus_reply(ctx, req, method, 2,
					 "{\"status\":\"error\",\"answer\":\"edgepulse.agent.action.run requires action\"}");
			return 0;
		}
		argv[argc++] = (char *)blobmsg_get_string(tb[MCP_ACTION]);
	} else if (strcmp(name, "edgepulse.agent.skill.plan") == 0 ||
		   strcmp(name, "edgepulse.agent.skill.run") == 0) {
		if (!tb[MCP_SKILL_ID]) {
			agent_ubus_reply(ctx, req, method, 2,
					 "{\"status\":\"error\",\"answer\":\"edgepulse.agent.skill requires skill_id\"}");
			return 0;
		}
		argv[argc++] = (char *)blobmsg_get_string(tb[MCP_SKILL_ID]);
	}

	if ((strcmp(name, "edgepulse.agent.action.run") == 0 ||
	     strcmp(name, "edgepulse.agent.skill.run") == 0) &&
	    tb[MCP_CONFIRM] && blobmsg_get_bool(tb[MCP_CONFIRM]))
		argv[argc++] = "--confirm";
	if ((strcmp(name, "edgepulse.agent.action.run") == 0 ||
	     strcmp(name, "edgepulse.agent.skill.run") == 0) &&
	    tb[MCP_SSID]) {
		argv[argc++] = "--ssid";
		argv[argc++] = (char *)blobmsg_get_string(tb[MCP_SSID]);
	}
	if ((strcmp(name, "edgepulse.agent.action.run") == 0 ||
	     strcmp(name, "edgepulse.agent.skill.run") == 0) &&
	    tb[MCP_KEY]) {
		argv[argc++] = "--key";
		argv[argc++] = (char *)blobmsg_get_string(tb[MCP_KEY]);
	}
	if ((strcmp(name, "edgepulse.agent.action.run") == 0 ||
	     strcmp(name, "edgepulse.agent.skill.run") == 0) &&
	    tb[MCP_ENCRYPTION]) {
		argv[argc++] = "--encryption";
		argv[argc++] = (char *)blobmsg_get_string(tb[MCP_ENCRYPTION]);
	}
	if ((strcmp(name, "edgepulse.agent.action.run") == 0 ||
	     strcmp(name, "edgepulse.agent.skill.run") == 0) &&
	    tb[MCP_INTERFACE]) {
		argv[argc++] = "--interface";
		argv[argc++] = (char *)blobmsg_get_string(tb[MCP_INTERFACE]);
	}
	if (strcmp(name, "edgepulse.agent.action.run") == 0 &&
	    tb[MCP_WIFI_INTERFACE]) {
		argv[argc++] = "--wifi-interface";
		argv[argc++] = (char *)blobmsg_get_string(tb[MCP_WIFI_INTERFACE]);
	}
	if (strcmp(name, "edgepulse.agent.action.run") == 0 &&
	    tb[MCP_CONTAINS]) {
		argv[argc++] = "--contains";
		argv[argc++] = (char *)blobmsg_get_string(tb[MCP_CONTAINS]);
	}
	if (strcmp(name, "edgepulse.agent.action.run") == 0 &&
	    tb[MCP_LEVEL]) {
		argv[argc++] = "--level";
		argv[argc++] = (char *)blobmsg_get_string(tb[MCP_LEVEL]);
	}
	argv[argc] = NULL;

	rc = capture_agent_ctl(argv, output, sizeof(output));
	agent_ubus_reply(ctx, req, method, rc, output);
	return 0;
}

static int agent_ubus_action_run(struct ubus_context *ctx,
				 struct ubus_object *obj __attribute__((unused)),
				 struct ubus_request_data *req,
				 const char *method,
				 struct blob_attr *msg)
{
	struct blob_attr *tb[ACTION_MAX];
	char output[16384];
	char *argv[18];
	int argc = 0;
	int rc;

	blobmsg_parse(action_policy, ACTION_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[ACTION_NAME]) {
		agent_ubus_reply(ctx, req, method, 2,
				 "{\"status\":\"error\",\"answer\":\"action.run requires action\"}");
		return 0;
	}

	argv[argc++] = "edgepulse-ctl";
	argv[argc++] = "agent";
	argv[argc++] = "action";
	argv[argc++] = (char *)blobmsg_get_string(tb[ACTION_NAME]);
	if (tb[ACTION_CONFIRM] && blobmsg_get_bool(tb[ACTION_CONFIRM]))
		argv[argc++] = "--confirm";
	if (tb[ACTION_SSID]) {
		argv[argc++] = "--ssid";
		argv[argc++] = (char *)blobmsg_get_string(tb[ACTION_SSID]);
	}
	if (tb[ACTION_KEY]) {
		argv[argc++] = "--key";
		argv[argc++] = (char *)blobmsg_get_string(tb[ACTION_KEY]);
	}
	if (tb[ACTION_ENCRYPTION]) {
		argv[argc++] = "--encryption";
		argv[argc++] = (char *)blobmsg_get_string(tb[ACTION_ENCRYPTION]);
	}
	if (tb[ACTION_WIFI_INTERFACE]) {
		argv[argc++] = "--wifi-interface";
		argv[argc++] = (char *)blobmsg_get_string(tb[ACTION_WIFI_INTERFACE]);
	}
	if (tb[ACTION_CONTAINS]) {
		argv[argc++] = "--contains";
		argv[argc++] = (char *)blobmsg_get_string(tb[ACTION_CONTAINS]);
	}
	if (tb[ACTION_LEVEL]) {
		argv[argc++] = "--level";
		argv[argc++] = (char *)blobmsg_get_string(tb[ACTION_LEVEL]);
	}
	argv[argc] = NULL;
	rc = capture_agent_ctl(argv, output, sizeof(output));
	agent_ubus_reply(ctx, req, method, rc, output);
	return 0;
}

static int agent_ubus_status(struct ubus_context *ctx, struct ubus_object *obj,
			     struct ubus_request_data *req, const char *method,
			     struct blob_attr *msg)
{
	return agent_ubus_simple_call(ctx, obj, req, "status", msg);
}

static int agent_ubus_policy_show(struct ubus_context *ctx, struct ubus_object *obj,
				  struct ubus_request_data *req, const char *method,
				  struct blob_attr *msg)
{
	return agent_ubus_simple_call(ctx, obj, req, "policy.show", msg);
}

static int agent_ubus_audit_list(struct ubus_context *ctx, struct ubus_object *obj,
				 struct ubus_request_data *req, const char *method,
				 struct blob_attr *msg)
{
	return agent_ubus_simple_call(ctx, obj, req, "audit.list", msg);
}

static int agent_ubus_skill_list(struct ubus_context *ctx, struct ubus_object *obj,
				 struct ubus_request_data *req, const char *method,
				 struct blob_attr *msg)
{
	return agent_ubus_simple_call(ctx, obj, req, "skill.list", msg);
}

static const struct ubus_method agent_ubus_methods[] = {
	UBUS_METHOD_NOARG("status", agent_ubus_status),
	UBUS_METHOD_NOARG("skill.list", agent_ubus_skill_list),
	UBUS_METHOD("skill.plan", agent_ubus_skill_plan, skill_policy),
	UBUS_METHOD("skill.run", agent_ubus_skill_run, skill_policy),
	UBUS_METHOD_NOARG("mcp.tools.list", agent_ubus_mcp_tools_list),
	UBUS_METHOD("mcp.tools.call", agent_ubus_mcp_tools_call, mcp_tool_policy),
	UBUS_METHOD("chat.ask", agent_ubus_chat_ask, chat_ask_policy),
	UBUS_METHOD("chat.list", agent_ubus_chat_list, chat_ask_policy),
	UBUS_METHOD("action.run", agent_ubus_action_run, action_policy),
	UBUS_METHOD_NOARG("policy.show", agent_ubus_policy_show),
	UBUS_METHOD_NOARG("audit.list", agent_ubus_audit_list),
};

static struct ubus_object_type agent_ubus_object_type =
	UBUS_OBJECT_TYPE("edgepulse.agent", agent_ubus_methods);

static struct ubus_object agent_ubus_object = {
	.name = "edgepulse.agent",
	.type = &agent_ubus_object_type,
	.methods = agent_ubus_methods,
	.n_methods = ARRAY_SIZE(agent_ubus_methods),
};

static void agent_heartbeat_cb(struct uloop_timeout *timeout)
{
	if (!keep_running) {
		uloop_end();
		return;
	}
	if (store_agent_audit(agent_ubus_db_path, "agentd.heartbeat",
			      "EdgePulse AI agent runtime monitor heartbeat") != 0) {
		fprintf(stderr, "edgepulse: failed to write agent heartbeat: %s\n",
			strerror(errno));
	}
	uloop_timeout_set(timeout, agent_heartbeat_interval_sec * 1000);
}

static int run_agent_ubus_daemon(const char *db_path, int heartbeat_interval_sec)
{
	agent_ubus_db_path = db_path;
	agent_heartbeat_interval_sec = heartbeat_interval_sec;
	agent_heartbeat_timeout.cb = agent_heartbeat_cb;

	uloop_init();
	agent_ubus_ctx = ubus_connect(NULL);
	if (!agent_ubus_ctx) {
		uloop_done();
		return -1;
	}
	ubus_add_uloop(agent_ubus_ctx);
	if (ubus_add_object(agent_ubus_ctx, &agent_ubus_object) != 0) {
		ubus_free(agent_ubus_ctx);
		uloop_done();
		return -1;
	}
	uloop_timeout_set(&agent_heartbeat_timeout, heartbeat_interval_sec * 1000);
	uloop_run();
	ubus_free(agent_ubus_ctx);
	uloop_done();
	return 0;
}
#endif

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

#ifdef EDGEPULSE_ENABLE_UBUS
	if (run_agent_ubus_daemon(db_path, heartbeat_interval_sec) == 0)
		goto stopped;
	fprintf(stderr, "edgepulse: ubus agent object unavailable, falling back to heartbeat-only mode\n");
#endif

	while (keep_running) {
		if (store_agent_audit(db_path, "agentd.heartbeat",
				      "EdgePulse AI agent runtime monitor heartbeat") != 0) {
			fprintf(stderr, "edgepulse: failed to write agent heartbeat: %s\n",
				strerror(errno));
		}

		for (int i = 0; keep_running && i < heartbeat_interval_sec; i++)
			sleep(1);
	}

#ifdef EDGEPULSE_ENABLE_UBUS
stopped:
#endif
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
