# Execution Plan

Review date: 2026-05-09

## Current Status

The project has completed the first MVP path on OpenWrt One:

- [x] C library shared by the daemon and CLI.
- [x] `edgepulse` daemon command with periodic JSON status output.
- [x] `edgepulse-ctl` minimum CLI with `status`, `latest`, `features`, `export`, and `version` commands.
- [x] Unit test program for the current shared telemetry helpers.
- [x] OpenWrt feed package skeleton for `edgepulse` and `luci-app-edgepulse`.
- [x] Local OpenWrt buildroot validation for `.apk` package output.
- [x] OpenWrt One install verification with `apk add --allow-untrusted`.
- [x] SQLite-backed raw sample storage under `/tmp/edgepulse/edgepulse.db`.
- [x] Stored feature windows with mean, min, max, standard deviation, delta, rate, and coefficient of variation.
- [x] CSV export for training rows backed by stored feature rows.
- [x] LuCI overview, metrics, features, and settings pages packaged and installed.

The next implementation focus is to add remote training-data upload, canonical feature normalization, longer-running reliability validation, collector toggle enforcement, and an optional AI agent runtime that can be enabled, configured, and operated from OpenWrt/LuCI.

## Goal

Build the smallest useful EdgePulse implementation for OpenWrt One:

- A C daemon package that samples local telemetry.
- A SQLite database stored under `/tmp`.
- Periodic feature extraction for AI training data.
- A LuCI application to show runtime metrics and configure sampling.

## Phase 0: Documentation Baseline

Status: complete

Todo:

- [x] Create `docs/README.md`.
- [x] Create `docs/readme-review.md`.
- [x] Create `docs/execution-plan.md`.
- [x] Create `docs/openwrt-one-telemetry-mvp.md`.
- [x] Create Traditional Chinese translations for project docs.
- [x] Keep updated project docs synchronized with their Traditional Chinese translation files.
- [x] Document local OpenWrt package validation.
- [x] Document the unit-test plan.

Exit criteria:

- The project has a clear MVP boundary.
- OpenWrt One assumptions are documented with source links.
- Metric, SQLite, and LuCI plans are explicit enough to become tickets.

## Phase 1: Package Skeleton

Status: complete for MVP

Create the OpenWrt feed repository and package structure:

```text
edgepulse-openwrt-feed/
  edgepulse/
    Makefile
    files/etc/config/edgepulse
    files/etc/init.d/edgepulse
  luci-app-edgepulse/
    Makefile
    root/usr/share/luci/menu.d/luci-app-edgepulse.json
    root/usr/share/rpcd/acl.d/luci-app-edgepulse.json
```

Todo:

- [x] Add `edgepulse-openwrt-feed/edgepulse/Makefile`.
- [x] Add `edgepulse-openwrt-feed/edgepulse/files/etc/config/edgepulse`.
- [x] Add `edgepulse-openwrt-feed/edgepulse/files/etc/init.d/edgepulse`.
- [x] Add `edgepulse-openwrt-feed/luci-app-edgepulse/Makefile`.
- [x] Add LuCI menu metadata.
- [x] Add rpcd ACL metadata.
- [x] Install both `edgepulse` and `edgepulse-ctl` into the OpenWrt package.
- [x] Build `edgepulse-1.apk` in the local OpenWrt buildroot.
- [x] Build `luci-app-edgepulse-1.apk` in the local OpenWrt buildroot.
- [x] Install and verify both packages on OpenWrt One.
- [x] Move OpenWrt package and LuCI implementation ownership to the standalone `edgepulse-openwrt-feed` repository for local OpenWrt builds.
- [x] Remove the obsolete in-repo OpenWrt feed package mirror and keep `packaging/openwrt-feed/README.md` as a pointer to the standalone feed repository.
- [x] Add release/version workflow for source archives and OpenWrt package `PKG_RELEASE` updates.

Initial dependencies:

- `libsqlite3`
- `libubus`
- `libubox`
- `libblobmsg-json`

Exit criteria:

- OpenWrt can consume the feed through `feeds.conf`.
- Package cross-compiles in an OpenWrt SDK for `mediatek/filogic`.
- Daemon starts through `/etc/init.d/edgepulse`.
- UCI config can enable or disable the daemon.

