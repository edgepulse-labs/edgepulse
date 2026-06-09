# EdgePulse Skills And External MCP Services

Review date: 2026-06-09

This document defines how EdgePulse should expose deterministic skills and how
it should integrate external MCP services without letting model output bypass
router-local policy, audit, and confirmation gates.

## Current Baseline

EdgePulse already has the local pieces needed for the first skill surface:

- `edgepulse-ctl agent skill list`
- `edgepulse-ctl agent skill plan <skill_id>`
- `edgepulse-ctl agent skill run <skill_id> [--confirm] [options]`
- Built-in deterministic OpenWrt skills mapped to fixed `agent action` IDs.
- Optional JSON skill manifests loaded from `EDGEPULSE_SKILLS_DIR`, local
  `skills.d/`, or `/usr/share/edgepulse/skills.d`.
- MCP tools for `edgepulse.agent.skill.list`, `edgepulse.agent.skill.plan`, and
  `edgepulse.agent.skill.run`.

The important design rule is that a skill is not arbitrary code. A skill is a
small, auditable task descriptor that maps an operator intent to an existing
EdgePulse action and its policy requirements.

## Skill Manifest Contract

A skill manifest is a JSON file installed under `/usr/share/edgepulse/skills.d`
or provided during development through `EDGEPULSE_SKILLS_DIR`.

Required fields:

- `id`: Stable skill ID.
- `title`: Human-readable title.
- `description`: Short operator-facing description.
- `action`: Existing EdgePulse action ID such as `status`, `wifi-status`,
  `dns-diagnose`, `reconnect-wan`, or `wifi-set`.
- `required_policy`: `read_only` or `operator_confirmed`.
- `steps`: Evidence or operation step names that describe the deterministic
  execution path.

Recommended fields:

- `version`: Manifest version.
- `requires_confirm`: `true` only for mutation skills.
- `inputs_schema`: JSON object schema describing accepted arguments.

Example:

```json
{
  "id": "openwrt.dns.quick_check",
  "title": "DNS Quick Check",
  "description": "Check basic IP and DNS reachability through bounded commands.",
  "version": "1",
  "action": "dns-diagnose",
  "required_policy": "read_only",
  "requires_confirm": false,
  "steps": [
    "net.ping.ip",
    "net.ping.dns"
  ],
  "inputs_schema": {
    "type": "object",
    "properties": {}
  }
}
```

## Runtime Flow

Skill execution must keep the existing EdgePulse action layer as the only
executor:

```text
User / LuCI / MCP client
        |
        v
edgepulse-ctl agent skill run <skill_id>
        |
        v
Skill Registry
        |
        v
Policy Engine and Confirmation Gate
        |
        v
edgepulse-ctl agent action <action>
        |
        v
Audit Log, Tool Evidence, Final JSON Result
```

This gives the model a way to select a known task without gaining a new way to
run shell commands, mutate UCI, call arbitrary `ubus` methods, or bypass action
permissions.

## MCP Server Surface

EdgePulse currently acts as a local MCP server surface through the C adapter.
The safe tool set is:

- `edgepulse.agent.skill.list`
- `edgepulse.agent.skill.plan`
- `edgepulse.agent.skill.run`
- `edgepulse.agent.action.run`
- `edgepulse.agent.audit.list`
- read-only status, `ubus`, and controlled UCI methods

An external AI tool should call `skill.plan` before `skill.run` when presenting
an operation to a user. For mutation skills, `skill.run` must only receive
`confirm=true` after an explicit operator confirmation in the client UI.

## External MCP Service Client Direction

EdgePulse may later call external MCP services, but that should be a separate
client capability from the current local MCP server adapter.

Recommended first client scope:

- Local stdio transport only.
- Static UCI configuration for each allowed MCP server.
- `initialize`, `tools/list`, and `tools/call`.
- Per-server and per-tool allowlists.
- Timeout, output-size limit, and audit logging on every call.
- No remote network transport in the first implementation.

Possible UCI shape:

```uci
config mcp_server 'local_dns_tools'
    option enabled '1'
    option transport 'stdio'
    option command '/usr/bin/edgepulse-mcp-dns-tools'
    option timeout_sec '10'
    list allowed_tool 'dns.lookup'
    list allowed_tool 'dns.trace'
```

External MCP tool execution should use the same validation path as local tools:

```text
Model proposes external tool call
        |
        v
Skill or action policy checks
        |
        v
MCP server and tool allowlist checks
        |
        v
stdio MCP tools/call with timeout
        |
        v
Redaction, audit log, observation returned to model
```

## Non-Goals

- No unrestricted chatbot command execution.
- No arbitrary `shell.exec`, `ubus.call`, `uci.set`, package management, or
  firewall mutation through skills or MCP.
- No remote MCP service listener in the router-local daemon by default.
- No model-owned permission decisions. EdgePulse remains policy-authoritative.

## Implementation Order

1. Keep the current built-in skill and manifest loader as the first production
   skill surface.
2. Add LuCI skill list, plan, and run controls over the existing CLI or ubus
   wrappers.
3. Add tests for manifest validation, read-only skill runs, mutation
   confirmation refusal, and MCP `tools/call` skill routing.
4. Add a local stdio MCP client only after the skill/action policy surface is
   stable.
5. Add remote MCP transports only if deployment authentication, authorization,
   and audit requirements are defined.
