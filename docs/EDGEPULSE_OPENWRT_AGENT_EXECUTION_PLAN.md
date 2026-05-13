# EdgePulse OpenWrt AI Agent Execution Checklist

Repository: `edgepulse-labs/edgepulse`

Last updated: 2026-05-13

This document is the execution checklist for the OpenWrt AI Agent work. Keep it current: whenever an item is implemented and verified, change `[ ]` to `[x]` and add a short note if the implementation differs from the original plan.

## 1. Project Objective

Build an ultra-lightweight AI Agent framework in C for OpenWrt environments.

The OpenWrt node is not intended to host a full autonomous LLM runtime. EdgePulse owns router-local policy, state, audit, skill execution, and MCP exposure. External or local LLMs may classify intent, summarize evidence, and recommend next steps, but must never directly execute unrestricted system actions.

### Objective Checklist

- [x] Local-first execution path exists through CLI diagnostics, action runner, skill runner, MCP adapter, and SQLite state.
- [x] Policy-gated actions exist for read-only and confirmed mutation flows.
- [x] Skill-based task execution exists through built-in skills and validated JSON manifests.
- [x] MCP-compatible local tool exposure exists through `edgepulse-ctl agent mcp`.
- [x] OpenWrt-native integration path exists through ubus-backed daemon methods when built with `EDGEPULSE_ENABLE_UBUS`.
- [x] Offline-capable local diagnostics, policy checks, and audit logging exist.
- [x] LLM cannot directly execute arbitrary commands; execution maps to fixed local actions and allowlists.
- [ ] Continue reducing coupling in the single large `edgepulse-ctl` source file into smaller agent modules.
- [ ] Validate memory footprint on target OpenWrt hardware after each major feature group.

## 2. Architectural Principles

### 2.1 Security-First

Never allow arbitrary shell execution, unrestricted ubus calls, unrestricted UCI mutation, unrestricted filesystem writes, or unrestricted package installation.

Required execution path:

```text
Intent
 -> Skill Registry
 -> Policy Engine
 -> Confirmation Gate
 -> Skill Runner
 -> Post Verification
 -> Audit Logger
```

Checklist:

- [x] Read-only command allowlist blocks unknown shell commands.
- [x] Mutation command allowlist blocks unknown mutation commands.
- [x] Confirmed actions require `operator_confirmed` policy and `--confirm`.
- [x] Per-action policy switches exist for WAN reconnect, Wi-Fi restart, and Wi-Fi config changes.
- [x] MCP method ACLs exist in UCI-backed agent config.
- [x] Sensitive Wi-Fi keys are redacted from tool output.
- [x] AI/model responses are advisory and do not bypass policy/action gates.
- [ ] Add more complete redaction for tokens, SSH keys, API keys, and PPPoE credentials beyond current key-focused coverage.
- [ ] Add failure recovery and rollback tracking for multi-step mutation actions.

### 2.2 OpenWrt-Native

Prefer ubus, UCI, procd, rpcd ACL, json-c, uloop, and blobmsg. Avoid Python/Node.js runtime dependencies, large C++ runtimes, and container-heavy designs.

Checklist:

- [x] Persistent daemon supports heartbeat mode.
- [x] `edgepulse.agent` ubus object is implemented behind `EDGEPULSE_ENABLE_UBUS`.
- [x] CLI, LuCI helper path, MCP adapter, and ubus wrappers share local EdgePulse commands/state.
- [x] Runtime config is read from OpenWrt-style UCI config format.
- [ ] Validate ubus build and behavior in OpenWrt buildroot or target image with ubus headers.
- [ ] Add rpcd ACL package metadata in the OpenWrt feed for LuCI/ubus access control.

### 2.3 Local-First

Minimum local functionality:

- [x] Telemetry collection and SQLite state.
- [x] Diagnostics without remote model dependency.
- [x] Rule-based skill execution.
- [x] MCP tool exposure over local stdio.
- [x] Policy validation and local audit logging.
- [ ] Add target-device resource profile checks for memory and CPU overhead.

## 3. Architecture Checklist

User surfaces:

- [x] CLI through `edgepulse-ctl agent ...`.
- [x] LuCI helper path for shared chat.
- [x] Local C MCP adapter.
- [x] ubus object for local agent calls when compiled with ubus support.
- [ ] LuCI conversation picker and operation shortcuts.
- [ ] Optional external bridge process if `openwrt-mcp-server` returns to scope.

Agent runtime components:

- [x] Intent classifier for MVP action routing.
- [x] Built-in skill registry.
- [x] JSON manifest skill loader.
- [x] Skill planner via `agent skill plan`.
- [x] Policy engine and allowlists.
- [x] Confirmation gate for mutation actions.
- [x] MCP server/stdio adapter with JSON-RPC methods.
- [x] Audit logger using SQLite.
- [x] Task runner mapped to fixed actions.
- [ ] Modularize agent implementation into dedicated source files.
- [ ] Add richer task execution state and rollback records.

