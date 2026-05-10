# AI Agent 對話與 MCP 整合

Review 日期：2026-05-10

這份文件定義 EdgePulse 如何讓 CLI 與 LuCI 共用 AI Agent 對話紀錄，以及目前
local C MCP adapter 如何與未來外部 bridge tools 分工。

## 簡短結論

- UCI 應負責設定 chat mode、model backends、memory 與 policy，但 UCI 本身不適合作為聊天介面。
- CLI 與 LuCI 都應使用同一個 EdgePulse agent runtime 和同一組 SQLite conversation tables。
- 目前 daemon 應逐步成為 OpenWrt operations、policy checks、audit logs 與 conversation persistence 的本地權威。
- 目前第一版 MCP surface 是 `edgepulse-ctl agent mcp` 裡的 local C adapter；它支援 method listing、direct method calls 與 local stdio JSON-RPC。
- `openwrt-mcp-server` 目前先排除在本階段實作之外。未來若重新納入，應作為獨立 bridge process 呼叫 EdgePulse local methods，而不是重複實作 router command logic。
- 不建議把 remote Rust MCP server 直接併進 C daemon。邊界應保持清楚：EdgePulse 負責 router-local policy 與 state；bridge 負責 remote transport。

## 目前狀態

已實作：

- CLI shared chat：`edgepulse-ctl agent chat ask <conversation_id> <message>` 與 `edgepulse-ctl agent chat list [conversation_id]`。
- LuCI helper shared chat：`/usr/libexec/edgepulse-luci agent-chat-ask` 與 `agent-chat-list`。
- LuCI AI Agent page 會載入 shared transcript state，並將 Diagnostic output 轉成可讀的 diagnostic report。
- UCI defaults 已包含 `chat_enabled`、`default_conversation_id` 與 `mcp_enabled`。
- Local C MCP adapter 暴露 chat、status、action、audit、read-only `ubus` 與受限 EdgePulse UCI methods。
- OpenWrt One 驗證已確認 CLI、LuCI helper 與 MCP stdio 都能讀到同一份 conversation history。

## 共享 Conversation Model

Conversation state 放在 `/tmp/edgepulse/edgepulse.db`。

Tables：

- `agent_conversations`：conversation metadata、title、created time、updated time。
- `agent_messages`：每個 conversation 的 user/assistant ordered messages。
- `agent_requests`：request-level status 與 answer summary。
- `agent_audit_log`：policy、tool、model 與 action events。

同一份紀錄可從以下入口看到：

- CLI：`edgepulse-ctl agent chat list`、`edgepulse-ctl agent chat list <conversation_id>`、`edgepulse-ctl agent chat ask <conversation_id> "<message>"`
- LuCI：`/usr/libexec/edgepulse-luci agent-chat-list [conversation_id]` 與 `/usr/libexec/edgepulse-luci agent-chat-ask <conversation_id> <message>`
- Local C MCP：JSON-RPC methods 呼叫 EdgePulse local agent path，並回傳相同的 conversation IDs 與 messages。

## LuCI Chat Interface

`luci-app-edgepulse` 目前已使用與 CLI 相同的 backend conversation path。Diagnostic view 會產生結構化、可讀的報告，並 refresh shared conversation state。

目前與必要行為：

- 顯示 active conversation transcript。
- 透過 `agent-chat-ask` 將新 message 送進 selected conversation。
- 每次 request 後 refresh transcript，讓 CLI 產生的 messages 也能在 LuCI 看見。

未來 UI 工作：

- 顯示 recent conversations 並允許切換 conversation。
- 提供 operation shortcuts，例如 router status、Wi-Fi status、recent logs、reconnect WAN 與 Wi-Fi setup。
- Confirmed operations 必須先顯示確認步驟，再呼叫 confirmed action path。

UI 必須走與 CLI 相同的 backend path，避免產生兩套對話紀錄。

## UCI 的角色

UCI 應維持為 configuration，不應存 transcript。

建議 UCI fields：

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

不建議把 conversation 放進 UCI 的原因：

- UCI 不適合高頻改寫的 chat history。
- Chat logs 可能快速變大。
- `/tmp/edgepulse/edgepulse.db` 已經是 runtime state database，可被 CLI、LuCI、daemon 與 bridge tools 共用。

## Daemon 設計

Daemon 最終應提供 local IPC API，而不是讓所有 caller 都透過 one-shot CLI execution。

建議 local API：

- `agent.status`
- `agent.chat.ask`
- `agent.chat.list`
- `agent.chat.messages`
- `agent.action.run`
- `agent.policy.show`
- `agent.audit.list`

