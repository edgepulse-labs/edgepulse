# AI Agent 對話與 MCP 整合

Review 日期：2026-05-10

這份文件定義 EdgePulse 如何讓 CLI、LuCI 與外部 AI tools 共用 AI Agent 對話紀錄，以及如何與 `openwrt-mcp-server` 整合。

## 簡短結論

- UCI 應負責設定 chat mode、model backends、memory 與 policy，但 UCI 本身不適合作為聊天介面。
- CLI 與 LuCI 都應使用同一個 EdgePulse agent runtime 和同一組 SQLite conversation tables。
- 目前 daemon 應逐步成為 OpenWrt operations、policy checks、audit logs 與 conversation persistence 的本地權威。
- `../openwrt-mcp-server` 適合整合成獨立 bridge process，透過 HTTP/MQTT/JSON-RPC 把 EdgePulse agent capabilities 提供給外部 AI/MCP clients。
- 不建議把 Rust MCP server 直接併進 C daemon。邊界應保持清楚：EdgePulse 負責 router-local policy 與 state；MCP server 負責 external transport。

## 共享 Conversation Model

Conversation state 放在 `/tmp/edgepulse/edgepulse.db`。

Tables：

- `agent_conversations`：conversation metadata、title、created time、updated time。
- `agent_messages`：每個 conversation 的 user/assistant ordered messages。
- `agent_requests`：request-level status 與 answer summary。
- `agent_audit_log`：policy、tool、model 與 action events。

同一份紀錄應可從以下入口看到：

- CLI：`edgepulse-ctl agent chat list`、`edgepulse-ctl agent chat list <conversation_id>`、`edgepulse-ctl agent chat ask <conversation_id> "<message>"`
- LuCI：`/usr/libexec/edgepulse-luci agent-chat-list [conversation_id]` 與 `/usr/libexec/edgepulse-luci agent-chat-ask <conversation_id> <message>`
- MCP bridge：JSON-RPC methods 呼叫 EdgePulse CLI/IPC，並回傳相同的 conversation IDs 與 messages。

## LuCI Chat Interface

`luci-app-edgepulse` 應提供真正的 conversation UI，而不只是單次 diagnostic textarea。

必要行為：

- 顯示 recent conversations。
- 開啟 conversation 後顯示 user/assistant turns。
- 在目前 conversation 送出新 message。
- 提供 operation shortcuts，例如 router status、Wi-Fi status、recent logs、reconnect WAN 與 Wi-Fi setup。
- Confirmed operations 必須先顯示確認步驟，再呼叫 confirmed action path。
- 每次 request 後 refresh transcript，讓 CLI 產生的 messages 也能在 LuCI 看見。

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

- 第一階段：CLI-backed wrapper，已適合 LuCI 與 MCP bridge prototype。
- OpenWrt-native 階段：register local `ubus` object，例如 `edgepulse.agent`。
- Optional 階段：Unix domain socket，用於 streaming model/tool progress。

`ubus` 是最適合 OpenWrt 的本地 control plane，因為 LuCI、rpcd ACLs、shell tools 與其他 local services 都能使用。

## MCP 整合

`openwrt-mcp-server` 應作為 bridge：

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

這樣 security 與 ownership 會很清楚：

- EdgePulse 決定 action 是否被允許。
- EdgePulse 寫入 audit log。
- EdgePulse 擁有 conversation persistence。
- MCP server 負責驗證 remote clients，並把 JSON-RPC/MCP-style tool calls 轉成 EdgePulse local calls。

## Proposed MCP Methods

Bridge 應 expose 小而穩定的方法集合：

| Method                       | EdgePulse mapping                                          |
|------------------------------|------------------------------------------------------------|
| `edgepulse.status`           | `edgepulse-ctl status --json`                              |
| `edgepulse.agent.status`     | `edgepulse-ctl agent status`                               |
| `edgepulse.agent.chat.list`  | `edgepulse-ctl agent chat list [conversation_id]`          |
| `edgepulse.agent.chat.ask`   | `edgepulse-ctl agent chat ask <conversation_id> <message>` |
| `edgepulse.agent.action.run` | `edgepulse-ctl agent action <action> [--confirm]`          |
| `edgepulse.agent.audit.list` | `edgepulse-ctl agent audit list`                           |

State-changing methods 必須由 MCP bridge 傳入 explicit confirmation，而 EdgePulse policy engine 仍必須強制檢查 `operator_confirmed`。

## Integration Plan

- [x] 在 EdgePulse SQLite schema 加入 shared conversation tables。
- [x] 加入由 shared conversation store 支援的 CLI chat commands。
- [ ] 加入 LuCI wrapper commands：chat list 與 chat ask。
- [ ] 將單次 output 的 LuCI diagnostic area 改成 transcript view。
- [ ] 每次 message 後 LuCI refresh shared transcript。
- [ ] 加入 UCI defaults：`chat_enabled`、`default_conversation_id`、`mcp_enabled`。
- [ ] 加入 EdgePulse `ubus` object，提供 local agent calls。
- [ ] 更新 `openwrt-mcp-server` command executor，改為呼叫 EdgePulse local APIs，而不是 placeholder command execution。
- [ ] 將 `openwrt-mcp-server` 包成 optional OpenWrt feed package，或文件化為 external companion service。
- [ ] 加入 end-to-end tests，證明 CLI、LuCI 與 MCP 都能看到同一份 conversation history。

## 建議

採用 layered design：

1. EdgePulse C agent runtime 是 local source of truth。
2. LuCI 是同一個 agent API 與 SQLite transcript 上的一個 local UI。
3. `openwrt-mcp-server` 是外部 AI tools 的 companion bridge，不是 OpenWrt policy 的 owner。

這樣可以避免重複 safety logic，也能防止未來 AI integrations 繞過 router-local policy 與 audit controls。
