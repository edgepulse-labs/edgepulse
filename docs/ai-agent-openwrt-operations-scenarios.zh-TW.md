# AI Agent OpenWrt 操作情境

Review 日期：2026-05-10

這份文件定義 EdgePulse AI Agent 作為 OpenWrt operations assistant 時，常見的使用者對話情境。Agent 應把使用者意圖轉成受 policy 控制的 OpenWrt action，只執行被允許的工具，並用 evidence 回報它做了什麼。

## 目前狀態

第一版 CLI implementation 已存在，並已在 OpenWrt One package 測試。Read-only
`status`、`wifi-status` 與 `logs-recent` 已實作。Confirmed `reconnect-wan`
與 `wifi-set` 已存在，但必須通過 `operator_confirmed` policy 與明確
`--confirm` path。LuCI AI Agent page 目前可執行 diagnostic/chat requests；
專用 operation buttons 與 confirmation UX 仍是後續工作。

## 運作模型

Agent 有兩種 action level：

- Read-only：狀態查詢、telemetry summary、`ubus` status query、有限範圍 recent log read。Agent 啟用後可直接執行。
- Confirmed operations：UCI 寫入、Wi-Fi reload、WAN 重新撥接，或任何可能中斷連線的動作。這些必須同時具備 `policy_profile=operator_confirmed` 與 CLI/LuCI 的明確 `--confirm` execution path。

每次 operation 都必須回傳：

- 使用者意圖與選到的 action，
- policy decision，
- tool calls 與 exit status，
- 相關 command output 或 structured `ubus` evidence，
- 使用者可讀的 answer，說明請求已完成、需要確認，或失敗。

## Intent Catalog

| 使用者意圖        | 使用者請求範例           | Agent action    | Tools                                                                             | 需要確認 |
|-------------------|--------------------------|-----------------|-----------------------------------------------------------------------------------|----------|
| Router status     | 「路由器現在狀態如何？」    | `status`        | `uptime`, `ubus network.interface dump`, `ubus network.wireless status`           | 否       |
| Wi-Fi status      | 「Wi-Fi 有開嗎？」          | `wifi-status`   | `ubus network.wireless status`                                                    | 否       |
| Connection status | 「查 WAN/LAN 連線狀況。」   | `status`        | `ubus network.interface dump` 與 telemetry                                        | 否       |
| Recent anomalies  | 「最近有沒有異常紀錄？」    | `logs-recent`   | `logread -l 80`                                                                   | 否       |
| Reconnect WAN     | 「幫我重新撥接網路。」      | `reconnect-wan` | `ifdown wan`, `ifup wan`, 後續 `ubus` status                                      | 是       |
| Configure Wi-Fi   | 「設定 Wi-Fi 名稱和密碼。」 | `wifi-set`      | `uci set wireless...`, `uci commit wireless`, `wifi reload`, 後續 wireless status | 是       |

## 對話情境

### 1. 查詢 Router Health

User：

```text
路由器現在狀態正常嗎？
```

預期行為：

- Agent 將 request 對應到 `status`。
- 收集 uptime、interface status、wireless status 與 EdgePulse telemetry。
- 用簡短摘要回答目前健康狀態，並附上 tool evidence。

CLI path：

```sh
edgepulse-ctl agent action status
```

### 2. 查詢 Wi-Fi Status

User：

```text
Wi-Fi 有開嗎？目前 SSID 和 radio 狀態如何？
```

預期行為：

- Agent 將 request 對應到 `wifi-status`。
- 呼叫 `ubus call network.wireless status`。
- 回報 radio/interface 狀態，以及 tool output 是否可用。

CLI path：

```sh
edgepulse-ctl agent action wifi-status
```

### 3. 重新撥接 WAN

User：

```text
網路好像斷了，幫我重新撥接 WAN。
```

預期行為：

