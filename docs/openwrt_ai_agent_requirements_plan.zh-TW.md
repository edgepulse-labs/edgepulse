# OpenWrt AI Agent 專案需求計畫

## 1. Project Overview

本專案目標是為 OpenWrt-based devices 建立輕量、可擴充的 AI Agent runtime。Agent 可以執行多個 shell commands、與 OpenWrt `ubus` 互動、維持 request context、與多個 AI model API servers 溝通，並在裝置上建立 local memory。

初始目標平台是 OpenWrt Wi-Fi router 或 CPE device，但架構也應可移植到 Raspberry Pi、Linux gateways、embedded Linux devices 與 x86 edge systems。

## 2. Project Goals

### 2.1 Primary Goals

- 提供適合 OpenWrt 與 embedded Linux environments 的 AI Agent runtime。
- 允許 agent 在受控 policies 下安全執行 shell commands。
- 允許 agent 查詢與呼叫 OpenWrt `ubus` objects and methods。
- 在 multi-step agent tasks 中維持 request context。
- 支援多個 AI model API servers，包含 local 與 remote providers。
- 維持 local device memory，用於 historical state、observations、decisions 與 user-defined facts。
- 提供 modular architecture，涵蓋 tool execution、model routing、memory、policy control 與 logging。

### 2.2 Non-Goals for Initial Version

- 沒有 user 或 policy constraints 的 full autonomous system administration。
- Unrestricted shell access。
- 在 OpenWrt device 上進行 large-scale model training。
- 在 low-memory router targets 上部署 full vector database。
- 取代既有 OpenWrt management systems，例如 LuCI、netifd、procd 或 rpcd。

## 3. Target Use Cases

### 3.1 Device Diagnostics

Agent 可以檢查 system status 並解釋 device problems。

Examples:

- 檢查 CPU、memory、flash、swap 與 process status。
- 透過 `ubus` 檢查 network interface state。
- 診斷 Wi-Fi instability。
- 檢查 WAN connectivity 與 DNS resolution。
- 分析 `logread`、`dmesg` 與 service-specific logs。

### 3.2 OpenWrt Configuration Assistance

Agent 可以檢查並協助 OpenWrt configuration。

Examples:

- 讀取 UCI configuration。
- 查詢 `ubus call network.interface dump`。
- 解釋目前 LAN/WAN/Wi-Fi topology。
- 建議安全的 configuration changes。
- 可選擇套用已核准的 configuration changes。

### 3.3 Multi-Step Troubleshooting

Agent 可以跨多個 tool calls 維持 context。

Example flow:

1. User reports slow network。
2. Agent checks interface status through `ubus`。
3. Agent checks CPU and memory pressure。
4. Agent checks Wi-Fi station statistics。
5. Agent checks recent logs。
6. Agent summarizes likely root causes。
7. Agent suggests next actions。

### 3.4 Local Memory and Historical Observation

Agent 可以記住被選定的 local facts 與 historical observations。

Examples:

- WAN interface 通常會取得 public IPv4 address。
- 安裝特定 package 後 device memory pressure 增加。
- Wi-Fi channel 上週曾被變更。
- 應保留 user-defined preferred DNS server。

### 3.5 Multi-Model Routing

Agent 可以將 requests route 到不同 model API servers。

Examples:

- 使用 small local model 做 command classification。
- 使用 remote stronger model 處理 complex reasoning。
- 使用 specialized model 做 log analysis。
- 當某個 API server 失敗時 fallback 到另一個 model。

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

Agent runtime 是中央 process，負責接收 user requests、維持 context、planning actions、invoking tools、calling model APIs，以及回傳 responses。

### Requirements

- 在 OpenWrt 上以 daemon 方式執行。
- 支援 CLI mode，供 development 與 debugging 使用。
- 透過 Unix domain socket 或 `ubus` integration 支援 local IPC。
- 支援 request cancellation 與 timeout handling。
- 對每個 request 與 tool call 產生 structured logs。
- 支援 low-memory operation。

### Suggested Implementation Options

- Language: C、C++、Rust、Go，或 prototype 使用 Python。
- OpenWrt-native long-term implementation 建議使用 C 搭配 `libubus`、`libuci`、`libubox` 與 `libjson-c`，這與平台高度一致。
- 可採 hybrid design：C daemon 負責 OpenWrt integration，高階 agent logic 透過 local model API 或 scriptable plugin layer 處理。

## 5.2 Request Context Manager

Request context manager 儲存 agent task 的 active state。

### Context Should Include

- Request ID。
- User message。
- System policy profile。
- Active model session。
- Tool call history。
- Shell command results。
- `ubus` query results。
- Memory retrieval results。
- Current reasoning summary。
- Final response draft。

### Requirements

- 跨 multi-step requests 維持 context。
- 限制 context size，避免 memory pressure。
- 支援 context compaction 與 summarization。
- 避免將 sensitive data 不必要地送到 remote models。
- 追蹤資料來自哪個 tool。

## 5.3 Shell Command Executor

Shell executor 允許 agent 執行 approved shell commands。

### Requirements

