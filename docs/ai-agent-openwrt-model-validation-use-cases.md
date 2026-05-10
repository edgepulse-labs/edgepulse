# AI Agent OpenWrt Model Validation Use Cases

This document defines repeatable AI Agent validation use cases for OpenWrt targets. The goal is to compare model compatibility across providers without changing the router-side runtime every time.

## Scope

These use cases are model benchmarks for the EdgePulse AI Agent MVP. They validate that a model can:

- Return useful assistant content for OpenWrt diagnostics.
- Avoid leaking secrets in command output, JSON, or syslog.
- Behave predictably when the endpoint is unreachable or returns reasoning-only content.
- Work with the read-only tool policy and LuCI helper path.

Use `.env` or exported shell variables to provide the target device, package paths, and model settings. See [Test environment variables](test-environment-variables.md).

## Required Observability

Each test should capture:

- `edgepulse-ctl agent status`
- `edgepulse-ctl agent ask "<prompt>"`
- `edgepulse-ctl agent audit list`
- `logread -e edgepulse-agent`
- For LuCI coverage: `/usr/libexec/edgepulse-luci agent-status` and `/usr/libexec/edgepulse-luci agent-diagnose`

Model response checks should record:

- `model_response.status`
- `model_response.http_status`
- `model_response.finish_reason`
- `model_response.reasoning_present`
- `answer`
- Whether the answer came from model content or local fallback behavior.

## Use Case 1: Local-Only Baseline

Purpose: confirm the agent can run without a remote model.

Setup:

- `agent.enabled=1`
- `agent.local_only=1`
- Any configured model may remain present.

Prompt:

```text
Summarize CPU, memory, uptime, and network health from the provided telemetry in one concise sentence.
```

Expected result:

- Agent returns local telemetry and tool evidence.
- No remote model request is made.
- Output clearly indicates local-only model behavior.
- Syslog includes request/tool/policy summaries without secrets.

## Use Case 2: Remote Model Diagnostic Summary

Purpose: verify the model can turn compact OpenWrt telemetry into usable assistant content.

Setup:

- `agent.enabled=1`
- `agent.local_only=0`
- Model endpoint, model name, timeout, token budget, and API key are configured.

Prompt:

```text
Summarize CPU, memory, uptime, and network health from the provided telemetry in one concise sentence.
```

Expected result:

- `model_response.status=ok`
- `finish_reason=stop`
- `answer` contains model-provided content.
- `reasoning_present` may be true or false; this is acceptable when assistant content is present.
- Syslog records model settings such as `no_think` and `max_tokens`.

Current OpenWrt One reference settings:

- `no_think=0`
- `max_tokens=2048`
- `timeout_sec=60`
- `retry_count=0`

## Use Case 3: No-Think Compatibility Matrix

Purpose: determine whether a model endpoint supports no-think behavior.

Run the same diagnostic summary with:

- `no_think=0`
- `no_think=1`

Optional provider-specific variants may include request fields such as `enable_thinking=false` when a future adapter supports them.

Expected result:

- A compatible model should still return assistant content when no-think is requested.
- If `no_think=1` produces `finish_reason=length`, `reasoning_present=true`, and empty assistant content, mark the model as not compatible with the current no-think request style.
- Do not treat `reasoning_present=true` alone as failure; it only fails when assistant content is missing or unusable.

OpenWrt One finding:

- The tested Qwen OpenAI-compatible endpoint did not reliably honor `/no_think`.
- `no_think=1` repeatedly spent the response budget on `reasoning_content`.
- The validated setting for that endpoint is `no_think=0`.

## Use Case 4: Token Budget and Timeout Boundary

Purpose: find the smallest reliable token budget and timeout for each model.

Run the diagnostic summary with a small matrix:

- `max_tokens=512`
- `max_tokens=1024`
- `max_tokens=2048`
- Provider-specific higher values only if latency remains acceptable.

Expected result:

- A passing configuration returns `finish_reason=stop` and assistant content.
- A reasoning model may need a larger token budget even for short answers.
- Timeout should be long enough for a single model request but not so long that the LuCI UI feels stuck.

OpenWrt One finding:

- `1024` tokens often produced reasoning-only responses for the diagnostic summary.
- `2048` tokens with a `60` second timeout produced usable assistant content.

## Use Case 5: Endpoint Failure and Local Fallback

Purpose: verify router behavior when the model endpoint is unavailable.

Setup:

- Point `base_url` at an unreachable local or reserved address.
- Keep `agent.local_only=0`.

Expected result:

- `model_response.status` is non-OK, such as `fetch_error`.
- Agent still returns local telemetry and tool evidence.
- The answer clearly states that model inference failed and local fallback data is included.
- No API key is printed in JSON or syslog.

## Use Case 6: Secret Redaction

Purpose: ensure credentials do not leak through observability paths.

Setup:

- Configure a distinctive fake API key for a mock server test, or use the real key only on trusted hardware.

Expected result:

- `edgepulse-ctl agent ask` prints `"api_key": "redacted"`.
- `logread -e edgepulse-agent` never contains the raw key.
- Audit records contain event names and statuses, not secrets.

## Use Case 7: Read-Only Policy Guardrail

Purpose: ensure the MVP stays inside the read-only action set.

Prompt:

```text
Check system health and do not change any settings.
```

Expected result:

- Only read-only tools are run.
- Tool summaries appear for snapshot, shell read commands, and allowed ubus calls.
- No destructive command is exposed by the agent path.

## Use Case 8: LuCI Helper Parity

Purpose: verify the web UI backend uses the same agent path as the CLI.

Commands:

```sh
/usr/libexec/edgepulse-luci agent-status
/usr/libexec/edgepulse-luci agent-diagnose
```

Expected result:

- LuCI status contains the same model configuration fields as `edgepulse-ctl agent status`.
- LuCI diagnostic output has the same model/fallback behavior as `edgepulse-ctl agent ask`.
- Settings page can modify model timeout, retry count, max tokens, and no-think mode.

## Use Case 9: Model Inventory and Priority Failover

Purpose: verify users can discover provider-supported model IDs and that inference can move to the next configured model when the preferred model is unavailable.

Commands:

```sh
edgepulse-ctl agent models list
edgepulse-ctl agent models remote-list remote_reasoner
```

Setup:

- Configure at least two enabled `config model` sections.
- Give the first section a lower numeric `priority` value and point it at an unreachable local endpoint.
- Keep a known-good model section at the next priority.

Expected result:

- `models list` shows all configured model sections sorted by priority.
- `remote-list` returns model IDs from the provider `/models` endpoint and redacts the API key.
- `agent ask` first records the failing high-priority model, then selects the next usable model.
- The diagnostic JSON includes `model_failover.attempts` and `model_failover.selected_provider`.
- Syslog records each model attempt with provider, model name, priority, status, finish reason, and reasoning flag.

## Benchmark Record Template

Record one row per model/configuration:

| Date | Target | Provider | Model | no_think | max_tokens | timeout_sec | finish_reason | reasoning_present | Result | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 2026-05-10 | OpenWrt One | OpenAI-compatible | qwen/qwen3.6-35b-a3b | 0 | 2048 | 60 | stop | true | pass | Usable assistant content. |

## Pass Criteria

A model configuration is considered usable for the MVP when:

- Use Case 2 passes at least twice in a row.
- Use Case 5 returns a clear local fallback.
- Use Case 6 shows no secret leakage.
- Use Case 8 confirms LuCI can access the same status and diagnostic path.

The no-think mode is considered supported only when Use Case 3 passes with assistant content present.
