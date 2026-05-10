# AI Agent Chat and MCP Integration

Review date: 2026-05-10

This document defines how EdgePulse should support shared AI Agent conversations across CLI, LuCI, and external AI tools, and how it should integrate with `openwrt-mcp-server`.

## Short Answer

- UCI should configure chat mode, model backends, memory, and policy, but UCI itself should not be the chat interface.
- CLI and LuCI should both talk to the same EdgePulse agent runtime and the same SQLite conversation tables.
- The current daemon should become the local authority for OpenWrt operations, policy checks, audit logs, and conversation persistence.
- `../openwrt-mcp-server` should be integrated as a separate bridge process that exposes EdgePulse agent capabilities to external AI/MCP clients through HTTP/MQTT/JSON-RPC.
- Do not merge the Rust MCP server directly into the C daemon. Keep the boundary explicit: EdgePulse owns router-local policy and state; MCP server owns external transport.

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
- MCP bridge: JSON-RPC methods that call the EdgePulse CLI/IPC and return the same conversation IDs and messages.

## LuCI Chat Interface

`luci-app-edgepulse` should expose a real conversation UI, not only a single diagnostic textarea.

Required behavior:

- Show recent conversations.
- Open a conversation and show user/assistant turns.
- Send a new message into the selected conversation.
- Offer operation shortcuts such as router status, Wi-Fi status, recent logs, reconnect WAN, and Wi-Fi setup.
- For confirmed operations, show a confirmation step before calling the confirmed action path.
- Refresh the transcript after every request so CLI-originated messages are visible in LuCI.

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

`openwrt-mcp-server` should integrate as a bridge:

```text
External AI / MCP client
        |
        | HTTP / MQTT / JSON-RPC
        v
openwrt-mcp-server
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
- MCP server authenticates remote clients and translates JSON-RPC/MCP-style tool calls into EdgePulse local calls.

## Proposed MCP Methods

The bridge should expose a small, stable method set:

| Method                       | EdgePulse mapping                                          |
|------------------------------|------------------------------------------------------------|
| `edgepulse.status`           | `edgepulse-ctl status --json`                              |
| `edgepulse.agent.status`     | `edgepulse-ctl agent status`                               |
| `edgepulse.agent.chat.list`  | `edgepulse-ctl agent chat list [conversation_id]`          |
| `edgepulse.agent.chat.ask`   | `edgepulse-ctl agent chat ask <conversation_id> <message>` |
| `edgepulse.agent.action.run` | `edgepulse-ctl agent action <action> [--confirm]`          |
| `edgepulse.agent.audit.list` | `edgepulse-ctl agent audit list`                           |

For state-changing methods, the MCP bridge must pass explicit confirmation and the EdgePulse policy engine must still enforce `operator_confirmed`.

## Integration Plan

- [x] Add shared conversation tables to the EdgePulse SQLite schema.
- [x] Add CLI chat commands backed by the shared conversation store.
- [ ] Add LuCI wrapper commands for chat list and chat ask.
- [ ] Replace the single-output LuCI diagnostic area with a transcript view.
- [ ] Add LuCI refresh of shared transcript after every message.
- [ ] Add UCI defaults for `chat_enabled`, `default_conversation_id`, and `mcp_enabled`.
- [ ] Add an EdgePulse `ubus` object for local agent calls.
- [ ] Update `openwrt-mcp-server` command executor to call EdgePulse local APIs instead of placeholder command execution.
- [ ] Package `openwrt-mcp-server` as an optional OpenWrt feed package or document it as an external companion service.
- [ ] Add end-to-end tests proving CLI, LuCI, and MCP can see the same conversation history.

## Recommendation

Use a layered design:

1. EdgePulse C agent runtime is the local source of truth.
2. LuCI is one local UI over the same agent API and SQLite transcript.
3. `openwrt-mcp-server` is a companion bridge for external AI tools, not the owner of OpenWrt policy.

This avoids duplicating safety logic and keeps future AI integrations from bypassing router-local policy and audit controls.