OpenWrt capability layer:

- [x] ubus network status through allowlisted commands.
- [x] ubus wireless status through allowlisted commands.
- [x] ubus service list through allowlisted commands.
- [x] UCI read for EdgePulse config through allowlisted MCP method.
- [x] UCI wireless writes through validated confirmed actions.
- [x] logread bounded read.
- [x] ping reachability checks.
- [x] iwinfo wrapper for detailed Wi-Fi radio/station metrics.
- [ ] procd wrapper for service restart/enable/disable.
- [ ] controlled firewall/package-operation policy stubs that explicitly remain restricted.

## 4. Directory Structure Checklist

Original target structure:

```text
edgepulse/
├── src/
│   ├── agent/
│   ├── openwrt/
│   ├── skills/
│   └── mcp/
├── skills.d/
├── tests/
└── docs/
```

Status:

- [x] Current implementation works in existing `src/edgepulse-ctl`, `src/edgepulse-daemon`, and `src/edgepulse-lib` layout.
- [x] `skills.d/` exists with a validated example manifest.
- [x] Unit and integration tests exist for policy, action, MCP, shared transcript, and manifest loading.
- [ ] Split large agent implementation into `src/agent/`, `src/skills/`, `src/mcp/`, and `src/openwrt/` modules.
- [ ] Add module-level tests after splitting source files.

## 5. Skill System

Skills are deterministic operational workflows. They replace arbitrary LLM-generated commands.

### Manifest Format

Current MVP manifest format maps a skill to a fixed local action:

```json
{
  "id": "openwrt.status.quick",
  "title": "Quick OpenWrt Status",
  "description": "Manifest-loaded read-only status skill.",
  "action": "status",
  "required_policy": "read_only",
  "requires_confirm": false,
  "read_only": true,
  "steps": [
    "shell.uptime",
    "ubus.network.interface.dump",
    "ubus.network.wireless.status"
  ]
}
```

Checklist:

- [x] Built-in skill registry.
- [x] Manifest loading from `EDGEPULSE_SKILLS_DIR`, local `skills.d/`, or `/usr/share/edgepulse/skills.d`.
- [x] Manifest action validation against fixed supported actions.
- [x] Duplicate manifest IDs are skipped when they collide with built-ins or earlier manifests.
- [x] `edgepulse-ctl agent skill list`.
- [x] `edgepulse-ctl agent skill plan <skill_id>`.
- [x] `edgepulse-ctl agent skill run <skill_id> [--confirm] [options]`.
- [x] MCP exposes skill list/plan/run.
- [x] ubus exposes skill list/plan/run.
- [x] Package install copies `skills.d/*.json` to `/usr/share/edgepulse/skills.d`.
- [ ] Add strict schema validation with better error reporting for invalid manifests.
- [ ] Add manifest `inputs_schema` support for validated action parameters.
- [ ] Add skill version tracking.
- [ ] Add richer step metadata for policy, timeout, rollback, and verification.

### Initial Skill Set

Read-only skills:

- [x] `openwrt.status.summary`
- [x] `openwrt.wifi.status`
- [x] `openwrt.wifi.metrics`
- [x] `openwrt.logs.recent`
- [x] `openwrt.service.status`
- [x] `openwrt.dns.diagnose`
- [x] `openwrt.status.quick` example manifest

Confirmed mutation skills:

- [x] `openwrt.wan.reconnect`
- [x] `openwrt.wifi.restart`
- [x] `openwrt.wifi.set_ssid`

## 6. Core Components

### 6.1 Skill Registry

- [x] Load built-in skills.
- [x] Load JSON manifests.
- [x] Validate manifest action against supported fixed actions.
- [x] Map skill IDs to actions.
- [x] Expose skill metadata.
- [ ] Validate full schemas, including inputs and steps.
- [ ] Track manifest version and source path.

### 6.2 Skill Runner

- [x] Execute skill through fixed policy-gated action mapping.
- [x] Enforce read-only vs confirmed mutation policy.
- [x] Enforce timeouts at tool execution layer.
- [x] Track tool result status and elapsed time.
- [x] Post-verify WAN reconnect, Wi-Fi restart, and Wi-Fi set through status/ping checks.
- [ ] Store explicit skill execution state records separate from generic audit rows.
- [ ] Add rollback metadata and rollback execution where safe.

### 6.3 Policy Engine

Policy levels:

- `read_only`
- `operator_confirmed`
- `admin_only`
- `restricted`

Checklist:

- [x] `read_only` policy supported.
- [x] `operator_confirmed` policy supported.
- [x] Unsupported policy profiles emit validation warnings.
- [x] Per-action switches for WAN reconnect, Wi-Fi restart, and Wi-Fi set.
- [x] Command allowlists for read-only tools.
- [x] Command allowlists for mutation tools.
- [ ] Add explicit `admin_only` and `restricted` enforcement semantics.
- [ ] Add policy visualization in LuCI.

### 6.4 Confirmation Gate

- [x] CLI `--confirm` gate.
- [x] MCP action/skill run can pass explicit confirmation.
- [x] ubus action/skill/MCP wrappers can pass explicit confirmation.
- [x] Mutation actions return `confirmation_required` when not confirmed.
- [ ] LuCI confirmation workflow.
- [ ] Remote orchestration approval workflow.

### 6.5 Audit Engine

- [x] Agent requests write audit rows.
- [x] Tool executions write audit rows.
- [x] MCP calls write audit rows.
- [x] Daemon heartbeat writes audit rows.
- [x] Audit list CLI/MCP/ubus path exists.
- [x] Sensitive Wi-Fi key output redaction.
- [ ] Audit rows are append-only by convention, but need stronger immutability guarantees.
- [ ] Add structured skill execution audit detail with duration/result fields.

## 7. MCP Integration

### Required APIs

- [x] `initialize`
- [x] `tools/list`
- [x] `tools/call`
- [x] `edgepulse-ctl agent mcp methods`
- [x] `edgepulse-ctl agent mcp call <method> [args]`
- [x] `edgepulse-ctl agent mcp serve`
- [x] Method-level ACL enforcement through UCI config.
- [x] MCP fixture testing.
- [x] ubus wrappers for `mcp.tools.list` and `mcp.tools.call`.
- [ ] Full JSON-RPC 2.0 parser behavior beyond MVP line-oriented extraction.
- [ ] Long-running socket/server transport beyond local stdio.
- [x] Tool schema metadata beyond simple names/descriptions.

### Exposure Rules

- [x] Expose metadata for available methods/tools.
- [x] Expose policy/allowed state in method listing.
- [x] Do not expose unrestricted shell.
- [x] Do not expose unrestricted ubus.
- [x] Do not expose unrestricted filesystem access.
- [x] Add richer JSON schemas for tool inputs.

## 8. OpenWrt Integration Layer

### ubus wrapper

- [x] Network interface dump.
- [x] Wireless status.
- [x] Service list.
- [x] System board/info in diagnostics.
- [x] Interface-specific status wrapper for safe `wan`, `lan`, and `wwan` interface names.
- [x] DHCP state wrapper via safe interface status evidence.
- [ ] Target validation with real ubus.

### UCI wrapper

- [x] Read-only `uci show edgepulse` through MCP method.
- [x] Validated wireless SSID/encryption/key writes.
- [x] Wireless commit action.
- [ ] Generic controlled config read API with schemas.
- [ ] Validated config write API beyond Wi-Fi MVP.

### procd/service wrapper

- [x] Read-only service status through `ubus call service list`.
- [ ] Service restart for allowlisted services.
- [ ] Service enable/disable with admin policy.

### Wi-Fi wrapper

- [x] Wireless status through ubus.
- [x] Wi-Fi reload confirmed action.
- [x] Wi-Fi SSID/key/encryption update confirmed action.
- [x] iwinfo station/channel utilization wrapper through safe `wifi-metrics --wifi-interface wlan0|wlan1`.

### logread wrapper

- [x] Bounded `logread -l 80`.
- [x] Structured log filtering and redaction through safe `logs-recent --contains` and `--level` filters plus token/API-key/password output redaction.

## 9. Long-Running Agent Service

`edgepulse-agentd` / `edgepulse` agent mode should expose local control APIs.

ubus object: `edgepulse.agent`

Implemented methods:

- [x] `status`
- [x] `skill.list`
- [x] `skill.plan`
- [x] `skill.run`
- [x] `chat.ask`
- [x] `chat.list`
- [x] `action.run`
- [x] `policy.show`
- [x] `audit.list`
- [x] `mcp.tools.list`
- [x] `mcp.tools.call`

Remaining:

- [ ] Target compile and runtime validation with `EDGEPULSE_ENABLE_UBUS`.
- [ ] rpcd ACL integration.
- [ ] Optional Unix domain socket for streaming model/tool progress.

## 10. Security Requirements

Hard restrictions:

- [x] No `system()` execution path.
- [x] No `popen()` execution path.
- [x] No unrestricted shell path.
- [x] No dynamic command concatenation for execution.
- [x] `execvp` is only used after allowlist validation or for fixed local wrapper commands.
- [x] Package installation remains restricted/unimplemented.
- [x] Add static analysis/check script to prevent accidental introduction of `system()`/`popen()`.

Secret protection:

- [x] Wi-Fi password redaction in UCI-style and JSON-ish outputs.
- [x] Model API key is redacted in status/model output.
- [ ] Token/API-key/SSH/PPPoE redaction coverage.

