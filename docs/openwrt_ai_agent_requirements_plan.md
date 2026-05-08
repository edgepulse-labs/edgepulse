# OpenWrt AI Agent Project Requirements Plan

## 1. Project Overview

This project aims to build a lightweight, extensible AI Agent runtime for OpenWrt-based devices. The agent can execute multiple shell commands, interact with OpenWrt `ubus`, maintain request context, communicate with multiple AI model API servers, and build local memory on the device.

The initial target platform is an OpenWrt Wi-Fi router or CPE device, but the architecture should also be portable to Raspberry Pi, Linux gateways, embedded Linux devices, and x86 edge systems.

## 2. Project Goals

### 2.1 Primary Goals

- Provide an AI Agent runtime suitable for OpenWrt and embedded Linux environments.
- Allow the agent to safely execute shell commands under controlled policies.
- Allow the agent to query and invoke OpenWrt `ubus` objects and methods.
- Maintain request context across multi-step agent tasks.
- Support multiple AI model API servers, including local and remote providers.
- Maintain local device memory for historical state, observations, decisions, and user-defined facts.
- Provide a modular architecture for tool execution, model routing, memory, policy control, and logging.

### 2.2 Non-Goals for Initial Version

- Full autonomous system administration without user or policy constraints.
- Unrestricted shell access.
- Large-scale model training on the OpenWrt device.
- Full vector database deployment on low-memory router targets.
- Replacing existing OpenWrt management systems such as LuCI, netifd, procd, or rpcd.

## 3. Target Use Cases

### 3.1 Device Diagnostics

The agent can inspect system status and explain device problems.

Examples:

- Check CPU, memory, flash, swap, and process status.
- Inspect network interface state through `ubus`.
- Diagnose Wi-Fi instability.
- Check WAN connectivity and DNS resolution.
- Analyze logs from `logread`, `dmesg`, and service-specific logs.

### 3.2 OpenWrt Configuration Assistance

The agent can inspect and assist with OpenWrt configuration.

Examples:

- Read UCI configuration.
- Query `ubus call network.interface dump`.
- Explain current LAN/WAN/Wi-Fi topology.
- Suggest safe configuration changes.
- Optionally apply approved configuration changes.

### 3.3 Multi-Step Troubleshooting

The agent can maintain context across multiple tool calls.

Example flow:

1. User reports slow network.
2. Agent checks interface status through `ubus`.
3. Agent checks CPU and memory pressure.
4. Agent checks Wi-Fi station statistics.
5. Agent checks recent logs.
6. Agent summarizes likely root causes.
7. Agent suggests next actions.

### 3.4 Local Memory and Historical Observation

The agent can remember selected local facts and historical observations.

Examples:

- WAN interface usually receives a public IPv4 address.
- Device memory pressure increased after a specific package was installed.
- Wi-Fi channel was changed last week.
- A user-defined preferred DNS server should be preserved.

### 3.5 Multi-Model Routing

The agent can route requests to different model API servers.

Examples:

- Use a small local model for command classification.
- Use a remote stronger model for complex reasoning.
- Use a specialized model for log analysis.
- Fall back to another model when one API server fails.

## 4. System Architecture

```text
+--------------------------+
| User Interface           |
| CLI / ubus / LuCI / API  |
+------------+-------------+
             |
             v
+--------------------------+
| Agent Runtime            |
| - Request Context        |
| - Task Planner           |
| - Tool Orchestrator      |
| - Model Router           |
| - Memory Manager         |
| - Policy Engine          |
+------------+-------------+
             |
   +---------+----------+----------------+
   |                    |                |
   v                    v                v
+---------+      +-------------+   +-------------+
| Tools   |      | Model APIs  |   | Local Memory|
| shell   |      | local/remote|   | sqlite/json |
| ubus    |      | OpenAI-like |   | vector-lite |
| uci     |      | llama.cpp   |   | logs/index  |
+---------+      +-------------+   +-------------+
```