Reference:

- [OpenWrt feeds and repos](openwrt-feeds-and-repos.md)

## Phase 2: Minimal Raw Sampling

Status: complete for MVP

Implement low-risk file-based collectors first:

- [x] CPU: `/proc/stat`
- [x] Memory: `/proc/meminfo`
- [x] Load: `/proc/loadavg`
- [x] Network interfaces: `/proc/net/dev`
- [x] Thermal: `/sys/class/thermal/thermal_zone*/temp`
- [x] Uptime: `/proc/uptime`

Todo:

- [x] Add shared `edgepulse_collect_snapshot()` helper.
- [x] Emit a current JSON status snapshot.
- [x] Keep daemon output under `/tmp/edgepulse`.
- [x] Add SQLite schema initialization under `/tmp/edgepulse/edgepulse.db`.
- [x] Write raw samples into SQLite instead of only `edgepulse.json`.
- [x] Read daemon interval from UCI through the init script.
- [x] Record per-collector status so one failed collector does not fail the whole sample.
- [x] Add fixture-based tests for parsing `/proc` and `/sys` files.

Exit criteria:

- Samples are written to `/tmp/edgepulse/edgepulse.db`.
- Sampling interval is controlled by UCI.
- Collector failures are stored as status, not fatal daemon crashes.

## Phase 3: OpenWrt-Specific Collectors

Status: complete for MVP

Add OpenWrt integration:

- [x] `ubus` system board information.
- [x] `ubus` network interface status.
- [x] Wireless status through `/proc/net/wireless` where available.
- [x] Conntrack count from `/proc/sys/net/netfilter/nf_conntrack_count`.
- [x] nftables/counter support as optional later work.

Todo:

- [x] Add a small OpenWrt integration layer around `libubus`.
- [x] Store basic device metadata in a device metadata table.
- [x] Map physical interface counters to logical OpenWrt interfaces.
- [x] Treat missing conntrack sources as unavailable, not fatal.
- [x] Treat missing wireless sources as unavailable, not fatal.

Exit criteria:

- OpenWrt One board metadata is captured.
- WAN/LAN counters can be associated with logical interfaces.
- Wi-Fi interface metrics are recorded from `/proc/net/wireless` when available.

## Phase 4: Feature Windows

Status: complete for MVP

Compute periodic features from raw samples:

- [x] mean
- [x] min
- [x] max
- [x] standard deviation
- [x] delta
- [x] rate per second
- [x] coefficient of variation

Initial windows:

- [x] 60 seconds
- [x] 5 minutes
- [x] 15 minutes

Todo:

- [x] Define the feature table schema.
- [x] Add feature-window computation over SQLite raw samples.
- [x] Add `edgepulse-ctl features --json --window 60` implementation.
- [x] Add unit tests for feature calculations.

Exit criteria:

- Features are stored separately from raw samples.
- Feature rows include metric name, window size, start time, end time, and value.
- Export query can produce training rows.

## Phase 5: LuCI Application

Status: complete for MVP

Create a LuCI app:

```text
luci-app-edgepulse/
  htdocs/luci-static/resources/view/edgepulse/
    overview.js
    metrics.js
    features.js
    settings.js
  root/usr/share/luci/menu.d/luci-app-edgepulse.json
  root/usr/share/rpcd/acl.d/luci-app-edgepulse.json
```

Views:

- [x] Overview: initial health snapshot, load, memory, and uptime.
- [x] Overview: latest CPU, thermal, network, and collector status.
- [x] Metrics: latest raw metrics table.
- [x] Features: derived windows prepared for training data.
- [x] Settings: UCI-backed sampling interval, retention, enabled collectors, and database path.

Todo:

- [x] Add LuCI overview route.
- [x] Add rpcd ACL allowing LuCI to execute `edgepulse-ctl`.
- [x] Wire overview page to `edgepulse-ctl status --json`.
- [x] Add `metrics.js`.
- [x] Add `features.js`.
- [x] Add `settings.js`.
- [x] Replace direct command execution with a narrower RPC endpoint when the data model stabilizes.
- [x] Verify LuCI page rendering in a browser on OpenWrt One.

Exit criteria:

