#define _GNU_SOURCE
#ifndef EDGEPULSE_ENABLE_AI_AGENT
#define EDGEPULSE_ENABLE_AI_AGENT
#endif

#define main edgepulse_ctl_main
#include "../../src/edgepulse-ctl/main.c"
#undef main

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

static void check_int(const char *name, int actual, int expected)
{
	if (actual != expected) {
		fprintf(stderr, "FAIL %s: expected %d, got %d\n", name, expected, actual);
		failures++;
	}
}

static void check_string(const char *name, const char *actual, const char *expected)
{
	if (strcmp(actual ? actual : "", expected ? expected : "") != 0) {
		fprintf(stderr, "FAIL %s: expected '%s', got '%s'\n",
			name, expected ? expected : "", actual ? actual : "");
		failures++;
	}
}

static void check_contains(const char *name, const char *haystack, const char *needle)
{
	if (!haystack || !strstr(haystack, needle)) {
		fprintf(stderr, "FAIL %s: expected to find '%s' in '%s'\n",
			name, needle, haystack ? haystack : "");
		failures++;
	}
}

static void test_agent_policy_allowlist(void)
{
	char *const uname_ok[] = { "uname", "-a", NULL };
	char *const uptime_ok[] = { "uptime", NULL };
	char *const ubus_ok[] = { "ubus", "call", "system", "board", NULL };
	char *const wireless_ok[] = { "ubus", "call", "network.wireless", "status", NULL };
	char *const service_ok[] = { "ubus", "call", "service", "list", NULL };
	char *const logread_ok[] = { "logread", "-l", "80", NULL };
	char *const logread_denied[] = { "logread", "-f", NULL };
	char *const ping_ip_ok[] = { "ping", "-c", "1", "-W", "2", "1.1.1.1", NULL };
	char *const ping_denied[] = { "ping", "-c", "3", "1.1.1.1", NULL };
	char *const uci_show_ok[] = { "uci", "show", "edgepulse", NULL };
	char *const uci_show_denied[] = { "uci", "show", "wireless", NULL };
	char *const ifup_wan_ok[] = { "ifup", "wan", NULL };
	char *const ifup_lan_denied[] = { "ifup", "lan", NULL };
	char *const wifi_reload_ok[] = { "wifi", "reload", NULL };
	char *const uci_ssid_ok[] = { "uci", "set", "wireless.@wifi-iface[0].ssid=EdgePulse", NULL };
	char *const uci_network_denied[] = { "uci", "set", "network.wan.proto=dhcp", NULL };
	char *const ubus_denied[] = { "ubus", "call", "service", "restart", NULL };
	char *const rm_denied[] = { "rm", "-rf", "/", NULL };
	char *const uname_denied[] = { "uname", "-r", NULL };

	check_int("allow uname -a", agent_command_allowed(uname_ok), 1);
	check_int("allow uptime", agent_command_allowed(uptime_ok), 1);
	check_int("allow ubus system board", agent_command_allowed(ubus_ok), 1);
	check_int("allow ubus wireless status", agent_command_allowed(wireless_ok), 1);
	check_int("allow ubus service list", agent_command_allowed(service_ok), 1);
	check_int("allow bounded logread", agent_command_allowed(logread_ok), 1);
	check_int("deny streaming logread", agent_command_allowed(logread_denied), 0);
	check_int("allow bounded ping", agent_command_allowed(ping_ip_ok), 1);
	check_int("deny arbitrary ping", agent_command_allowed(ping_denied), 0);
	check_int("allow edgepulse uci read", agent_command_allowed(uci_show_ok), 1);
	check_int("deny arbitrary uci read", agent_command_allowed(uci_show_denied), 0);
	check_int("deny ubus service restart", agent_command_allowed(ubus_denied), 0);
	check_int("deny rm", agent_command_allowed(rm_denied), 0);
	check_int("deny uname -r", agent_command_allowed(uname_denied), 0);
	check_int("deny null argv", agent_command_allowed(NULL), 0);
	check_int("allow confirmed ifup wan", agent_mutating_command_allowed(ifup_wan_ok), 1);
	check_int("deny confirmed ifup lan", agent_mutating_command_allowed(ifup_lan_denied), 0);
	check_int("allow wifi reload", agent_mutating_command_allowed(wifi_reload_ok), 1);
	check_int("allow wifi ssid uci", agent_mutating_command_allowed(uci_ssid_ok), 1);
	check_int("deny network uci", agent_mutating_command_allowed(uci_network_denied), 0);
}