## 5. Core Components

## 5.1 Agent Runtime

The agent runtime is the central process responsible for receiving user requests, maintaining context, planning actions, invoking tools, calling model APIs, and returning responses.

### Requirements

- Run as a daemon on OpenWrt.
- Support CLI mode for development and debugging.
- Support local IPC through Unix domain socket or `ubus` integration.
- Support request cancellation and timeout handling.
- Support structured logs for every request and tool call.
- Support low-memory operation.

### Suggested Implementation Options

- Language: C, C++, Rust, Go, or Python for prototype.
- For OpenWrt-native long-term implementation, C with `libubus`, `libuci`, `libubox`, and `libjson-c` is strongly aligned with the platform.
- A hybrid design is possible: C daemon for OpenWrt integration, higher-level agent logic through a local model API or scriptable plugin layer.

## 5.2 Request Context Manager

The request context manager stores the active state of an agent task.

### Context Should Include

- Request ID.
- User message.
- System policy profile.
- Active model session.
- Tool call history.
- Shell command results.
- `ubus` query results.
- Memory retrieval results.
- Current reasoning summary.
- Final response draft.

### Requirements

- Maintain context across multi-step requests.
- Limit context size to avoid memory pressure.
- Support context compaction and summarization.
- Prevent sensitive data from being unnecessarily sent to remote models.
- Track which data came from which tool.

## 5.3 Shell Command Executor

The shell executor allows the agent to run approved shell commands.

### Requirements

- Execute shell commands with timeout.
- Capture stdout, stderr, exit code, and execution time.
- Support parallel or batched command execution when safe.
- Support command allowlist and denylist.
- Support read-only mode by default.
- Require explicit approval or elevated policy for destructive commands.
- Sanitize command arguments.
- Prevent shell injection when commands are built from model output.

### Initial Safe Command Categories

- System inspection:
  - `uname`
  - `uptime`
  - `free`
  - `df`
  - `top -bn1`
  - `ps`
- Network inspection:
  - `ip addr`
  - `ip route`
  - `ifstatus`
  - `ping`
  - `nslookup`
- Log inspection:
  - `logread`
  - `dmesg`
- OpenWrt config read-only:
  - `uci show`

### Restricted Command Categories

- File deletion:
  - `rm`
  - `find -delete`
- Configuration mutation:
  - `uci set`
  - `uci commit`
- Service restart:
  - `/etc/init.d/* restart`
  - `wifi reload`
- Package installation/removal:
  - `opkg install`
  - `opkg remove`
- Firewall changes:
  - `iptables`
  - `nft`
  - `fw4`

These commands should require explicit policy permission and preferably user approval.

## 5.4 OpenWrt ubus Tool Adapter

The ubus adapter allows the agent to query and invoke OpenWrt services through structured APIs instead of relying only on shell commands.

### Requirements

- List available `ubus` objects.
- Inspect object methods.
- Call selected `ubus` methods.
- Parse JSON responses.
- Enforce method-level policy.
- Prefer `ubus` over shell when structured data is available.

### Important ubus Objects

- `system`
- `network.interface`
- `network.device`
- `network.wireless`
- `service`
- `session`
- `file`
- `uci`
- `hostapd.*`
- `iwinfo`

### Example Operations

```sh
ubus list
ubus call system board
ubus call system info
ubus call network.interface dump
ubus call network.device status '{"name":"eth0"}'
ubus call service list
```

## 5.5 Model Router

The model router manages communication with multiple AI model API servers.

### Requirements

- Support multiple model endpoints.
- Support OpenAI-compatible APIs.
- Support local model servers such as llama.cpp server, Ollama, vLLM, or custom inference servers.
- Support remote providers when enabled.
- Support routing policy by task type.
- Support fallback models.
- Support API timeout, retry, and circuit breaker behavior.
- Support per-model token and cost accounting when applicable.

### Model Roles