- 以 timeout 執行 shell commands。
- 擷取 stdout、stderr、exit code 與 execution time。
- 安全時支援 parallel 或 batched command execution。
- 支援 command allowlist 與 denylist。
- 預設採 read-only mode。
- 對 destructive commands 要求 explicit approval 或 elevated policy。
- Sanitize command arguments。
- 避免從 model output 建立 commands 時造成 shell injection。

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

這些 commands 應要求 explicit policy permission，最好也要求 user approval。

## 5.4 OpenWrt ubus Tool Adapter

ubus adapter 讓 agent 透過 structured APIs 查詢與呼叫 OpenWrt services，而不是只依賴 shell commands。

### Requirements

- 列出可用的 `ubus` objects。
- 檢查 object methods。
- 呼叫被允許的 `ubus` methods。
- Parse JSON responses。
- 執行 method-level policy。
- 當 structured data 可用時，優先使用 `ubus` 而非 shell。

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

Model router 管理與多個 AI model API servers 的通訊。

### Requirements

- 支援多個 model endpoints。
- 支援 OpenAI-compatible APIs。
- 支援 local model servers，例如 llama.cpp server、Ollama、vLLM 或 custom inference servers。
- 啟用時支援 remote providers。
- 支援依 task type 進行 routing policy。
- 支援 fallback models。
- 支援 API timeout、retry 與 circuit breaker behavior。
- 適用時支援 per-model token 與 cost accounting。

### Model Roles

| Role                 | Purpose                          | Example Model Type           |
|----------------------|----------------------------------|------------------------------|
| Classifier           | 決定 intent 與 tool safety level | Small local model            |
| Planner              | 建立 multi-step diagnostic plan  | Strong local or remote model |
| Tool Result Analyzer | 分析 command 與 ubus output      | Local or remote model        |
| Summarizer           | 壓縮 context 與 memory           | Small local model            |
| Final Responder      | 產生 user-facing response        | Strong model                 |

## 5.6 Local Memory Manager

Memory manager 儲存 persistent local knowledge。

### Requirements

- 儲存 device facts。
- 儲存被選定的 user preferences。
- 儲存 historical observations。
- 儲存 previous diagnostic summaries。
- 支援 expiration policy。
- 支援 manual deletion。
- 支援 sensitive data classification。
- 預設保持 memory local。

### Storage Options

| Storage                  | Use Case                        |
|--------------------------|---------------------------------|
| JSON files               | Simple prototype                |
| SQLite                   | Recommended baseline            |
| SQLite FTS5              | 對 logs 與 notes 做 text search |
| Lightweight vector index | Optional semantic retrieval     |
| RRD/time-series DB       | Historical metrics              |

### Memory Types

| Type               | Description                      | Example                                    |
|--------------------|----------------------------------|--------------------------------------------|
| Device fact        | 穩定的 system information        | Board name, OpenWrt version                |
| User preference    | User-approved preference         | Preferred DNS server                       |
| Observation        | Historical event                 | WAN disconnected at 03:12                  |
| Diagnostic summary | Prior troubleshooting result     | High memory pressure after package install |
| Policy memory      | Approved operational constraints | Never restart Wi-Fi without confirmation   |

## 5.7 Policy Engine

Policy engine 控制 agent 被允許做什麼。

### Requirements

- 執行 command-level permissions。
- 執行 ubus method-level permissions。
- 分離 read-only 與 write-capable operations。
- 未經 approval 不允許 destructive actions。
- 支援 local-only mode。
- 支援 remote-model data redaction。
- 支援 audit logging。

### Suggested Policy Levels

| Level                  | Description                          |
|------------------------|--------------------------------------|
| Observe                | 只允許 read-only inspection          |
| Diagnose               | Read-only tools plus analysis        |
| Suggest                | 可以提出 changes，但不能套用          |
| Apply with approval    | User confirmation 後可以套用 changes |
| Autonomous maintenance | 可以套用預先定義的 safe actions      |

Default mode 應為 `Observe` 或 `Diagnose`。

## 5.8 Tool Orchestrator

Tool orchestrator 負責執行 planner 產生的 tool calls。

### Requirements

- Validate tool call schema。
- Execution 前檢查 policy。
- 安全時 concurrent execute independent read-only commands。
- Deduplicate repeated queries。
- Normalize tool results。
- 將 tool results 提供回 model router。
- 當 risk 超過 policy 時停止 execution。

## 5.9 Logging and Audit Trail

### Requirements

- Log every user request。
- Log every model request and response metadata。
- Log every tool invocation。
- Log command exit code、runtime、stdout size 與 stderr size。
- 從 logs redacts secrets。
- Audit logs 保持 local。
- 支援 log rotation。

### Sensitive Data to Redact

- Wi-Fi passwords。
- PPPoE credentials。
- API keys。
- Session tokens。
- Remote sharing disabled 時的 private IP topology。
- Privacy mode enabled 時的 MAC addresses。

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

LuCI 或 external UI 的 optional API。

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