static void test_agent_tool_execution(void)
{
	struct agent_tool_result result;
	char *const uptime_ok[] = { "uptime", NULL };
	char *const blocked[] = { "rm", "-rf", "/", NULL };
	char *const ubus_ok[] = { "ubus", "call", "system", "board", NULL };
	const char *bin_dir = "/tmp/edgepulse-agent-test-bin";
	const char *ubus_path = "/tmp/edgepulse-agent-test-bin/ubus";
	const char *old_path = getenv("PATH");
	char path_value[512];
	FILE *fp;

	check_int("run uptime", agent_run_read_only_command("shell.uptime", uptime_ok,
							     5, 128, &result), 0);
	check_string("uptime status", result.status, "ok");
	if (result.output[0] == '\0') {
		fprintf(stderr, "FAIL uptime output: empty\n");
		failures++;
	}

	check_int("run blocked command", agent_run_read_only_command("shell.rm", blocked,
								     5, 128, &result), -1);
	check_string("blocked status", result.status, "blocked");
	check_contains("blocked output", result.output, "allowlist");

	mkdir(bin_dir, 0700);
	fp = fopen(ubus_path, "w");
	if (!fp) {
		fprintf(stderr, "FAIL fake ubus fixture: open\n");
		failures++;
		return;
	}
	fputs("#!/bin/sh\nprintf '{\"board_name\":\"fixture-board\"}\\n'\n", fp);
	fclose(fp);
	chmod(ubus_path, 0700);
	snprintf(path_value, sizeof(path_value), "%s:%s", bin_dir, old_path ? old_path : "");
	setenv("PATH", path_value, 1);

	check_int("run fake ubus", agent_run_read_only_command("ubus.system.board", ubus_ok,
							       5, 128, &result), 0);
	check_string("fake ubus status", result.status, "ok");
	check_contains("fake ubus output", result.output, "fixture-board");

	if (old_path)
		setenv("PATH", old_path, 1);
	else
		unsetenv("PATH");
	unlink(ubus_path);
	rmdir(bin_dir);
}