| Role | Purpose | Example Model Type |
|---|---|---|
| Classifier | Decide intent and tool safety level | Small local model |
| Planner | Create multi-step diagnostic plan | Strong local or remote model |
| Tool Result Analyzer | Analyze command and ubus output | Local or remote model |
| Summarizer | Compact context and memory | Small local model |
| Final Responder | Generate user-facing response | Strong model |

## 5.6 Local Memory Manager

The memory manager stores persistent local knowledge.

### Requirements

- Store device facts.
- Store selected user preferences.
- Store historical observations.
- Store previous diagnostic summaries.
- Support expiration policy.
- Support manual deletion.
- Support sensitive data classification.
- Keep memory local by default.

### Storage Options

| Storage | Use Case |
|---|---|
| JSON files | Simple prototype |
| SQLite | Recommended baseline |
| SQLite FTS5 | Text search over logs and notes |
| Lightweight vector index | Optional semantic retrieval |
| RRD/time-series DB | Historical metrics |

### Memory Types

| Type | Description | Example |
|---|---|---|
| Device fact | Stable system information | Board name, OpenWrt version |
| User preference | User-approved preference | Preferred DNS server |
| Observation | Historical event | WAN disconnected at 03:12 |
| Diagnostic summary | Prior troubleshooting result | High memory pressure after package install |
| Policy memory | Approved operational constraints | Never restart Wi-Fi without confirmation |

## 5.7 Policy Engine

The policy engine controls what the agent is allowed to do.

### Requirements

- Enforce command-level permissions.
- Enforce ubus method-level permissions.
- Separate read-only and write-capable operations.
- Prevent destructive actions without approval.
- Support local-only mode.
- Support remote-model data redaction.
- Support audit logging.

### Suggested Policy Levels

| Level | Description |
|---|---|
| Observe | Read-only inspection only |
| Diagnose | Read-only tools plus analysis |
| Suggest | Can propose changes but not apply them |
| Apply with approval | Can apply changes after user confirmation |
| Autonomous maintenance | Can apply predefined safe actions |

The default mode should be `Observe` or `Diagnose`.

## 5.8 Tool Orchestrator

The tool orchestrator is responsible for executing tool calls generated by the planner.

### Requirements

- Validate tool call schema.
- Check policy before execution.
- Execute independent read-only commands concurrently when safe.
- Deduplicate repeated queries.
- Normalize tool results.
- Provide tool results back to the model router.
- Stop execution when risk exceeds policy.

## 5.9 Logging and Audit Trail

### Requirements

- Log every user request.
- Log every model request and response metadata.
- Log every tool invocation.
- Log command exit code, runtime, stdout size, and stderr size.
- Redact secrets from logs.
- Keep audit logs locally.
- Support log rotation.

### Sensitive Data to Redact

- Wi-Fi passwords.
- PPPoE credentials.
- API keys.
- Session tokens.
- Private IP topology when remote sharing is disabled.
- MAC addresses when privacy mode is enabled.

## 6. Request Flow

```text
1. User sends request
2. Agent creates request context
3. Intent classifier determines task type
4. Policy engine assigns safety level
5. Planner creates tool plan
6. Tool orchestrator validates and executes tools
7. Results are added to context
8. Memory manager retrieves relevant local memory
9. Model router selects model for analysis
10. Agent generates final response
11. Useful observations may be written to local memory
12. Audit log is written
```

## 7. API and Interface Requirements

## 7.1 CLI Interface

Example:

```sh
owrt-agent ask "Why is my WAN disconnected?"
owrt-agent diagnose network
owrt-agent memory list
owrt-agent policy show
```

## 7.2 ubus Interface

Possible object name:

```text
ai.agent
```

Possible methods:

```text
ai.agent ask
ai.agent diagnose
ai.agent status
ai.agent memory.list
ai.agent memory.delete
ai.agent policy.get
ai.agent policy.set
```

Example:

```sh
ubus call ai.agent ask '{"message":"Check WAN status"}'
```

## 7.3 Local HTTP API

