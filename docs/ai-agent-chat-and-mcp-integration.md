# AI Agent Chat and MCP Integration

Review date: 2026-05-10

This document defines how EdgePulse supports shared AI Agent conversations across CLI and LuCI, and how the current local C MCP adapter should relate to future external bridge tools.

## Short Answer

- UCI should configure chat mode, model backends, memory, and policy, but UCI itself should not be the chat interface.
- CLI and LuCI should both talk to the same EdgePulse agent runtime and the same SQLite conversation tables.
- The current daemon should become the local authority for OpenWrt operations, policy checks, audit logs, and conversation persistence.
- The current first MCP surface is the local C adapter in `edgepulse-ctl agent mcp`; it supports method listing, direct method calls, and local stdio JSON-RPC.
- `openwrt-mcp-server` is out of scope for the current implementation pass. If reintroduced later, it should be a separate bridge process that calls EdgePulse local methods instead of duplicating router command logic.
- Do not merge a remote Rust MCP server directly into the C daemon. Keep the boundary explicit: EdgePulse owns router-local policy and state; a bridge owns remote transport.

## Current Status

Implemented:

- CLI shared chat: `edgepulse-ctl agent chat ask <conversation_id> <message>` and `edgepulse-ctl agent chat list [conversation_id]`.
- CLI deterministic skills: `edgepulse-ctl agent skill list`, `skill plan <skill_id>`, and `skill run <skill_id> [--confirm] [options]`, backed by built-in skills plus validated JSON manifests.
- LuCI helper shared chat: `/usr/libexec/edgepulse-luci agent-chat-ask` and `agent-chat-list`.
- LuCI AI Agent page loads shared transcript state and renders Diagnostic output as a human-readable report.
- UCI defaults include `chat_enabled`, `default_conversation_id`, and `mcp_enabled`.
- Local C MCP adapter exposes chat, status, skill, action, audit, read-only `ubus`, and limited EdgePulse UCI methods.
- Skill manifests can be loaded from `EDGEPULSE_SKILLS_DIR`, local `skills.d/`, or `/usr/share/edgepulse/skills.d`; manifests only map to fixed EdgePulse actions.
- The `edgepulse.agent` ubus object exposes `mcp.tools.list` and `mcp.tools.call` wrappers over the same local C MCP adapter.
- OpenWrt One validation confirmed CLI, LuCI helper, and MCP stdio can read the same conversation history.

## Shared Conversation Model

Conversation state lives in `/tmp/edgepulse/edgepulse.db`.

Tables:

- `agent_conversations`: conversation metadata, title, created time, updated time.
- `agent_messages`: ordered user and assistant messages for each conversation.
- `agent_requests`: request-level status and answer summary.
- `agent_audit_log`: policy, tool, model, and action events.

The same records are visible from:

- CLI: `edgepulse-ctl agent chat list`, `edgepulse-ctl agent chat list <conversation_id>`, `edgepulse-ctl agent chat ask <conversation_id> "<message>"`
- LuCI: `/usr/libexec/edgepulse-luci agent-chat-list [conversation_id]` and `/usr/libexec/edgepulse-luci agent-chat-ask <conversation_id> <message>`
- Local C MCP: JSON-RPC methods that call the EdgePulse local agent path and return the same conversation IDs and messages.

## LuCI Chat Interface

`luci-app-edgepulse` now exposes the same backend conversation path as CLI. The Diagnostic view produces a structured, human-readable report and refreshes shared conversation state.

Current and required behavior:

- Show the active conversation transcript.
- Send a new message into the selected conversation through `agent-chat-ask`.
- Refresh the transcript after every request so CLI-originated messages are visible in LuCI.

Future UI work:

- Show recent conversations and allow switching between them.
- Offer operation shortcuts such as router status, Wi-Fi status, recent logs, reconnect WAN, restart Wi-Fi, and Wi-Fi setup.
- For confirmed operations, show a confirmation step before calling the confirmed action path.

The UI should use the same backend path as CLI so there is no split brain.

## UCI Role

UCI should remain configuration, not transcript storage.

Recommended UCI fields:

```uci
config agent 'agent'
    option enabled '1'
    option local_only '0'
    option memory_enabled '1'
    option shell_enabled '1'
    option ubus_enabled '1'
    option policy_profile 'read_only'
    option allow_reconnect_wan '1'
    option allow_wifi_restart '1'
    option allow_wifi_set '1'
    option chat_enabled '1'
    option default_conversation_id 'default'
    option mcp_enabled '0'
```

Why not store conversations in UCI:

- UCI is not designed for high-churn chat history.
- Chat logs can grow quickly.
- `/tmp/edgepulse/edgepulse.db` is already the volatile runtime state database and can be shared across CLI, LuCI, daemon, and bridge tools.

## Daemon Design

The daemon should eventually provide a local IPC API instead of forcing every caller through one-shot CLI execution.