- LuCI can read `/tmp/edgepulse/edgepulse.db` through a small RPC endpoint or JSON export command.
- Settings are persisted through UCI.
- The UI works on desktop and mobile LuCI layouts.

## Phase 6: Training Data Export

Status: complete for MVP

Add a local export command:

```sh
edgepulse-ctl export --format csv --window 60s --since 1h
```

Todo:

- [x] Add placeholder `edgepulse-ctl export` command.
- [x] Implement CSV export from computed feature rows.
- [x] Add `--format`, `--window`, and `--since` argument parsing.
- [x] Include device metadata in exported rows.
- [x] Add stable CSV headers.
- [x] Add tests for missing metric representation.

Exit criteria:

- CSV export has stable column names.
- Export includes device metadata and feature timestamps.
- Missing metrics are represented consistently.

## Phase 7: Remote Training Data Upload and Normalization

Status: planned

Add an optional path for sending periodically collected training feature rows to a remote collection server.

Todo:

- [ ] Add upload UCI options: enabled flag, remote URL, token, interval, batch size, TLS verification, and device ID mode.
- [ ] Add LuCI settings controls for enabling/disabling upload and configuring the remote collector server.
- [ ] Add `edgepulse-ctl export --format json` or `jsonl` for machine-to-machine upload payloads.
- [ ] Add an `edgepulse-upload` helper or service that sends bounded batches and stores an acknowledged cursor.
- [ ] Add retry, backoff, and offline-safe spool behavior so upload failures never block local sampling.
- [ ] Document the remote server request and acknowledgement schema.
- [ ] Add thermal zone type collection so multi-zone devices can be normalized more reliably.
- [ ] Define a canonical feature schema using logical network roles, top-N variable slots, aggregate thermal features, and mask vectors.
- [ ] Keep device-side export sparse and label-preserving until the canonical schema stabilizes.

Exit criteria:

- Upload is disabled by default and can be enabled from LuCI.
- The remote collector URL and token can be configured from LuCI/UCI.
- Feature upload resumes safely after network or server failures.
- Training pipelines can map variable interface and thermal-zone rows into fixed schema vectors.

Reference:

- [Training data upload and normalization](training-data-upload-and-normalization.md)

## Phase 8: Optional AI Agent Runtime

Status: MVP implementation validated on OpenWrt One

Add an optional, policy-controlled AI agent to EdgePulse so an OpenWrt device can answer diagnostic questions using local telemetry, safe shell commands, read-only `ubus` queries, local memory, and one or more configured model API servers.

Initial boundary:

- The AI agent is disabled by default unless the package build or UCI config explicitly enables it.
- The first implementation should be diagnostic and read-only.
- Remote model use must be explicit and configurable.
- Tool execution must be policy-gated and auditable.
- LuCI should expose both an interaction page and settings page before the feature is considered user-ready.

OpenWrt package and build configuration todo:

- [x] Add an OpenWrt package build option in `edgepulse-openwrt-feed/edgepulse/Makefile` to include or exclude AI agent support at build time.
- [x] Define package config symbols for AI agent defaults, such as `EDGEPULSE_ENABLE_AI_AGENT`, default model provider, default remote base URL, default model name, default local-only mode, and default policy profile.
- [x] Avoid baking real secrets into firmware images by default; support a build-time API key placeholder only for development images and prefer runtime UCI or environment-based secret configuration.
- [x] Decide whether the first package shape is an optional `edgepulse-agent` subpackage or a feature compiled into the existing `edgepulse` package.
- [x] Add package dependencies needed by the first agent implementation, such as TLS/HTTP client support, JSON handling, `libuci`, `libubus`, and SQLite memory.
- [x] Ensure image builders can select `edgepulse` without AI agent support for low-memory targets.
- [x] Document example `.config` entries for enabling the agent in the standalone `edgepulse-openwrt-feed` workflow.

UCI configuration todo:

- [x] Extend `edgepulse-openwrt-feed/edgepulse/files/etc/config/edgepulse` with an `agent` section containing `enabled`, `local_only`, `memory_enabled`, `shell_enabled`, `ubus_enabled`, `policy_profile`, request timeout, heartbeat interval, tool timeout, and max tool output size.
- [x] Add model configuration sections for at least one remote OpenAI-compatible endpoint, including `enabled`, `role`, `base_url`, `model`, `api_key`, `api_key_env`, timeout, and retry settings.
- [x] Add defaults that let the agent report a clear "not configured" status when no API key or local model endpoint is available.
- [x] Support redacted handling for `api_key` in status output, logs, CLI commands, and LuCI.
- [x] Add UCI validation for URL format, model name presence, timeout ranges, memory toggle, shell toggle, and read-only policy mode.
- [x] Add migration-safe defaults so installing a new package does not overwrite existing telemetry settings or secret fields.

Agent runtime implementation todo:

- [x] Add an `edgepulse-agentd` daemon or an agent mode inside the existing daemon with procd lifecycle management.
- [x] Add an `edgepulse-agent` or `edgepulse-ctl agent` CLI for `ask`, `diagnose`, `status`, `memory list`, `memory delete`, and `policy show`.
- [x] Add the first `edgepulse-ctl agent status|diagnose|ask` MVP commands.
- [x] Implement request context tracking with request ID, user message, selected model, tool call history, compacted observation summary, and final answer.
- [x] Implement a read-only shell executor with an allowlist, structured argument schemas, timeout, output size limits, exit code capture, and audit logging.
- [x] Implement a read-only `ubus` adapter for `system`, `network.interface`, `network.device`, `network.wireless`, `service`, `iwinfo`, and selected status methods.
- [x] Implement an OpenAI-compatible model client with configurable base URL, model, API key source, timeout, retries, and response/error normalization.
- [x] Implement model routing for roles such as classifier, planner, analyzer, responder, and fallback.
- [x] Add local SQLite memory tables for observations, user facts, diagnostic summaries, sensitivity level, TTL, and source metadata.
- [x] Add a policy engine that blocks destructive shell commands, UCI mutation, package installation/removal, service restarts, firewall changes, and file deletion by default.
- [x] Add audit logs for every request, model call, tool call, policy decision, and memory write.
- [x] Add redaction helpers before logging or sending tool output to remote models.

LuCI application todo:

- [x] Add an AI Agent menu entry under `luci-app-edgepulse`.
- [x] Add an interaction page where the user can ask a diagnostic question and see the answer, tool evidence, model used, and policy decisions.
- [x] Add a diagnostic shortcut page or mode for common tasks such as WAN down, DNS failure, Wi-Fi instability, high CPU, high memory, and package/service health.
- [x] Extend the settings page with AI agent enable/disable, local-only mode, model provider, remote base URL, model name, API key or API key environment variable, memory toggle, shell toggle, `ubus` toggle, policy profile, and timeout settings.
- [x] Redact API keys in LuCI and require explicit replacement to change them.
- [x] Add a status panel showing whether the agent is enabled, whether a model backend is configured, last request status, memory database status, and policy mode.
- [x] Update rpcd ACLs so LuCI can call only the required agent status, ask, diagnostic, memory, and settings endpoints.

Testing and validation todo:

- [x] Add unit tests for policy allow/deny decisions and command argument validation.
- [x] Add tests for model request construction, redaction, timeout handling, retry handling, and fallback behavior.
- [x] Add tests for UCI parsing of agent and model sections.
- [x] Add fixture tests for shell and `ubus` tool output summarization.
- [x] Validate package builds with AI agent disabled and enabled.
- [x] Validate installation on OpenWrt One with AI agent disabled by default.
- [x] Validate a configured remote model can answer a read-only diagnostic request after setting UCI model parameters.
- [x] Validate LuCI interaction and settings pages on desktop and mobile layouts.

Exit criteria:

- AI agent support can be selected or omitted at OpenWrt package build time.
- Runtime UCI can enable or disable the agent after installation.
- UCI/LuCI can configure the default remote model base URL, model name, API key source, local-only mode, memory behavior, and tool policy.
- If the agent is installed without model credentials, it reports a clear configuration status instead of failing silently.
- A user can ask a diagnostic question from CLI and LuCI.
- The agent only performs read-only, policy-approved shell and `ubus` actions in the first implementation.
- Every model call and tool call is logged with secrets redacted.

Reference:

- [OpenWrt AI Agent project requirements plan](openwrt_ai_agent_requirements_plan.md)

## Phase 8A: AI Agent Live Model Validation