- 第一個 response 應說明 reconnect WAN 會改變網路狀態，可能中斷連線。
- 除非 caller 使用 confirmed action path，否則不得執行。
- 確認後，agent 執行 `ifdown wan`、`ifup wan`，接著查詢 interface status 並回報結果。

CLI path：

```sh
edgepulse-ctl agent action reconnect-wan
edgepulse-ctl agent action reconnect-wan --confirm
```

### 4. 設定 Wi-Fi

User：

```text
把 Wi-Fi 名稱改成 EdgePulseLab，密碼設成一組新的 WPA2 密碼。
```

預期行為：

- Agent 抽取 SSID、encryption mode 與 key。
- 缺少欄位或值不安全時拒絕執行，並要求補齊。
- 由於會寫 UCI config 並 reload Wi-Fi，必須要求確認。
- 執行後回報每個 UCI/write/reload step，最後查詢 wireless status。

CLI path：

```sh
edgepulse-ctl agent action wifi-set --ssid EdgePulseLab --key '<new-password>'
edgepulse-ctl agent action wifi-set --ssid EdgePulseLab --key '<new-password>' --confirm
```

### 5. 查詢最近異常紀錄

User：

```text
最近有沒有異常紀錄？
```

預期行為：

- Agent 將 request 對應到 `logs-recent`。
- 使用 `logread -l 80` 讀取有限範圍的 recent log window。
- 摘要 errors、warnings、reconnects、service restarts、DNS failures、authentication failures；若沒有明顯問題，也要清楚回報。

CLI path：

```sh
edgepulse-ctl agent action logs-recent
```

## 實作計畫

- [x] 加入 `edgepulse-ctl agent action` CLI entrypoint，承接常用 OpenWrt operations。
- [x] 加入 router status、Wi-Fi status、recent logs 的 read-only actions。
- [x] 加入 WAN reconnect 與 Wi-Fi setting changes 的 confirmed action support。
- [x] State-changing actions 必須要求 `operator_confirmed` policy 與 `--confirm`。
- [x] 每個 action request 與 tool result 都寫入 SQLite audit。
- [x] 回傳包含 action status、request ID、tools 與 final answer 的 structured JSON。
- [ ] 加入 LuCI controls，將自然語言或 button-driven operations 導到同一條 `agent action` path。
- [ ] 加入小型 intent classifier，先把常見中文與英文請求對應到 action IDs，再 fallback 到 model。
- [ ] 加強 post-action verification，包含 WAN IP、DNS reachability、associated Wi-Fi clients、radio up/down state。
- [ ] 加入 fixture `ubus`、`uci`、`ifup`、`ifdown`、`wifi`、`logread` 的 integration tests。
- [ ] 加入 per-action permission switches，讓 deployment 可以允許 WAN reconnect，但不允許 Wi-Fi mutation。

## 未來擴充

- 加入 LuCI operation panel，提供 status、Wi-Fi status、logs、reconnect WAN
  與 Wi-Fi setup 的 button-driven flows。
- 使用 shared chat transcript 顯示選到哪個 action、執行哪些 tools，以及後續
  verification 結果。
- 加入 per-action UCI switches，讓 deployment 可分別允許 WAN reconnect、
  Wi-Fi reload、Wi-Fi mutation 與 log inspection。
- 加入 intent layer，對常見中文/英文 router requests 使用 deterministic
  mapping，只有在 intent 模糊時才 fallback 到 model。
- Mutation 後加入更完整 verification，例如 WAN address changes、DNS probes、
  gateway reachability、radio state 與 associated clients。

## Safety Rules

- 預設 policy 維持 read-only。
- State-changing operations 絕不能只靠未確認的 natural-language prompt 執行。
- 有 structured `ubus` status 時，agent 應優先使用它，而不是 free-form shell output。
- Agent 不得在 JSON、logs 或 audit records 中印出 Wi-Fi keys、API keys 或 tokens。
- Partial failure 必須以 partial/error 回報，並保留失敗 tool 的 status。
