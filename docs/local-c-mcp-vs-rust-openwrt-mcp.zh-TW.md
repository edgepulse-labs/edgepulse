# 本地 C MCP Adapter 與 Rust OpenWrt MCP Server

Review 日期：2026-05-10

這份文件比較 EdgePulse 未來兩條 MCP 路線：

- 內建在 EdgePulse C runtime 裡的 local C MCP adapter，
- 獨立的 Rust `openwrt-mcp-server` bridge。

## 建議

第一版 MCP surface 先用 C 做 local-only adapter，之後再讓 Rust server 成為 optional remote bridge。

Local C adapter 應負責 router-local policy、allowlists、UCI/ubus access、audit logs 與 conversation storage。Rust bridge 應負責 HTTP、MQTT、TLS、tokens、sessions 與 fleet integration 等 remote transports。

目前專案狀態：local C adapter 已實作，並已透過 `edgepulse-ctl agent mcp serve`
在 OpenWrt One 驗證。Rust bridge 不在目前實作 scope 內。

目前 placement 決策：local MCP adapter 保留在 `edgepulse-ctl agent mcp`，長時間
OpenWrt-local callers 走 `edgepulse.agent` ubus object。暫不拆出獨立
`edgepulse-mcpd` binary；等 method surface、streaming progress 與 remote bridge
需求明確後再重新評估。

## 為什麼先從 C 開始

C runtime 已經和 EdgePulse package 整合，而且已有：

- agent/model/policy settings 的 UCI parsing，
- OpenWrt-oriented command allowlists，
- local telemetry 與 SQLite state，
- AI Agent conversation storage，
- audit logging，
- OpenWrt package integration。

第一版 local MCP adapter 放在這裡，可以避免重複實作 safety logic。

## 第一版 C MCP 範圍

第一階段刻意維持 local 且小範圍：

```sh
edgepulse-ctl agent mcp methods
edgepulse-ctl agent mcp call <method> [args]
edgepulse-ctl agent mcp serve
```

由 UCI 控制：

```uci
config agent 'agent'
    option mcp_enabled '0'
    option mcp_allow_edgepulse_status '1'
    option mcp_allow_agent_status '1'
    option mcp_allow_chat_list '1'
    option mcp_allow_chat_ask '1'
    option mcp_allow_action_run '1'
    option mcp_allow_audit_list '1'
    option mcp_allow_ubus_status_network '1'
    option mcp_allow_ubus_status_wireless '1'
    option mcp_allow_uci_get_edgepulse '1'
```

第一版 methods：

| Method                           | Scope                                   |
|----------------------------------|-----------------------------------------|
| `edgepulse.status`               | 讀取 EdgePulse telemetry status         |
| `edgepulse.agent.status`         | 讀取 agent/model/policy status          |
| `edgepulse.agent.chat.list`      | 讀取 shared conversation messages       |
| `edgepulse.agent.chat.ask`       | 新增 user message 與 assistant response |
| `edgepulse.agent.action.run`     | 執行 policy-gated named actions         |
| `edgepulse.agent.audit.list`     | 讀取 audit records                      |
| `edgepulse.ubus.status.network`  | Read-only `ubus` network status         |
| `edgepulse.ubus.status.wireless` | Read-only `ubus` wireless status        |
| `edgepulse.uci.get.edgepulse`    | Read-only EdgePulse UCI config          |

不要 expose 任意：

- `shell.exec`，
- `ubus.call`，
- `uci.set`，
- package installation，
- service restarts，
- firewall mutation。

## Ubus 與 UCI Policy

`ubus` 適合進第一版 C MCP，只要維持 read-only 且 method-specific。

允許範例：

- `network.interface dump`
- `network.wireless status`
- `system board`
- `system info`

`uci` 只適合 limited interface。

允許範例：

- 讀取 `edgepulse` config，
- 透過 `edgepulse.agent.action.run` 執行 confirmed named actions，例如 `wifi-set`。

避免 arbitrary UCI mutation，因為 MCP clients 是 AI-facing tools，不應取得 raw router control plane。

## Rust Bridge 方向

Rust `openwrt-mcp-server` 可演進成 companion bridge：

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

Rust 較適合：

- remote HTTP/MQTT transports，
- TLS 與 token handling，
- async connections，
- JSON schema validation，
- fleet/device orchestration，
- higher-level MCP compatibility layers。

C 較適合：

- 小型 OpenWrt package footprint，
- 直接整合 UCI/ubus/procd，
- local policy enforcement，
- 靠近 action 的 audit records，
- low-level status collection。

## 未來可能的分歧

兩邊可以刻意分工：

- C adapter 維持 local-only、minimal、policy-authoritative。
- Rust bridge 成為 remote-capable、multi-transport、fleet-oriented。
- C expose stable EdgePulse methods；Rust 把 external MCP clients 翻譯成這些 methods。
- C 避免 TLS/session 複雜度；Rust 負責這些複雜度。
- C 隨 core package 發佈；Rust 對較大設備或 managed fleets 維持 optional。

核心規則：remote tools 不得繞過 EdgePulse local policy。即使 Rust 之後變得更完整，state-changing operations 仍必須走 EdgePulse named actions 與 audit logging。

## 執行計畫

- [x] 加入 `mcp_enabled` UCI parsing 與 status output。
- [x] 加入 local C MCP method listing。
- [x] 加入 read-only ubus 與 limited UCI read 的 local C MCP method calls。
- [x] 將 action execution 導到既有 policy-gated named actions。
- [x] 加入 local stdio JSON-RPC mode，支援 `initialize`、`tools/list` 與 `tools/call`。
- [x] 保留 numeric、string 與 null JSON-RPC request IDs。
- [x] 在 OpenWrt One 驗證 `tools/list` 與 `tools/call edgepulse.agent.chat.list`。
- [x] 加入 long-running local daemon mode，透過 Unix domain socket 或 ubus。
- [x] 加入 method-level ACL settings in UCI。
- [ ] 在 LuCI 加入 local MCP enable 與 exposed methods review controls。
- [ ] 讓 Rust `openwrt-mcp-server` 呼叫 local C MCP adapter，而不是重複 OpenWrt command logic。
- [ ] 驗證 Rust remote calls 與 local CLI calls 產生相同 audit records。
- [x] 決定第一版 local MCP adapter placement：維持在 `edgepulse-ctl agent mcp`，daemon 提供 `edgepulse.agent` ubus local API，不新增 `edgepulse-mcpd`。

## 長期發展方向

- C adapter 保持小型、local、policy-authoritative。
- Method names 與 JSON shapes 穩定後，再提供給 remote bridges。
- 擴大 method surface 前，先加入 method-level ACLs 與 LuCI review controls。
- CLI execution 不足以支援長時間 client 時，再使用 `ubus` 或 Unix domain socket。
- 只有在需要 remote transport、fleet identity、TLS/session handling 或更完整 MCP
  compatibility 時，再重新評估 Rust bridge。