static void test_agent_uci_parsing(void)
{
	const char *path = "/tmp/edgepulse-agent-uci-test.conf";
	FILE *fp = fopen(path, "w");
	struct agent_config agent;
	struct agent_model_config model;

	if (!fp) {
		fprintf(stderr, "FAIL open temp config\n");
		failures++;
		return;
	}

	fputs("config edgepulse 'main'\n"
	      " option db_path '/tmp/custom-agent.db'\n"
	      "\n"
	      "config agent 'agent'\n"
	      " option enabled '1'\n"
	      " option local_only '0'\n"
	      " option memory_enabled '0'\n"
	      " option shell_enabled '1'\n"
	      " option ubus_enabled '0'\n"
	      " option policy_profile 'read_only'\n"
	      " option chat_enabled '1'\n"
	      " option default_conversation_id 'ops'\n"
	      " option mcp_enabled '1'\n"
	      " option allow_reconnect_wan '0'\n"
	      " option allow_wifi_restart '0'\n"
	      " option allow_wifi_set '0'\n"
	      " option mcp_allow_edgepulse_status '0'\n"
	      " option mcp_allow_chat_ask '0'\n"
	      " option mcp_allow_uci_get_edgepulse '0'\n"
	      " option request_timeout_sec '42'\n"
	      " option heartbeat_interval_sec '7'\n"
	      " option tool_timeout_sec '3'\n"
	      " option max_tool_output_bytes '2048'\n"
	      "\n"
	      "config model 'remote_reasoner'\n"
	      " option enabled '1'\n"
	      " option role 'planner,analyzer'\n"
	      " option base_url 'http://127.0.0.1:8080/v1'\n"
	      " option model 'edge-test-model'\n"
	      " option api_key 'super-secret'\n"
	      " option api_key_env 'EDGEPULSE_TEST_KEY'\n"
	      " option timeout_sec '9'\n"
	      " option retry_count '2'\n"
	      " option max_tokens '1536'\n"
	      " option no_think '0'\n"
	      " option priority '20'\n"
	      "\n"
	      "config model 'backup_reasoner'\n"
	      " option enabled '1'\n"
	      " option role 'analyzer'\n"
	      " option base_url 'http://127.0.0.1:8081/v1'\n"
	      " option model 'backup-model'\n"
	      " option api_key 'backup-secret'\n"
	      " option priority '10'\n",
	      fp);
	fclose(fp);

	setenv("EDGEPULSE_CONFIG_PATH", path, 1);
	check_int("read agent config", read_agent_config(&agent, &model), 0);
	{
		struct agent_model_config models[AGENT_MAX_MODELS];
		int model_count = 0;

		check_int("read agent model configs",
			  read_agent_config_models(&agent, models, AGENT_MAX_MODELS,
						   &model_count),
			  0);
		check_int("model count parse", model_count, 2);
		check_string("model priority sort", models[0].name, "backup_reasoner");
		check_int("model priority value", models[0].priority, 10);
		check_string("models status", agent_models_status(&agent, models, model_count),
			     "configured");
	}
	unsetenv("EDGEPULSE_CONFIG_PATH");
	unlink(path);

	check_int("agent enabled parse", agent.enabled, 1);
	check_int("agent local_only parse", agent.local_only, 0);
	check_int("agent memory parse", agent.memory_enabled, 0);
	check_int("agent ubus parse", agent.ubus_enabled, 0);
	check_int("agent chat parse", agent.chat_enabled, 1);
	check_int("agent mcp parse", agent.mcp_enabled, 1);
	check_int("agent reconnect permission parse", agent.allow_reconnect_wan, 0);
	check_int("agent wifi restart permission parse", agent.allow_wifi_restart, 0);
	check_int("agent wifi permission parse", agent.allow_wifi_set, 0);
	check_int("mcp status acl parse", agent.mcp_allow_edgepulse_status, 0);
	check_int("mcp chat ask acl parse", agent.mcp_allow_chat_ask, 0);
	check_int("mcp uci acl parse", agent.mcp_allow_uci_get_edgepulse, 0);
	check_int("mcp agent status acl default", agent.mcp_allow_agent_status, 1);
	check_string("agent conversation parse", agent.default_conversation_id, "ops");
	check_int("agent request timeout parse", agent.request_timeout_sec, 42);
	check_int("agent heartbeat parse", agent.heartbeat_interval_sec, 7);
	check_int("agent tool timeout parse", agent.tool_timeout_sec, 3);
	check_int("agent output limit parse", agent.max_tool_output_bytes, 2048);
	check_string("agent db path parse", agent.db_path, "/tmp/custom-agent.db");
	check_int("model present parse", model.present, 1);
	check_int("model enabled parse", model.enabled, 1);
	check_int("model configured parse", model.configured, 1);
	check_string("model name parse", model.name, "backup_reasoner");
	check_string("model value parse", model.model, "backup-model");
	check_string("model api key parse", model.api_key, "backup-secret");
	check_int("model priority parse", model.priority, 10);
	check_string("model status", agent_model_status(&agent, &model), "configured");
}

