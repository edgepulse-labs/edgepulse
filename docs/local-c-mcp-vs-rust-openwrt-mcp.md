# Local C MCP Adapter vs Rust OpenWrt MCP Server

Review date: 2026-05-10

This document compares two future MCP paths for EdgePulse:

- a local C MCP adapter built into the EdgePulse C runtime,
- the separate Rust `openwrt-mcp-server` bridge.

## Recommendation

Build the first MCP surface in C as a local-only adapter, then let the Rust server become an optional remote bridge later.

The local C adapter should own router-local policy, allowlists, UCI/ubus access, audit logs, and conversation storage. The Rust bridge should own remote transports such as HTTP, MQTT, TLS, tokens, sessions, and fleet integration.

Current project status: the local C adapter is implemented and validated on
OpenWrt One through `edgepulse-ctl agent mcp serve`. The Rust bridge is not in
the current implementation scope.

## Why Start With C

The C runtime is already packaged with EdgePulse and already has:

- UCI parsing for agent/model/policy settings,
- OpenWrt-oriented command allowlists,
- local telemetry and SQLite state,
- AI Agent conversation storage,
- audit logging,
- OpenWrt package integration.

Putting the first local MCP adapter here avoids duplicate safety logic.

## First C MCP Scope

First phase is intentionally local and narrow:

```sh
edgepulse-ctl agent mcp methods
edgepulse-ctl agent mcp call <method> [args]
edgepulse-ctl agent mcp serve
```

It is controlled by:

```uci
config agent 'agent'
    option mcp_enabled '0'
```

Supported first methods:

| Method                           | Scope                                     |
|----------------------------------|-------------------------------------------|
| `edgepulse.status`               | Read EdgePulse telemetry status           |
| `edgepulse.agent.status`         | Read agent/model/policy status            |
| `edgepulse.agent.chat.list`      | Read shared conversation messages         |
| `edgepulse.agent.chat.ask`       | Add a user message and assistant response |
| `edgepulse.agent.action.run`     | Run policy-gated named actions            |
| `edgepulse.agent.audit.list`     | Read audit records                        |
| `edgepulse.ubus.status.network`  | Read-only `ubus` network status           |
| `edgepulse.ubus.status.wireless` | Read-only `ubus` wireless status          |
| `edgepulse.uci.get.edgepulse`    | Read-only EdgePulse UCI config            |

Do not expose arbitrary:

- `shell.exec`,
- `ubus.call`,
- `uci.set`,
- package installation,
- service restarts,
- firewall mutation.

## Ubus And UCI Policy

`ubus` is appropriate in the first C MCP phase when it is read-only and method-specific.

Allowed examples:

- `network.interface dump`
- `network.wireless status`
- `system board`
- `system info`

`uci` is appropriate only as a limited interface.

Allowed examples:

- read `edgepulse` config,
- run confirmed named actions such as `wifi-set` through `edgepulse.agent.action.run`.

Avoid arbitrary UCI mutation because MCP clients are AI-facing tools and must not receive the raw router control plane.

## Rust Bridge Direction

The Rust `openwrt-mcp-server` can evolve as a companion bridge:

```text
External AI / fleet control
        |
        | HTTP / MQTT / TLS / token / sessions
        v
openwrt-mcp-server
        |
        | local EdgePulse MCP/CLI/ubus
        v
EdgePulse C runtime
```

Rust is better for:

- remote HTTP/MQTT transports,
- TLS and token handling,
- async connections,
- JSON schema validation,
- fleet/device orchestration,
- higher-level MCP compatibility layers.

C is better for:

- small OpenWrt package footprint,
- direct UCI/ubus/procd integration,
- local policy enforcement,
- audit records close to the action,
- low-level status collection.

## Possible Future Divergence

The two implementations may diverge intentionally:

- C adapter stays local-only, minimal, and policy-authoritative.
- Rust bridge becomes remote-capable, multi-transport, and fleet-oriented.
- C exposes stable EdgePulse methods; Rust translates external MCP clients into those methods.
- C avoids TLS/session complexity; Rust owns that complexity.
- C ships with the core package; Rust remains optional for larger devices or managed fleets.

The key rule: remote tools must not bypass EdgePulse local policy. Even if Rust grows richer, state-changing operations still flow through EdgePulse named actions and audit logging.

## Execution Plan

- [x] Add `mcp_enabled` UCI parsing and status output.
- [x] Add local C MCP method listing.
- [x] Add local C MCP method calls for read-only ubus and limited UCI read.
- [x] Route action execution through existing policy-gated named actions.
- [x] Add local stdio JSON-RPC mode with `initialize`, `tools/list`, and `tools/call`.
- [x] Preserve JSON-RPC request IDs for numeric, string, and null IDs.
- [x] Validate `tools/list` and `tools/call edgepulse.agent.chat.list` on OpenWrt One.
- [ ] Add a long-running local daemon mode over Unix domain socket or ubus.
- [ ] Add method-level ACL settings in UCI.
- [ ] Add LuCI controls for enabling local MCP and reviewing exposed methods.
- [ ] Teach Rust `openwrt-mcp-server` to call the local C MCP adapter instead of duplicating OpenWrt command logic.
- [ ] Validate that Rust remote calls and local CLI calls produce identical audit records.

## Longer-Term Development Direction

- Keep the C adapter small, local, and policy-authoritative.
- Stabilize the method names and JSON shapes before exposing them to remote
  bridges.
- Add method-level ACLs and LuCI review controls before broadening the method
  surface.
- Use `ubus` or a Unix domain socket for local long-running clients once CLI
  execution becomes too limiting.
- Revisit Rust only when remote transport, fleet identity, TLS/session
  handling, or richer MCP compatibility becomes necessary.
