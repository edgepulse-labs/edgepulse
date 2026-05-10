# Project Status And Roadmap

Review date: 2026-05-10

This document is the current source of truth for what has been implemented,
what has been validated on OpenWrt One, and where EdgePulse should evolve
next. Older planning documents remain useful design records, but this page
should be checked first when a plan and the implementation appear to differ.

## Current Implementation Status

EdgePulse is no longer only a research sketch. The project now has a working
C runtime, OpenWrt feed packaging, LuCI integration, and an initial AI Agent
surface.

Implemented and validated locally:

- C telemetry daemon and `edgepulse-ctl` CLI.
- SQLite runtime database under `/tmp/edgepulse/edgepulse.db`.
- Raw telemetry collection, time-window feature extraction, and JSON export
  paths.
- OpenWrt feed packaging in `https://github.com/edgepulse-labs/edgepulse-openwrt-feed`.
- LuCI application pages for overview, metrics, features, settings, and AI
  Agent interaction.
- AI Agent configuration through UCI and LuCI, including model provider,
  local-only mode, memory, shell, `ubus`, policy, and timeout settings.
- OpenAI-compatible remote model client with redacted API key handling,
  model priority, fallback, and model inventory commands.
- Shared AI Agent conversations stored in SQLite and visible through CLI and
  LuCI helper commands.
- Diagnostic responses rendered in LuCI as a human-readable diagnostic report
  instead of raw JSON only.
- Policy-gated OpenWrt operations through `edgepulse-ctl agent action`.
- Local C MCP adapter through `edgepulse-ctl agent mcp methods`, `call`, and
  local stdio `serve`.
- First MCP methods for EdgePulse status, agent status, chat, action, audit,
  read-only network/wireless `ubus` status, and limited EdgePulse UCI read.

Validated on `ssh one` / OpenWrt One:

- Related APKs installed: `edgepulse`, `luci-app-edgepulse`, `farmhash`,
  `tensorflow-lite`, and `tensorflow-lite-test`.
- `edgepulse-ctl agent status` reports `chat_enabled=true` and
  `mcp_enabled=true` when enabled in UCI.
- CLI chat requests write to shared conversation history.
- LuCI helper `agent-chat-list` reads the same conversation history.
- MCP stdio `tools/list` and `tools/call edgepulse.agent.chat.list` work and
  preserve JSON-RPC request IDs.

## Current Boundaries

- The default safety posture remains local, policy-gated, and read-only.
- State-changing actions require `policy_profile=operator_confirmed` and an
  explicit confirmation path.
- MCP is currently a local C adapter, not a network service.
- `ubus` and UCI exposure through MCP is method-specific. Arbitrary
  `ubus.call`, `uci.set`, shell execution, package management, and firewall
  mutation are intentionally not exposed.
- `/tmp/edgepulse/edgepulse.db` is volatile runtime state. Persistent history
  remains a future option.
- Rust `openwrt-mcp-server` is not part of the current implementation path. It
  remains a possible external bridge once the local C method surface is stable.

## Near-Term Roadmap

The next implementation pass should focus on making the current feature set
more reliable and easier to operate.

1. Add LuCI controls for common `agent action` operations.
2. Add a small Chinese/English intent classifier that maps common user
   requests to `status`, `wifi-status`, `logs-recent`, `reconnect-wan`, and
   `wifi-set`.
3. Strengthen redaction for Wi-Fi keys and other sensitive action arguments in
   action output, syslog, audit records, and LuCI.
4. Add post-action verification for WAN IP, DNS reachability, wireless radio
   status, and associated clients.
5. Add fixture integration tests for `ubus`, `uci`, `logread`, `ifdown`,
   `ifup`, and `wifi`.
6. Add UCI method-level ACLs for MCP methods.
7. Add LuCI settings controls for local MCP enablement and exposed method
   review.
8. Add a local `ubus` object or Unix domain socket for long-running agent/MCP
   calls so future clients do not need to shell out through one-shot CLI
   execution.

## Medium-Term Roadmap

- Remote training-data upload with resumable delivery and clear LuCI/UCI
  controls.
- Stable feature normalization for multi-interface and multi-thermal-zone
  devices.
- Persistent or exportable agent memory for deployments that need history
  across reboot.
- Better model compatibility records for OpenAI-compatible providers,
  no-think behavior, timeouts, token budgets, and failover.
- More complete LuCI chat UX, including conversation selection and operation
  shortcuts.
- Optional `openwrt-mcp-server` bridge that calls EdgePulse local MCP/CLI/ubus
  methods instead of duplicating OpenWrt command logic.

## Long-Term Direction

EdgePulse should remain a router-local, auditable operations assistant rather
than an unrestricted chatbot. The C runtime should stay policy-authoritative
for OpenWrt state. External tools, model providers, and future Rust bridge
components should integrate through stable EdgePulse methods and should never
bypass local policy or audit logging.