LLM safety boundary:

- [x] LLM may summarize and answer diagnostics.
- [x] LLM cannot call tools except through policy-gated local methods.
- [x] Intent classifier maps text to fixed actions only.
- [ ] Add model prompt hardening tests for unsafe action suggestions.

## 11. Test Strategy

- [x] Unit tests for telemetry parsing/core library.
- [x] Unit tests for agent command allowlists.
- [x] Unit tests for mutation allowlists.
- [x] Unit tests for config parsing.
- [x] Unit tests for model request/payload helpers.
- [x] Unit tests for validation warnings.
- [x] Unit tests for intent classification.
- [x] Unit tests for redaction.
- [x] Unit tests for conversation storage.
- [x] Unit tests for MCP JSON helper parsing.
- [x] Unit tests for built-in and manifest-backed skill registry.
- [x] Safety check blocks forbidden `system()`/`popen()` APIs in source.
- [x] Integration fixture for model calls.
- [x] Integration fixture for policy-gated operations.
- [x] Integration fixture for MCP methods/stdin server.
- [x] Integration fixture for shared transcript.
- [ ] ubus fixture tests with OpenWrt ubus libraries.
- [ ] ACL tests for rpcd/LuCI access.
- [ ] Failure recovery and rollback tests.

## 12. MVP Success Criteria

- [x] `edgepulse-agentd` runs persistently.
- [x] ubus API is implemented behind build flag.
- [x] Five read-only skills work.
- [x] At least two mutation skills work.
- [x] MCP `tools/list` works.
- [x] MCP `tools/call` works.
- [x] LuCI shared chat integration exists.
- [x] Policy enforcement works.
- [x] Confirmation gate works.
- [x] Audit logging works.
- [x] Redaction works for current Wi-Fi key paths.
- [x] No unrestricted execution paths exist in current implementation.
- [ ] Validate ubus API on OpenWrt target.
- [ ] Finish LuCI operation shortcuts and confirmation UI.

## 13. Recommended Technology Stack

- [x] Event loop: `uloop` for ubus build.
- [x] IPC: `ubus` object behind build flag.
- [x] Config: UCI-style config parser.
- [x] DB: SQLite.
- [x] Packaging: OpenWrt feed ownership documented.
- [ ] JSON: migrate ad hoc parsing to `json-c` where available.
- [ ] ACL: rpcd ACL metadata in feed.
- [ ] Build: current Makefile works; CMake remains optional/not adopted.

## 14. Development Phases

### Phase 1: Core Runtime

- [x] edgepulse-agentd heartbeat/runtime mode.
- [x] ubus API object.
- [x] Skill registry.
- [x] Skill runner.
- [x] Audit logging.
- [ ] Source modularization.
- [ ] Rollback state tracking.

### Phase 2: MCP

- [x] MCP JSON-RPC MVP.
- [x] `tools/list`.
- [x] `tools/call`.
- [x] MCP ACL enforcement.
- [x] Direct ubus MCP wrappers.
- [ ] Full JSON-RPC parser and richer tool schemas.

### Phase 3: LuCI

- [x] Shared chat helper path.
- [x] Diagnostic transcript refresh.
- [ ] Conversation selection.
- [ ] Operation shortcuts.
- [ ] Confirmation workflow.
- [ ] Policy visualization.

### Phase 4: LLM Integration

- [x] OpenAI-compatible local/remote model call path.
- [x] Local intent classifier.
- [x] Basic summarization/diagnostic answer path.
- [x] Remote model routing with priority/failover.
- [ ] Prompt safety regression tests.
- [ ] Streaming progress support.

## 15. Current Next Work Queue

Use this queue for the next implementation passes:

- [ ] Add `json-c` based manifest and MCP request parsing when available, with fallback or build guards if needed.
- [ ] Add structured skill execution records and rollback metadata.
- [ ] Add LuCI conversation selector and operation shortcut buttons.
- [ ] Add LuCI confirmation UI for mutation actions.
- [ ] Add rpcd ACL files in the OpenWrt feed.
- [ ] Validate `EDGEPULSE_ENABLE_UBUS` build on OpenWrt target.
- [x] Add iwinfo wrapper for richer Wi-Fi diagnostics through `wifi-metrics` and `openwrt.wifi.metrics`.
- [ ] Add procd allowlisted service restart skill under stricter policy.

## 16. Strategic Direction

EdgePulse should evolve into a lightweight OpenWrt-native operational AI runtime with policy-authoritative execution boundaries.

It should not become a general unrestricted shell-based autonomous agent.

Core philosophy:

```text
Deterministic Skills
+ Strict Policy Enforcement
+ Native OpenWrt Integration
+ Lightweight Runtime
+ Auditable Operations
```
