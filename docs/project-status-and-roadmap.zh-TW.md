# 專案狀態與 Roadmap

Review date: 2026-05-10

這份文件是目前專案狀態的主要依據，用來說明哪些功能已經實作、哪些已經在
OpenWrt One 上驗證，以及 EdgePulse 接下來應該往哪裡發展。較早的規劃文件
仍然是有用的設計紀錄，但當文件內容與目前實作看起來不一致時，應先以這份
文件為準。

## 目前實作狀態

EdgePulse 已經不只是研究構想。專案目前已有可運作的 C runtime、OpenWrt feed
package、LuCI 整合，以及第一版 AI Agent surface。

已實作並在本地驗證：

- C telemetry daemon 與 `edgepulse-ctl` CLI。
- `/tmp/edgepulse/edgepulse.db` SQLite runtime database。
- Raw telemetry collection、time-window feature extraction 與 JSON export。
- OpenWrt feed package，repository 為
  `https://github.com/edgepulse-labs/edgepulse-openwrt-feed`。
- LuCI overview、metrics、features、settings 與 AI Agent interaction pages。
- 透過 UCI 與 LuCI 設定 AI Agent，包括 model provider、local-only mode、
  memory、shell、`ubus`、policy 與 timeout。
- OpenAI-compatible remote model client，包含 API key redaction、model
  priority、fallback 與 model inventory commands。
- Shared AI Agent conversations 存在 SQLite，並可由 CLI 與 LuCI helper 讀取。
- LuCI Diagnostic 結果已整理成人類容易閱讀的 diagnostic report，不再只有
  raw JSON。
- `edgepulse-ctl agent action` 支援 policy-gated OpenWrt operations。
- 本地 C MCP adapter 支援 `edgepulse-ctl agent mcp methods`、`call` 與
  local stdio `serve`。
- 第一批 MCP methods 支援 EdgePulse status、agent status、chat、action、
  audit、read-only network/wireless `ubus` status，以及受限的 EdgePulse UCI
  read。

已在 `ssh one` / OpenWrt One 驗證：

- 已安裝相關 APK：`edgepulse`、`luci-app-edgepulse`、`farmhash`、
  `tensorflow-lite` 與 `tensorflow-lite-test`。
- UCI 啟用後，`edgepulse-ctl agent status` 顯示 `chat_enabled=true` 與
  `mcp_enabled=true`。
- CLI chat request 會寫入 shared conversation history。
- LuCI helper `agent-chat-list` 可讀取同一份 conversation history。
- MCP stdio `tools/list` 與 `tools/call edgepulse.agent.chat.list` 可運作，
  且會保留 JSON-RPC request ID。

## 目前邊界

- 預設安全姿態仍然是 local、policy-gated、read-only。
- State-changing actions 必須具備 `policy_profile=operator_confirmed` 與明確
  confirmation path。
- MCP 目前是本地 C adapter，不是 network service。
- MCP 對 `ubus` 與 UCI 的開放是 method-specific；不開放 arbitrary
  `ubus.call`、`uci.set`、shell execution、package management 或 firewall
  mutation。
- `/tmp/edgepulse/edgepulse.db` 是 volatile runtime state。跨 reboot 的
  persistent history 仍屬於未來選項。
- Rust `openwrt-mcp-server` 目前先排除在實作路徑外；等 local C method
  surface 穩定後，它可以成為外部 bridge。

## 近期 Roadmap

下一輪實作應聚焦在讓現有功能更可靠、更容易操作。

1. 加入 LuCI controls，呼叫常用的 `agent action` operations。
2. 加入小型中文/英文 intent classifier，把常見請求對應到 `status`、
   `wifi-status`、`logs-recent`、`reconnect-wan` 與 `wifi-set`。
3. 強化 Wi-Fi key 與其他敏感 action arguments 在 action output、syslog、
   audit records 與 LuCI 中的 redaction。
4. 加入 post-action verification，例如 WAN IP、DNS reachability、wireless
   radio status 與 associated clients。
5. 加入 `ubus`、`uci`、`logread`、`ifdown`、`ifup`、`wifi` 的 fixture
   integration tests。
6. 加入 MCP methods 的 UCI method-level ACLs。
7. 在 LuCI settings 中加入 local MCP enablement 與 exposed method review。
8. 增加 local `ubus` object 或 Unix domain socket，支援長時間 agent/MCP
   calls，讓未來 client 不必只透過 one-shot CLI shell out。

## 中期 Roadmap

- Remote training-data upload，具備 resumable delivery 與清楚的 LuCI/UCI
  controls。
- 穩定的 feature normalization，支援 multi-interface 與 multi-thermal-zone
  devices。
- 可持久化或可匯出的 agent memory，供需要跨 reboot history 的部署使用。
- 更完整的 model compatibility records，涵蓋 OpenAI-compatible providers、
  no-think behavior、timeouts、token budgets 與 failover。
- 更完整的 LuCI chat UX，包括 conversation selection 與 operation shortcuts。
- Optional `openwrt-mcp-server` bridge，透過 EdgePulse local MCP/CLI/ubus
  methods 操作，而不是重複實作 OpenWrt command logic。

## 長期方向

EdgePulse 應維持為 router-local、auditable 的 operations assistant，而不是
unrestricted chatbot。C runtime 應持續作為 OpenWrt state 的 policy authority。
外部 tools、model providers 與未來 Rust bridge components 都應透過穩定的
EdgePulse methods 整合，不能繞過 local policy 或 audit logging。