static void test_agent_model_request_and_payload(void)
{
	struct agent_config agent;
	struct agent_model_config model;
	struct agent_model_request request;
	struct agent_model_response response;
	char payload[1024];
	char content[256];

	init_agent_config(&agent, &model);
	agent.enabled = 1;
	agent.local_only = 0;
	model.present = 1;
	model.enabled = 1;
	model.configured = 1;
	snprintf(model.name, sizeof(model.name), "%s", "remote_reasoner");
	snprintf(model.role, sizeof(model.role), "%s", "planner,analyzer");
	snprintf(model.base_url, sizeof(model.base_url), "%s", "http://127.0.0.1:8080/v1");
	snprintf(model.model, sizeof(model.model), "%s", "edge-test-model");
	snprintf(model.api_key, sizeof(model.api_key), "%s", "super-secret");
	model.retry_count = 1;
	model.max_tokens = 1024;
	model.no_think = 1;

	agent_build_model_request(&agent, &model, "analyzer", &request);
	check_string("model request status", request.status, "ready");
	check_string("model request endpoint", request.endpoint,
		     "http://127.0.0.1:8080/v1/chat/completions");

	agent_build_model_request(&agent, &model, "classifier", &request);
	check_string("role mismatch status", request.status, "role_not_matched");

	build_model_payload(payload, sizeof(payload), &model,
			    "quote \" newline\n tab\t slash\\");
	check_contains("payload model", payload, "\"model\":\"edge-test-model\"");
	check_contains("payload max tokens", payload, "\"max_tokens\":1024");
	check_contains("payload no think", payload, "/no_think");
	check_contains("payload escaped quote", payload, "quote \\\"");
	check_contains("payload slash", payload, "slash\\\\");
	if (strstr(payload, "\n") || strstr(payload, "\t")) {
		fprintf(stderr, "FAIL payload controls: contains raw newline or tab\n");
		failures++;
	}

	snprintf(model.base_url, sizeof(model.base_url), "%s", "https://api.example.test/v1");
	model.retry_count = 0;
	agent_build_model_request(&agent, &model, "analyzer", &request);
	agent_call_model_with_retries(&request, &model, "hello", &response);
	check_int("remote transport attempts", response.attempts, 1);
	if (strcmp(response.status, "fetch_error") != 0 &&
	    strcmp(response.status, "transport_unavailable") != 0) {
		fprintf(stderr, "FAIL remote transport status: got '%s'\n",
			response.status);
		failures++;
	}

	check_int("extract model content",
		  extract_openai_message_content("{\"choices\":[{\"message\":{\"content\":\"local model response\"}}]}",
						 content, sizeof(content)),
		  0);
	check_string("model content", content, "local model response");
	check_int("extract escaped model content",
		  extract_openai_message_content("{\"content\":\"line one\\nline two \\\"quoted\\\"\"}",
						 content, sizeof(content)),
		  0);
	check_string("escaped model content", content, "line one\nline two \"quoted\"");
	check_int("extract finish reason",
		  extract_openai_finish_reason("{\"finish_reason\":\"length\"}",
					       content, sizeof(content)),
		  0);
	check_string("finish reason", content, "length");
	check_int("detect reasoning content",
		  openai_has_reasoning_content("{\"reasoning_content\":\"thinking\"}"),
		  1);
	check_int("ignore empty reasoning content",
		  openai_has_reasoning_content("{\"reasoning_content\":\"\"}"),
		  0);
}

static void test_agent_validation_warnings(void)
{
	struct agent_config agent;
	struct agent_model_config model;

	init_agent_config(&agent, &model);
	check_int("default config has no warnings", agent_config_has_warnings(&agent, &model), 0);

	snprintf(agent.policy_profile, sizeof(agent.policy_profile), "%s", "unsafe");
	check_int("unsafe policy warns", agent_config_has_warnings(&agent, &model), 1);
	snprintf(agent.policy_profile, sizeof(agent.policy_profile), "%s", "read_only");
	check_int("read-only policy does not mutate", agent_policy_allows_mutation(&agent), 0);
	snprintf(agent.policy_profile, sizeof(agent.policy_profile), "%s", "operator_confirmed");
	check_int("operator policy valid", agent_config_has_warnings(&agent, &model), 0);
	check_int("operator policy can mutate", agent_policy_allows_mutation(&agent), 1);
	snprintf(agent.policy_profile, sizeof(agent.policy_profile), "%s", "read_only");
	check_int("default mcp method allowed",
		  agent_mcp_method_allowed(&agent, "edgepulse.agent.status"), 1);
	agent.mcp_allow_agent_status = 0;
	check_int("disabled mcp method blocked",
		  agent_mcp_method_allowed(&agent, "edgepulse.agent.status"), 0);
	check_int("unknown mcp method blocked",
		  agent_mcp_method_allowed(&agent, "edgepulse.unknown"), 0);

	snprintf(model.base_url, sizeof(model.base_url), "%s", "http://example.test/v1");
	check_int("remote http warns", agent_config_has_warnings(&agent, &model), 1);
}