Recommended local API:

- `agent.status`
- `agent.chat.ask`
- `agent.chat.list`
- `agent.chat.messages`
- `agent.action.run`
- `agent.policy.show`
- `agent.audit.list`

Transport options:

- First step: CLI-backed wrapper, already compatible with LuCI and MCP bridge prototypes.
- OpenWrt-native step: register a local `ubus` object such as `edgepulse.agent`.
- Optional step: Unix domain socket for streaming model/tool progress.

`ubus` is the best OpenWrt-native control plane because LuCI, rpcd ACLs, shell tools, and other local services can all use it.

## MCP Integration

The current MCP implementation is local and C-based:

```sh
edgepulse-ctl agent mcp methods
edgepulse-ctl agent mcp call <method> [args]
edgepulse-ctl agent mcp serve
```

`edgepulse-ctl agent mcp serve` accepts local stdio JSON-RPC requests for
`initialize`, `tools/list`, and `tools/call`.

A future `openwrt-mcp-server` should integrate as a bridge:

```text
External AI / MCP client
        |
        | HTTP / MQTT / JSON-RPC
        v
openwrt-mcp-server or another external bridge
        |
        | local CLI, ubus, or Unix socket
        v
EdgePulse agent runtime
        |
        | policy, UCI, ubus, tools, SQLite
        v
OpenWrt system
```

This keeps security and ownership clear:

- EdgePulse decides whether an action is allowed.
- EdgePulse writes the audit log.
- EdgePulse owns conversation persistence.
- The bridge authenticates remote clients and translates JSON-RPC/MCP-style tool calls into EdgePulse local calls.

## Proposed MCP Methods

The bridge should expose a small, stable method set:

| Method                       | EdgePulse mapping                                          |
|------------------------------|------------------------------------------------------------|
| `edgepulse.status`           | `edgepulse-ctl status --json`                              |
| `edgepulse.agent.status`     | `edgepulse-ctl agent status`                               |
| `edgepulse.agent.chat.list`  | `edgepulse-ctl agent chat list [conversation_id]`          |
| `edgepulse.agent.chat.ask`   | `edgepulse-ctl agent chat ask <conversation_id> <message>` |
| `edgepulse.agent.skill.list` | `edgepulse-ctl agent skill list`                           |
| `edgepulse.agent.skill.plan` | `edgepulse-ctl agent skill plan <skill_id>`                |
| `edgepulse.agent.skill.run`  | `edgepulse-ctl agent skill run <skill_id> [--confirm]`     |
| `edgepulse.agent.action.run` | `edgepulse-ctl agent action <action> [--confirm]`          |
| `edgepulse.agent.audit.list` | `edgepulse-ctl agent audit list`                           |
| `edgepulse.ubus.status.network` | read-only `ubus call network.interface dump`            |
| `edgepulse.ubus.status.wireless` | read-only `ubus call network.wireless status`          |
| `edgepulse.uci.get.edgepulse` | read-only `uci show edgepulse`                            |

For state-changing methods, the MCP bridge must pass explicit confirmation and the EdgePulse policy engine must still enforce `operator_confirmed`.

## Integration Plan

- [x] Add shared conversation tables to the EdgePulse SQLite schema.
- [x] Add CLI chat commands backed by the shared conversation store.
- [x] Add LuCI wrapper commands for chat list and chat ask.
- [x] Replace the single-output LuCI diagnostic area with a readable diagnostic report and shared transcript refresh.
- [x] Add LuCI refresh of shared transcript after every message.
- [x] Add UCI defaults for `chat_enabled`, `default_conversation_id`, and `mcp_enabled`.
- [x] Add a local C MCP stdio adapter with `initialize`, `tools/list`, and `tools/call`.
- [x] Validate that CLI, LuCI helper, and MCP can see the same conversation history on OpenWrt One.
- [x] Add an EdgePulse `ubus` object for local agent calls.
- [x] Add built-in deterministic skill list, plan, and run paths over CLI, MCP, and ubus.
- [x] Add validated JSON skill manifest loading and package install support.
- [x] Add direct ubus wrappers for MCP tool listing and tool calls.
- [ ] Add LuCI conversation selection and operation shortcuts.
- [x] Add end-to-end tests for mixed-origin CLI, LuCI, and MCP conversation writes.
- [ ] If `openwrt-mcp-server` returns to scope, update its executor to call EdgePulse local APIs instead of placeholder command execution.
- [x] Package or document any Rust bridge as an optional companion service, not as the policy authority.

## Recommendation

Use a layered design:

1. EdgePulse C agent runtime is the local source of truth.
2. LuCI is one local UI over the same agent API and SQLite transcript.
3. The local C MCP adapter is the first MCP surface.
4. A future Rust bridge is a companion transport for external AI tools, not the owner of OpenWrt policy.

This avoids duplicating safety logic and keeps future AI integrations from bypassing router-local policy and audit controls.
