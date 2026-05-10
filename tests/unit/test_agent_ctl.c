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
	char *const ubus_denied[] = { "ubus", "call", "service", "restart", NULL };
	char *const rm_denied[] = { "rm", "-rf", "/", NULL };
	char *const uname_denied[] = { "uname", "-r", NULL };

	check_int("allow uname -a", agent_command_allowed(uname_ok), 1);
	check_int("allow uptime", agent_command_allowed(uptime_ok), 1);
	check_int("allow ubus system board", agent_command_allowed(ubus_ok), 1);
	check_int("deny ubus service restart", agent_command_allowed(ubus_denied), 0);
	check_int("deny rm", agent_command_allowed(rm_denied), 0);
	check_int("deny uname -r", agent_command_allowed(uname_denied), 0);
	check_int("deny null argv", agent_command_allowed(NULL), 0);
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
	      " option retry_count '2'\n",
	      fp);
	fclose(fp);

	setenv("EDGEPULSE_CONFIG_PATH", path, 1);
	check_int("read agent config", read_agent_config(&agent, &model), 0);
	unsetenv("EDGEPULSE_CONFIG_PATH");
	unlink(path);

	check_int("agent enabled parse", agent.enabled, 1);
	check_int("agent local_only parse", agent.local_only, 0);
	check_int("agent memory parse", agent.memory_enabled, 0);
	check_int("agent ubus parse", agent.ubus_enabled, 0);
	check_int("agent request timeout parse", agent.request_timeout_sec, 42);
	check_int("agent heartbeat parse", agent.heartbeat_interval_sec, 7);
	check_int("agent tool timeout parse", agent.tool_timeout_sec, 3);
	check_int("agent output limit parse", agent.max_tool_output_bytes, 2048);
	check_string("agent db path parse", agent.db_path, "/tmp/custom-agent.db");
	check_int("model present parse", model.present, 1);
	check_int("model enabled parse", model.enabled, 1);
	check_int("model configured parse", model.configured, 1);
	check_string("model name parse", model.name, "remote_reasoner");
	check_string("model value parse", model.model, "edge-test-model");
	check_string("model api key parse", model.api_key, "super-secret");
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

	agent_build_model_request(&agent, &model, "analyzer", &request);
	check_string("model request status", request.status, "ready");
	check_string("model request endpoint", request.endpoint,
		     "http://127.0.0.1:8080/v1/chat/completions");

	agent_build_model_request(&agent, &model, "classifier", &request);
	check_string("role mismatch status", request.status, "role_not_matched");

	build_model_payload(payload, sizeof(payload), model.model,
			    "quote \" newline\n tab\t slash\\");
	check_contains("payload model", payload, "\"model\":\"edge-test-model\"");
	check_contains("payload escaped quote", payload, "quote \\\"");
	check_contains("payload slash", payload, "slash\\\\");
	if (strstr(payload, "\n") || strstr(payload, "\t")) {
		fprintf(stderr, "FAIL payload controls: contains raw newline or tab\n");
		failures++;
	}

	snprintf(model.base_url, sizeof(model.base_url), "%s", "https://api.example.test/v1");
	agent_build_model_request(&agent, &model, "analyzer", &request);
	agent_call_model_with_retries(&request, &model, "hello", &response);
	check_int("unsupported transport attempts", response.attempts, 1);
	check_string("unsupported transport status", response.status, "unsupported_transport");

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

	snprintf(model.base_url, sizeof(model.base_url), "%s", "http://example.test/v1");
	check_int("remote http warns", agent_config_has_warnings(&agent, &model), 1);
}

int main(void)
{
	test_agent_policy_allowlist();
	test_agent_tool_execution();
	test_agent_uci_parsing();
	test_agent_model_request_and_payload();
	test_agent_validation_warnings();

	if (failures != 0) {
		fprintf(stderr, "%d agent unit test(s) failed\n", failures);
		return 1;
	}

	puts("edgepulse agent unit tests passed");
	return 0;
}
