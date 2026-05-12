EdgePulse OpenWrt AI Agent Execution Plan

Repository: edgepulse-labs/edgepulse￼

⸻

1. Project Objective

Build an ultra-lightweight AI Agent framework in C for OpenWrt environments.

The system should provide:

* Local-first execution
* Policy-gated actions
* Skill-based task execution
* MCP-compatible tool exposure
* OpenWrt-native integrations
* Extremely low memory footprint
* Offline-capable operation
* Minimal dependency chain
* Safe operational boundaries

The OpenWrt node is NOT intended to host a full autonomous LLM runtime.

Instead:

* OpenWrt hosts:
    * policy-authoritative executor
    * telemetry collector
    * skill engine
    * MCP tool server
    * audit engine
    * task planner
    * OpenWrt capability bridge
* External or local LLM:
    * performs NLP
    * intent understanding
    * summarization
    * recommendation generation

LLM must NEVER directly execute unrestricted system actions.

⸻

2. Architectural Principles

2.1 Security-first

Never allow:

* arbitrary shell execution
* unrestricted ubus calls
* unrestricted UCI mutation
* unrestricted filesystem writes
* unrestricted package installation

All execution paths must pass through:

Intent
 -> Skill Registry
 -> Policy Engine
 -> Confirmation Gate
 -> Skill Runner
 -> Post Verification
 -> Audit Logger

⸻

2.2 OpenWrt-native

Prefer:

* ubus
* uci
* procd
* rpcd ACL
* json-c
* uloop
* blobmsg

Avoid:

* Python runtime
* Node.js runtime
* Large C++ runtime
* container-heavy architectures

⸻

2.3 Local-first

The system must continue operating without cloud connectivity.

Minimum local functionality:

* telemetry
* diagnostics
* rule-based skill execution
* MCP tool exposure
* policy validation
* local logging

⸻

3. High-Level Architecture

+========================================================+
|                  User Interfaces                       |
|--------------------------------------------------------|
| LuCI | CLI | MCP Client | Remote AI Orchestrator       |
+========================================================+
                          |
                          v
+========================================================+
|                 edgepulse-agentd                       |
|--------------------------------------------------------|
| Intent Classifier                                      |
| Skill Registry                                         |
| Skill Planner                                          |
| Policy Engine                                          |
| Confirmation Gate                                      |
| MCP Server                                             |
| Audit Logger                                           |
| Task Runner                                            |
+========================================================+
                          |
                          v
+========================================================+
|             OpenWrt Capability Layer                   |
|--------------------------------------------------------|
| ubus                                                   |
| UCI                                                    |
| procd                                                  |
| logread                                                |
| iwinfo                                                 |
| network.interface                                      |
| telemetry database                                     |
+========================================================+

⸻

4. Proposed Directory Structure

edgepulse/
├── src/
│   ├── agent/
│   │   ├── agent_main.c
│   │   ├── agent_intent.c
│   │   ├── agent_skill_registry.c
│   │   ├── agent_skill_runner.c
│   │   ├── agent_policy.c
│   │   ├── agent_audit.c
│   │   ├── agent_mcp.c
│   │   ├── agent_confirm.c
│   │   └── agent_task_planner.c
│   │
│   ├── openwrt/
│   │   ├── ubus_wrapper.c
│   │   ├── uci_wrapper.c
│   │   ├── procd_wrapper.c
│   │   ├── wifi_wrapper.c
│   │   └── logread_wrapper.c
│   │
│   ├── skills/
│   │   ├── skill_loader.c
│   │   ├── skill_manifest.c
│   │   └── skill_executor.c
│   │
│   └── mcp/
│       ├── mcp_server.c
│       ├── mcp_jsonrpc.c
│       └── mcp_tool_registry.c
│
├── skills.d/
│   ├── openwrt_status.json
│   ├── wan_reconnect.json
│   ├── wifi_restart.json
│   └── dns_check.json
│
├── tests/
│   ├── agent_policy_test.c
│   ├── skill_runner_test.c
│   ├── mcp_jsonrpc_test.c
│   ├── ubus_fixture_test.c
│   └── uci_redaction_test.c
│
└── docs/

⸻

5. Skill System Design

5.1 Objective

Skills are deterministic operational workflows.

They replace arbitrary LLM-generated commands.

⸻

5.2 Skill Manifest Format

Example:

{
  "id": "openwrt.wan.reconnect",
  "title": "Reconnect WAN",
  "description": "Reconnect WAN interface and verify connectivity",
  "intent_examples": [
    "WAN disconnected",
    "reconnect wan",
    "network cannot access internet"
  ],
  "required_policy": "operator_confirmed",
  "requires_confirm": true,
  "inputs_schema": {
    "interface": "wan"
  },
  "steps": [
    {
      "tool": "ubus.network.interface.status",
      "args": {
        "interface": "wan"
      }
    },
    {
      "tool": "openwrt.ifdown",
      "args": {
        "interface": "wan"
      }
    },
    {
      "tool": "openwrt.ifup",
      "args": {
        "interface": "wan"
      }
    },
    {
      "tool": "openwrt.verify.wan"
    }
  ]
}

⸻

6. Required Core Components

6.1 Skill Registry

Responsibilities:

* load manifests
* validate schemas
* map intents to skills
* expose metadata
* version tracking

⸻

6.2 Skill Runner