Status: validated on OpenWrt One

Validate the installed OpenWrt One AI agent against the currently configured model service and make failures observable from the router.

Live validation todo:

- [x] Record the initial OpenWrt One agent/model state and confirm whether `local_only` is intentionally blocking remote model use.
- [x] Run a local-only diagnostic and verify it returns local telemetry without calling the configured remote model.
- [x] Temporarily enable remote model use and verify a diagnostic question reaches the configured OpenAI-compatible model, returns an answer, and keeps API keys redacted.
- [x] Run a negative model-path test with an unreachable endpoint and verify fallback behavior is clear.
- [x] Verify read-only policy evidence, tool output, memory entries, and SQLite audit records are written after diagnostic requests.
- [x] Verify `logread` contains useful AI agent request/model/tool summaries with secrets redacted.
- [x] Verify LuCI backend commands can read status/memory and submit a diagnostic request using the same agent path.
- [x] Restore the OpenWrt One agent/model settings to the pre-test state after validation.

Live validation scenarios:

- Scenario 1: Agent enabled with `local_only=1`; ask for WAN/DNS health and expect `model_request.status=local_only`.
- Scenario 2: Agent enabled with `local_only=0`; ask for CPU/memory/network health and expect `model_response.status=ok`.
- Scenario 3: Remote endpoint intentionally invalid; expect a non-OK model response plus a local fallback answer.
- Scenario 4: Read-only policy remains active; allowed tools run and destructive operations remain outside the exposed agent action set.
- Scenario 5: LuCI helper path `/usr/libexec/edgepulse-luci agent-diagnose` produces the same structured diagnostic output used by the web UI.

Validation notes:

- `edgepulse-ctl agent ask` now logs redacted request, tool, model, and policy summaries to `logread` under `edgepulse-agent`.
- `edgepulse-ctl agent audit list` exposes recent SQLite audit events for router-side inspection.
- HTTPS/OpenAI-compatible model calls use `uclient-fetch`; API keys are redacted from JSON output and syslog summaries.
- The model response JSON and syslog model summary now include `finish_reason`, `reasoning_present`, `no_think`, and `max_tokens` so reasoning-only responses are visible from OpenWrt.
- `no_think` is configurable, but the configured Qwen OpenAI-compatible endpoint did not reliably honor `/no_think`; with `no_think=1` it repeatedly spent the full response budget on `reasoning_content`.
- The validated settings for the current OpenWrt One model are `no_think=0`, `max_tokens=2048`, `timeout_sec=60`, and `retry_count=0`; this produced `finish_reason=stop` and usable assistant content for the diagnostic summary.
- The default model prompt was reduced to a compact telemetry field summary. Full tool evidence remains in the structured agent output and audit/log records.
- The MVP still falls back to a local telemetry summary when a model returns HTTP 200 without assistant content.
- OpenWrt One was left with the agent enabled, remote model use enabled, and the validated model settings applied.

Follow-up model selection work:

- [x] Add CLI model inventory commands: `edgepulse-ctl agent models list` and `edgepulse-ctl agent models remote-list [section]`.
- [x] Fetch OpenAI-compatible `/models` from the configured endpoint using the configured token, while keeping API keys redacted.
- [x] Add `priority` to model sections so enabled model configs can be ordered for inference.
- [x] Try configured models by priority and fall back to the next ready model when the current model is unreachable or returns no usable assistant content.
- [x] Expose model priority, remote model choices, and copyable model config snippets through LuCI.
- [x] Validate failover on OpenWrt One by adding a temporary unreachable higher-priority model and confirming the agent falls back to the working `remote_reasoner`.

## MVP Definition

The first MVP is complete when OpenWrt One can:

- [x] Run `edgepulse` as a lightweight daemon.
- [x] Store volatile telemetry in `/tmp/edgepulse/edgepulse.db`.
- [x] Derive time-window features.
- [x] Show latest metrics and settings in LuCI.
- [x] Export feature rows for external model training.

The AI agent extension becomes implementation-ready when:

- [x] Build-time and runtime feature toggles are documented.
- [x] Default model and credential configuration paths are defined.
- [x] LuCI interaction and settings pages are planned as concrete tasks.
- [x] The first read-only diagnostic policy is implemented and tested.