static void test_agent_skill_registry(void)
{
	struct agent_config agent;
	struct agent_model_config model;
	struct agent_skill_registry registry;
	const struct agent_skill *status_skill;
	const struct agent_skill *wan_skill;
	const struct agent_skill *wifi_restart_skill;
	const struct agent_skill *manifest_skill;
	const char *dir = "/tmp/edgepulse-agent-skills-test";
	const char *manifest_path = "/tmp/edgepulse-agent-skills-test/custom.json";
	const char *blocked_path = "/tmp/edgepulse-agent-skills-test/blocked.json";
	FILE *fp;

	init_agent_config(&agent, &model);
	status_skill = agent_find_skill("openwrt.status.summary");
	wan_skill = agent_find_skill("openwrt.wan.reconnect");
	wifi_restart_skill = agent_find_skill("openwrt.wifi.restart");

	check_int("skill count", (int)agent_skill_count(), 8);
	check_string("find status skill action",
		     status_skill ? status_skill->action : "", "status");
	check_int("status skill read only", status_skill ? status_skill->read_only : 0, 1);
	check_int("status skill allowed in read only",
		  agent_skill_allowed(&agent, status_skill), 1);
	check_string("find wan skill action",
		     wan_skill ? wan_skill->action : "", "reconnect-wan");
	check_int("wan skill requires confirm",
		  wan_skill ? wan_skill->requires_confirm : 0, 1);
	check_int("wan skill blocked in read only",
		  agent_skill_allowed(&agent, wan_skill), 0);
	snprintf(agent.policy_profile, sizeof(agent.policy_profile), "%s",
		 "operator_confirmed");
	check_int("wan skill allowed in operator policy",
		  agent_skill_allowed(&agent, wan_skill), 1);
	agent.allow_reconnect_wan = 0;
	check_int("wan skill per-action disabled",
		  agent_skill_allowed(&agent, wan_skill), 0);
	check_string("find wifi restart skill action",
		     wifi_restart_skill ? wifi_restart_skill->action : "",
		     "wifi-restart");
	agent.allow_reconnect_wan = 1;
	agent.allow_wifi_restart = 1;
	check_int("wifi restart skill allowed in operator policy",
		  agent_skill_allowed(&agent, wifi_restart_skill), 1);
	agent.allow_wifi_restart = 0;
	check_int("wifi restart skill per-action disabled",
		  agent_skill_allowed(&agent, wifi_restart_skill), 0);
	check_int("unknown skill not found",
		  agent_find_skill("openwrt.unknown") == NULL, 1);

	mkdir(dir, 0700);
	fp = fopen(manifest_path, "w");
	if (!fp) {
		fprintf(stderr, "FAIL open skill manifest\n");
		failures++;
		return;
	}
	fputs("{"
	      "\"id\":\"custom.status\","
	      "\"title\":\"Custom Status\","
	      "\"description\":\"Custom read-only status wrapper\","
	      "\"action\":\"status\","
	      "\"required_policy\":\"read_only\","
	      "\"requires_confirm\":false,"
	      "\"read_only\":true,"
	      "\"steps\":[\"shell.uptime\"]"
	      "}\n",
	      fp);
	fclose(fp);
	fp = fopen(blocked_path, "w");
	if (!fp) {
		fprintf(stderr, "FAIL open blocked skill manifest\n");
		failures++;
		return;
	}
	fputs("{"
	      "\"id\":\"custom.blocked\","
	      "\"title\":\"Blocked\","
	      "\"description\":\"Unsupported action should not load\","
	      "\"action\":\"shell\","
	      "\"required_policy\":\"read_only\","
	      "\"steps\":[\"shell.anything\"]"
	      "}\n",
	      fp);
	fclose(fp);
	setenv("EDGEPULSE_SKILLS_DIR", dir, 1);
	agent_load_skill_registry(&registry);
	unsetenv("EDGEPULSE_SKILLS_DIR");
	unlink(manifest_path);
	unlink(blocked_path);
	rmdir(dir);

	check_int("manifest registry count", (int)registry.manifest_count, 1);
	manifest_skill = agent_find_manifest_skill(&registry, "custom.status");
	check_string("manifest skill action",
		     manifest_skill ? manifest_skill->action : "", "status");
	check_string("manifest skill source",
		     manifest_skill ? manifest_skill->source : "", "custom.json");
	check_int("blocked manifest not loaded",
		  agent_find_manifest_skill(&registry, "custom.blocked") == NULL, 1);
}