Responsibilities:

* execute ordered steps
* validate policies
* inject runtime context
* track execution state
* handle rollback
* enforce timeouts

⸻

6.3 Policy Engine

Policy levels:

read_only
operator_confirmed
admin_only
restricted

Examples:

Action	Policy
show wifi status	read_only
restart WAN	operator_confirmed
modify firewall	admin_only
install packages	restricted

⸻

6.4 Confirmation Gate

Mutation actions must require:

* CLI confirmation
* LuCI approval
* remote orchestration approval

Example:

This action will restart WAN interface.
Continue? [y/N]

⸻

6.5 Audit Engine

All executions must generate immutable logs:

{
  "timestamp": "...",
  "skill": "openwrt.wan.reconnect",
  "operator": "local_cli",
  "result": "success",
  "duration_ms": 842
}

Sensitive fields must be redacted.

⸻

7. MCP Integration

7.1 Current Gap

Current implementation already contains:

* local C MCP adapter
* methods
* stdio serving
* early MCP execution support

Still missing:

* full JSON-RPC 2.0
* initialize
* tools/list
* tools/call
* long-running server
* ACL enforcement
* fixture testing

⸻

7.2 Required MCP APIs

initialize

{
  "method": "initialize"
}

⸻

tools/list

{
  "method": "tools/list"
}

⸻

tools/call

{
  "method": "tools/call",
  "params": {
    "name": "openwrt.status.summary"
  }
}

⸻

7.3 MCP Tool Exposure Rules

MCP tools must expose:

* metadata
* schemas
* policy requirements
* read-only flags

Never expose:

* unrestricted shell
* unrestricted ubus
* unrestricted filesystem access

⸻

8. OpenWrt Integration Layer

8.1 Required Wrappers

ubus_wrapper

Provides:

* network status
* interface control
* DHCP state
* system state

⸻

uci_wrapper

Provides:

* controlled configuration reads
* validated configuration writes
* schema enforcement

⸻

procd_wrapper

Provides:

* service status
* service restart
* service enable/disable

⸻

wifi_wrapper

Provides:

* iwinfo access
* SSID state
* station count
* channel utilization

⸻

9. Initial Skill Set

9.1 Read-only Skills

openwrt.status.summary
openwrt.wifi.status
openwrt.logs.recent
openwrt.service.status
openwrt.dns.diagnose

⸻

9.2 Confirmed Mutation Skills

openwrt.wan.reconnect
openwrt.wifi.restart
openwrt.wifi.set_ssid

⸻

10. Long-running Agent Service

Current implementation appears CLI-oriented.

A persistent daemon is required.

⸻

10.1 Recommended Interface

Expose via ubus:

ubus object: edgepulse.agent

Methods:

status
skill.list
skill.plan
skill.run
chat.ask
action.run
policy.show
audit.list
mcp.tools.list
mcp.tools.call

⸻

10.2 Why ubus

Advantages:

* native OpenWrt IPC
* low overhead
* ACL integration
* LuCI compatibility
* async support
* lightweight

⸻

11. Security Requirements

11.1 Hard Restrictions

Never permit:

system()
popen()
fork arbitrary shell
eval
dynamic command concatenation

⸻

11.2 Secret Protection

Must redact:

* Wi-Fi passwords
* API keys
* tokens
* SSH keys
* PPPoE credentials

⸻

11.3 LLM Safety Boundary

LLM may:

* classify intent
* summarize logs
* suggest diagnosis

LLM may NOT:

* directly execute actions
* bypass policy engine
* generate raw shell commands for execution

⸻

12. Test Strategy

Required tests:

tests/
├── policy_tests
├── skill_execution_tests
├── ubus_fixture_tests
├── MCP_JSONRPC_tests
├── ACL_tests
├── redaction_tests
└── failure_recovery_tests

⸻

13. MVP Definition

13.1 MVP Success Criteria

The MVP is complete when:

* edgepulse-agentd runs persistently
* ubus API is functional
* 5 read-only skills work
* 2 mutation skills work
* MCP tools/list works
* MCP tools/call works
* LuCI integration works
* policy enforcement works
* confirmation gate works
* audit logging works
* redaction works
* no unrestricted execution paths exist

⸻

14. Recommended Technology Stack

Component	Technology
Event loop	uloop
IPC	ubus
Config	UCI
JSON	json-c
ACL	rpcd ACL
DB	SQLite
Build	CMake
Packaging	OpenWrt package/feed

⸻

15. Recommended Development Order

Phase 1

* edgepulse-agentd
* ubus API
* skill registry
* skill runner
* audit logging

⸻

Phase 2

* MCP JSON-RPC
* tools/list
* tools/call
* MCP ACL enforcement

⸻

Phase 3

* LuCI integration
* confirmation workflow
* policy visualization

⸻

Phase 4

* LLM integration
* local intent classifier
* summarization engine
* remote model orchestration

⸻

16. Final Strategic Direction

EdgePulse should evolve into:

A lightweight OpenWrt-native operational AI runtime
with policy-authoritative execution boundaries.

It should NOT become:

A general unrestricted shell-based autonomous agent.

The core philosophy should remain:

Deterministic Skills
+ Strict Policy Enforcement
+ Native OpenWrt Integration
+ Lightweight Runtime
+ Auditable Operations

Repository:
edgepulse-labs/edgepulse￼