- 絕不在未 validation 的情況下執行 raw model-generated shell commands。
- 優先使用 structured tool calls，而不是 free-form shell。
- 使用 allowlisted binaries 與 argument schemas。
- 套用 execution timeout。
- 限制 output size。
- 可行時在 restricted user 下執行。

### 9.2 ubus Safety

- 分離 read-only methods 與 write methods。
- 預設 block high-risk methods。
- 對 `file`、`uci` 與 service mutation methods 要求 explicit policy。

### 9.3 Model API Safety

- 將資料送到 remote models 前先 redact secrets。
- 允許 local-only mode。
- 記錄每個 request 使用的 model endpoint。
- 支援 per-provider data sharing policy。

### 9.4 Memory Safety

- 除非明確設定，否則不要儲存 secrets。
- 提供 memory inspection 與 deletion。
- 支援 temporary observations 的 TTL。
- 依 sensitivity level 標記 memory records。

## 10. Performance Requirements

### OpenWrt Embedded Target

- Minimal idle memory footprint。
- 避免在小型 routers 上執行大型 resident model runtime。
- Low-end devices 偏好 external 或 remote model server。
- 可行時使用 streaming。
- 限制 concurrent tool execution。
- 使用 bounded queues。

### Suggested Resource Profiles

| Device Class          | Agent Mode                                   |
|-----------------------|----------------------------------------------|
| 128 MB RAM router     | Remote model only, minimal memory            |
| 256-512 MB RAM router | Remote model plus SQLite memory              |
| 1 GB RAM CPE          | Local small classifier plus remote reasoning |
| Raspberry Pi / x86    | Local model server plus full memory features |

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

- 提供 `/etc/config/ai-agent` UCI config。
- 提供 `/etc/init.d/ai-agent` procd service script。
- Register optional `ubus` object。
- 支援 logread-compatible logging。
- 支援 optional LuCI integration。

## 12. Configuration Example

```uci
config agent 'agent'
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

- CLI-only prototype。
- Execute safe read-only shell commands。
- Call one OpenAI-compatible model endpoint。
- Maintain in-memory request context。
- Produce diagnostic summaries。

## Phase 1: OpenWrt Integration

- Add procd service。
- Add UCI configuration。
- Add `ubus` query support。
- Add SQLite memory。
- Add basic policy engine。

## Phase 2: Multi-Model Runtime

- Add multiple model endpoint support。
- Add model role routing。
- Add fallback and retry。
- Add context compaction。
- Add local-only mode。

## Phase 3: Safe Action Execution

- Add approval workflow。
- Add controlled UCI mutation。
- Add service restart with explicit confirmation。
- Add rollback planning。
- Add audit trail viewer。

## Phase 4: Productization

- Add LuCI UI。
- Add packaged OpenWrt feed。
- Add device profile presets。
- Add metrics and health dashboard。
- Add plugin SDK for tools。

## 14. Minimal Viable Product

MVP 應包含：

- CLI command: `owrt-agent ask`。
- Read-only shell executor。
- Read-only `ubus` adapter。
- 一個或多個 OpenAI-compatible model endpoints。
- Request context tracking。
- SQLite local memory。
- Basic policy engine。
- Structured audit logs。
- OpenWrt package and init script。

## 15. Engineering Risks

| Risk                | Description                                | Mitigation                                    |
|---------------------|--------------------------------------------|-----------------------------------------------|
| Shell command risk  | Model may propose unsafe commands          | Strict allowlist and policy engine            |
| Low memory          | OpenWrt devices may have limited RAM       | Use bounded context and optional remote model |
| Secret leakage      | Logs/config may contain credentials        | Redaction and local-only mode                 |
| Model hallucination | Agent may misinterpret command output      | Tool-grounded responses and source tracking   |
| Unstable APIs       | Different model servers vary in behavior   | OpenAI-compatible abstraction layer           |
| ubus mutation risk  | Incorrect ubus call may alter system state | Read-only default and approval gate           |

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

Reason: short、clear、OpenWrt-specific，且適合作為 CLI command、daemon name 與 package name。

## 17. Success Criteria

專案被視為成功的條件是 agent 可以：

- 接收 user diagnostic request。
- 建立 request context。
- 透過 shell 與 `ubus` 查詢 OpenWrt system state。
- 將 reasoning route 到已設定的 model API server。
- 基於 tool results 產生 grounded diagnostic answer。
- 儲存有用的 local memory。
- 預設避免 unsafe commands。
- 以 OpenWrt package-managed daemon 方式執行。

## 18. Example User Request

```text
My Wi-Fi clients are unstable. Please check the router status.
```

Expected behavior:

1. Query system info。
2. Query wireless status through `ubus`。
3. Query memory and CPU load。
4. Query recent logs。
5. Analyze likely causes。
6. Return findings and recommended next steps。
7. Store a diagnostic summary if memory is enabled。

## 19. Design Principle

Agent 不應被設計成在 router 上執行的 unrestricted chatbot。它應被設計為 policy-controlled、tool-grounded、auditable 的 OpenWrt operations assistant。

核心價值不只是 natural language interaction。核心價值是安全協調 OpenWrt system state、shell tools、`ubus`、local memory 與多個 AI model backends。
