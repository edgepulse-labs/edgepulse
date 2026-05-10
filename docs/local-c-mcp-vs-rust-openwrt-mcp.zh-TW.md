# 本地 C MCP Adapter 與 Rust OpenWrt MCP Server

Review 日期：2026-05-10

這份文件比較 EdgePulse 未來兩條 MCP 路線：

- 內建在 EdgePulse C runtime 裡的 local C MCP adapter，
- 獨立的 Rust `openwrt-mcp-server` bridge。

## 建議

第一版 MCP surface 先用 C 做 local-only adapter，之後再讓 Rust server 成為 optional remote bridge。

Local C adapter 應負責 router-local policy、allowlists、UCI/ubus access、audit logs 與 conversation storage。Rust bridge 應負責 HTTP、MQTT、TLS、tokens、sessions 與 fleet integration 等 remote transports。

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
```

由 UCI 控制：

```uci
config agent 'agent'
    option mcp_enabled '0'
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
- [ ] 加入 long-running local daemon mode，透過 Unix domain socket 或 ubus。
- [ ] 加入 JSON-RPC request/response envelope compatibility。
- [ ] 加入 method-level ACL settings in UCI。
- [ ] 在 LuCI 加入 local MCP enable 與 exposed methods review controls。
- [ ] 讓 Rust `openwrt-mcp-server` 呼叫 local C MCP adapter，而不是重複 OpenWrt command logic。
- [ ] 驗證 Rust remote calls 與 local CLI calls 產生相同 audit records。