static void test_agent_intent_and_redaction(void)
{
	char redacted[256];

	check_string("classify zh wifi status",
		     agent_classify_intent("Wi-Fi 有開嗎？"), "wifi-status");
	check_string("classify logs",
		     agent_classify_intent("show recent abnormal logs"), "logs-recent");
	check_string("classify reconnect",
		     agent_classify_intent("幫我重新撥接網路"), "reconnect-wan");
	check_string("classify status",
		     agent_classify_intent("router health status"), "status");
	check_string("classify unknown",
		     agent_classify_intent("tell me a short joke"), NULL);

	redact_wifi_keys("wireless.@wifi-iface[0].key=secret-pass\nok",
			 redacted, sizeof(redacted));
	check_contains("redact uci key", redacted, "key=redacted");
	if (strstr(redacted, "secret-pass")) {
		fprintf(stderr, "FAIL redact uci key secret remained: %s\n", redacted);
		failures++;
	}

	redact_wifi_keys("{\"key\":\"secret-pass\",\"ssid\":\"EdgePulse\"}",
			 redacted, sizeof(redacted));
	check_contains("redact json key", redacted, "key=redacted");
	if (strstr(redacted, "secret-pass")) {
		fprintf(stderr, "FAIL redact json key secret remained: %s\n", redacted);
		failures++;
	}
}

static void test_agent_conversation_storage(void)
{
	const char *path = "/tmp/edgepulse-agent-chat-test.db";
	sqlite3 *db = NULL;
	sqlite3_stmt *stmt = NULL;
	int count = 0;

	unlink(path);
	check_int("store conversation messages",
		  agent_store_conversation_messages(path, "luci", "req-1",
						    "hello", "local_only",
						    "answer"),
		  0);
	if (sqlite3_open(path, &db) != SQLITE_OK) {
		fprintf(stderr, "FAIL open chat db\n");
		failures++;
		return;
	}
	if (sqlite3_prepare_v2(db,
			       "SELECT count(*) FROM agent_messages WHERE conversation_id='luci';",
			       -1, &stmt, NULL) != SQLITE_OK) {
		fprintf(stderr, "FAIL prepare chat count\n");
		failures++;
		sqlite3_close(db);
		return;
	}
	if (sqlite3_step(stmt) == SQLITE_ROW)
		count = sqlite3_column_int(stmt, 0);
	sqlite3_finalize(stmt);
	sqlite3_close(db);
	unlink(path);
	check_int("conversation message pair", count, 2);
}

static void test_agent_mcp_json_helpers(void)
{
	char value[128];
	int boolean = 0;

	check_int("extract json string",
		  json_extract_string_field("{\"id\":\"1\",\"method\":\"tools/list\"}",
					    "method", value, sizeof(value)),
		  0);
	check_string("json method", value, "tools/list");
	check_int("extract escaped json string",
		  json_extract_string_field("{\"message\":\"line one\\nline two\"}",
					    "message", value, sizeof(value)),
		  0);
	check_string("json escaped", value, "line one\nline two");
	check_int("missing json string",
		  json_extract_string_field("{\"id\":\"1\"}", "method", value,
					    sizeof(value)),
		  -1);
	check_int("extract json bool",
		  json_extract_bool_field("{\"confirm\":true}", "confirm",
					  &boolean),
		  0);
	check_int("json bool value", boolean, 1);
	check_int("extract raw numeric id",
		  json_extract_raw_field("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}",
					 "id", value, sizeof(value)),
		  0);
	check_string("json raw numeric id", value, "1");
	check_int("extract raw string id",
		  json_extract_raw_field("{\"id\":\"abc-1\"}", "id", value,
					 sizeof(value)),
		  0);
	check_string("json raw string id", value, "\"abc-1\"");
}

int main(void)
{
	test_agent_policy_allowlist();
	test_agent_tool_execution();
	test_agent_uci_parsing();
	test_agent_model_request_and_payload();
	test_agent_validation_warnings();
	test_agent_skill_registry();
	test_agent_intent_and_redaction();
	test_agent_conversation_storage();
	test_agent_mcp_json_helpers();

	if (failures != 0) {
		fprintf(stderr, "%d agent unit test(s) failed\n", failures);
		return 1;
	}

	puts("edgepulse agent unit tests passed");
	return 0;
}