Optional API for LuCI or external UI.

Example endpoints:

```text
POST /v1/agent/ask
GET  /v1/agent/status
GET  /v1/memory
POST /v1/memory
DELETE /v1/memory/{id}
GET  /v1/policy
PUT  /v1/policy
```

## 8. Data Structures

## 8.1 Request Context

```json
{
  "request_id": "uuid",
  "created_at": "timestamp",
  "user_message": "string",
  "policy_level": "diagnose",
  "tool_calls": [],
  "model_calls": [],
  "memory_hits": [],
  "working_summary": "string",
  "final_response": "string"
}
```

## 8.2 Tool Call Record

```json
{
  "tool_call_id": "uuid",
  "tool_type": "shell|ubus|uci|memory",
  "command": "string",
  "arguments": {},
  "started_at": "timestamp",
  "ended_at": "timestamp",
  "exit_code": 0,
  "stdout": "string",
  "stderr": "string",
  "policy_decision": "allowed|blocked|requires_approval"
}
```

## 8.3 Model Endpoint Configuration

```json
{
  "name": "local-llama",
  "type": "openai-compatible",
  "base_url": "http://127.0.0.1:8080/v1",
  "model": "local-model-name",
  "api_key_ref": "env:LOCAL_MODEL_API_KEY",
  "roles": ["classifier", "summarizer"],
  "timeout_ms": 30000,
  "enabled": true
}
```

## 8.4 Memory Record

```json
{
  "memory_id": "uuid",
  "type": "device_fact|user_preference|observation|diagnostic_summary|policy",
  "content": "string",
  "source": "user|agent|tool",
  "created_at": "timestamp",
  "updated_at": "timestamp",
  "expires_at": null,
  "sensitivity": "normal|private|secret"
}
```

## 9. Security Requirements

### 9.1 Shell Safety

- Never execute raw model-generated shell commands without validation.
- Prefer structured tool calls over free-form shell.
- Use allowlisted binaries and argument schemas.
- Apply execution timeout.
- Limit output size.
- Run under a restricted user when possible.

### 9.2 ubus Safety

- Separate read-only methods from write methods.
- Block high-risk methods by default.
- Require explicit policy for `file`, `uci`, and service mutation methods.

### 9.3 Model API Safety

- Redact secrets before sending data to remote models.
- Allow local-only mode.
- Log model endpoint used for each request.
- Support per-provider data sharing policy.

### 9.4 Memory Safety

- Do not store secrets unless explicitly configured.
- Provide memory inspection and deletion.
- Support TTL for temporary observations.
- Mark memory records by sensitivity level.

## 10. Performance Requirements

### OpenWrt Embedded Target

- Minimal idle memory footprint.
- Avoid large resident model runtime on small routers.
- Prefer external or remote model server for low-end devices.
- Use streaming where possible.
- Limit concurrent tool execution.
- Use bounded queues.

### Suggested Resource Profiles

| Device Class | Agent Mode |
|---|---|
| 128 MB RAM router | Remote model only, minimal memory |
| 256-512 MB RAM router | Remote model plus SQLite memory |
| 1 GB RAM CPE | Local small classifier plus remote reasoning |
| Raspberry Pi / x86 | Local model server plus full memory features |

## 11. OpenWrt Packaging Requirements

### Package Structure

```text
package/openwrt-ai-agent/
├── Makefile
├── files/
│   ├── etc/config/ai-agent
│   ├── etc/init.d/ai-agent
│   ├── usr/bin/owrt-agent
│   └── usr/share/ai-agent/policy.json
└── src/
```

### OpenWrt Integration

- Provide `/etc/config/ai-agent` UCI config.
- Provide `/etc/init.d/ai-agent` procd service script.
- Register optional `ubus` object.
- Support logread-compatible logging.
- Support optional LuCI integration.

## 12. Configuration Example