Transport options：

- 第一階段：CLI-backed wrapper，已適合 LuCI 與 MCP prototype。
- OpenWrt-native 階段：register local `ubus` object，例如 `edgepulse.agent`。
- Optional 階段：Unix domain socket，用於 streaming model/tool progress。

`ubus` 是最適合 OpenWrt 的本地 control plane，因為 LuCI、rpcd ACLs、shell tools 與其他 local services 都能使用。

## MCP 整合

目前 MCP 實作是 local C-based：

```sh
edgepulse-ctl agent mcp methods
edgepulse-ctl agent mcp call <method> [args]
edgepulse-ctl agent mcp serve
```

`edgepulse-ctl agent mcp serve` 透過 local stdio 接收 `initialize`、`tools/list` 與 `tools/call` JSON-RPC requests。

未來的 `openwrt-mcp-server` 應作為 bridge：

```text
External AI / MCP client
        |
        | HTTP / MQTT / JSON-RPC
        v
openwrt-mcp-server 或其他 external bridge
        |
        | local CLI, ubus, or Unix socket
        v
EdgePulse agent runtime
        |
        | policy, UCI, ubus, tools, SQLite
        v
OpenWrt system
```

這樣 security 與 ownership 會很清楚：

- EdgePulse 決定 action 是否被允許。
- EdgePulse 寫入 audit log。
- EdgePulse 擁有 conversation persistence。
- Bridge 負責驗證 remote clients，並把 JSON-RPC/MCP-style tool calls 轉成 EdgePulse local calls。

## Proposed MCP Methods

Local adapter 與未來 bridge 應 expose 小而穩定的方法集合：

| Method                          | EdgePulse mapping                                          |
|---------------------------------|------------------------------------------------------------|
| `edgepulse.status`              | `edgepulse-ctl status --json`                              |
| `edgepulse.agent.status`        | `edgepulse-ctl agent status`                               |
| `edgepulse.agent.chat.list`     | `edgepulse-ctl agent chat list [conversation_id]`          |
| `edgepulse.agent.chat.ask`      | `edgepulse-ctl agent chat ask <conversation_id> <message>` |
| `edgepulse.agent.action.run`    | `edgepulse-ctl agent action <action> [--confirm]`          |
| `edgepulse.agent.audit.list`    | `edgepulse-ctl agent audit list`                           |
| `edgepulse.ubus.status.network` | read-only `ubus call network.interface dump`               |
| `edgepulse.ubus.status.wireless` | read-only `ubus call network.wireless status`             |
| `edgepulse.uci.get.edgepulse`   | read-only `uci show edgepulse`                             |

State-changing methods 必須傳入 explicit confirmation，而 EdgePulse policy engine 仍必須強制檢查 `operator_confirmed`。

## Integration Plan

- [x] 在 EdgePulse SQLite schema 加入 shared conversation tables。
- [x] 加入由 shared conversation store 支援的 CLI chat commands。
- [x] 加入 LuCI wrapper commands：chat list 與 chat ask。
- [x] 將單次 output 的 LuCI diagnostic area 改成可讀 diagnostic report，並 refresh shared transcript。
- [x] 每次 message 後 LuCI refresh shared transcript。
- [x] 加入 UCI defaults：`chat_enabled`、`default_conversation_id`、`mcp_enabled`。
- [x] 加入 local C MCP stdio adapter，支援 `initialize`、`tools/list` 與 `tools/call`。
- [x] 在 OpenWrt One 驗證 CLI、LuCI helper 與 MCP 都能看到同一份 conversation history。
- [ ] 加入 EdgePulse `ubus` object，提供 local agent calls。
- [ ] 加入 LuCI conversation selection 與 operation shortcuts。
- [ ] 加入 mixed-origin CLI、LuCI、MCP conversation writes 的 end-to-end tests。
- [ ] 若 `openwrt-mcp-server` 重新納入 scope，更新其 executor，改為呼叫 EdgePulse local APIs，而不是 placeholder command execution。
- [ ] 將任何 Rust bridge 包成或文件化為 optional companion service，而不是 policy authority。

## 建議

採用 layered design：

1. EdgePulse C agent runtime 是 local source of truth。
2. LuCI 是同一個 agent API 與 SQLite transcript 上的一個 local UI。
3. Local C MCP adapter 是第一版 MCP surface。
4. 未來 Rust bridge 是外部 AI tools 的 companion transport，不是 OpenWrt policy 的 owner。

這樣可以避免重複 safety logic，也能防止未來 AI integrations 繞過 router-local policy 與 audit controls。