```uci
config agent 'main'
    option enabled '1'
    option mode 'diagnose'
    option local_only '0'
    option memory_enabled '1'
    option shell_enabled '1'
    option ubus_enabled '1'

config model 'local_classifier'
    option enabled '1'
    option role 'classifier'
    option base_url 'http://127.0.0.1:8080/v1'
    option model 'small-local-model'

config model 'remote_reasoner'
    option enabled '1'
    option role 'planner,analyzer,responder'
    option base_url 'https://api.example.com/v1'
    option model 'large-reasoning-model'
    option api_key_env 'AI_AGENT_REMOTE_API_KEY'
```

## 13. Development Milestones

## Phase 0: Prototype

- CLI-only prototype.
- Execute safe read-only shell commands.
- Call one OpenAI-compatible model endpoint.
- Maintain in-memory request context.
- Produce diagnostic summaries.

## Phase 1: OpenWrt Integration

- Add procd service.
- Add UCI configuration.
- Add `ubus` query support.
- Add SQLite memory.
- Add basic policy engine.

## Phase 2: Multi-Model Runtime

- Add multiple model endpoint support.
- Add model role routing.
- Add fallback and retry.
- Add context compaction.
- Add local-only mode.

## Phase 3: Safe Action Execution

- Add approval workflow.
- Add controlled UCI mutation.
- Add service restart with explicit confirmation.
- Add rollback planning.
- Add audit trail viewer.

## Phase 4: Productization

- Add LuCI UI.
- Add packaged OpenWrt feed.
- Add device profile presets.
- Add metrics and health dashboard.
- Add plugin SDK for tools.

## 14. Minimal Viable Product

The MVP should include:

- CLI command: `owrt-agent ask`.
- Read-only shell executor.
- Read-only `ubus` adapter.
- One or more OpenAI-compatible model endpoints.
- Request context tracking.
- SQLite local memory.
- Basic policy engine.
- Structured audit logs.
- OpenWrt package and init script.

## 15. Engineering Risks

| Risk | Description | Mitigation |
|---|---|---|
| Shell command risk | Model may propose unsafe commands | Strict allowlist and policy engine |
| Low memory | OpenWrt devices may have limited RAM | Use bounded context and optional remote model |
| Secret leakage | Logs/config may contain credentials | Redaction and local-only mode |
| Model hallucination | Agent may misinterpret command output | Tool-grounded responses and source tracking |
| Unstable APIs | Different model servers vary in behavior | OpenAI-compatible abstraction layer |
| ubus mutation risk | Incorrect ubus call may alter system state | Read-only default and approval gate |

## 16. Recommended Initial Repository Name

Possible names:

- `openwrt-ai-agent`
- `owrt-agent`
- `ubus-agent`
- `edge-agentd`
- `router-agentd`
- `cpe-agentd`

Recommended initial name:

```text
owrt-agent
```

Reason: short, clear, OpenWrt-specific, and suitable for a CLI command, daemon name, and package name.

## 17. Success Criteria

The project is considered successful when the agent can:

- Receive a user diagnostic request.
- Build a request context.
- Query OpenWrt system state through shell and `ubus`.
- Route reasoning to the configured model API server.
- Produce a grounded diagnostic answer based on tool results.
- Store useful local memory.
- Avoid unsafe commands by default.
- Run as an OpenWrt package-managed daemon.

## 18. Example User Request

```text
My Wi-Fi clients are unstable. Please check the router status.
```

Expected behavior:

1. Query system info.
2. Query wireless status through `ubus`.
3. Query memory and CPU load.
4. Query recent logs.
5. Analyze likely causes.
6. Return findings and recommended next steps.
7. Store a diagnostic summary if memory is enabled.

## 19. Design Principle

The agent should not be designed as an unrestricted chatbot running on a router. It should be designed as a policy-controlled, tool-grounded, auditable OpenWrt operations assistant.

The core value is not only natural language interaction. The core value is safe orchestration across OpenWrt system state, shell tools, `ubus`, local memory, and multiple AI model backends.

