# AI Agent OpenWrt Model 驗證 Use Cases

這份文件定義 OpenWrt target 上可重複執行的 AI Agent 驗證 use cases。目標是在不反覆修改 router runtime 的前提下，比較不同 provider/model 的相容性。

## 範圍

這些 use cases 是 EdgePulse AI Agent MVP 的 model benchmark。它們驗證 model 是否能：

- 針對 OpenWrt diagnostic 回傳有用的 assistant content。
- 避免在 command output、JSON 或 syslog 中洩漏 secret。
- 在 endpoint 無法連線或只回 reasoning content 時，有可預期的行為。
- 搭配 read-only tool policy 與 LuCI helper path 正常運作。

Target device、package path 與 model settings 應透過 `.env` 或已 export 的 shell variables 提供。參考 [測試環境變數](test-environment-variables.zh-TW.md)。

## 必要觀測資料

每個測試都應擷取：

- `edgepulse-ctl agent status`
- `edgepulse-ctl agent ask "<prompt>"`
- `edgepulse-ctl agent audit list`
- `logread -e edgepulse-agent`
- LuCI coverage：`/usr/libexec/edgepulse-luci agent-status` 與 `/usr/libexec/edgepulse-luci agent-diagnose`

Model response 檢查項目：

- `model_response.status`
- `model_response.http_status`
- `model_response.finish_reason`
- `model_response.reasoning_present`
- `answer`
- 回答是來自 model content，還是 local fallback behavior。

## Use Case 1：Local-Only Baseline

目的：確認 agent 不依賴 remote model 也能執行。

設定：

- `agent.enabled=1`
- `agent.local_only=1`
- 可保留任意已設定的 model。

Prompt：

```text
Summarize CPU, memory, uptime, and network health from the provided telemetry in one concise sentence.
```

預期結果：

- Agent 回傳 local telemetry 與 tool evidence。
- 不會發出 remote model request。
- Output 清楚標示 local-only model behavior。
- Syslog 包含 request/tool/policy summaries，且不含 secret。

## Use Case 2：Remote Model Diagnostic Summary

目的：驗證 model 能把 compact OpenWrt telemetry 轉成可用的 assistant content。

設定：

- `agent.enabled=1`
- `agent.local_only=0`
- 已設定 model endpoint、model name、timeout、token budget 與 API key。

Prompt：

```text
Summarize CPU, memory, uptime, and network health from the provided telemetry in one concise sentence.
```

預期結果：

- `model_response.status=ok`
- `finish_reason=stop`
- `answer` 包含 model 產生的內容。
- `reasoning_present` 可以是 true 或 false；只要有 assistant content 就可接受。
- Syslog 記錄 `no_think` 與 `max_tokens` 等 model settings。

目前 OpenWrt One 參考設定：

- `no_think=0`
- `max_tokens=2048`
- `timeout_sec=60`
- `retry_count=0`

## Use Case 3：No-Think Compatibility Matrix

目的：判斷 model endpoint 是否支援 no-think behavior。

用相同 diagnostic summary 分別測：

- `no_think=0`
- `no_think=1`

未來 adapter 若支援 provider-specific 欄位，也可加入類似 `enable_thinking=false` 的測試。

預期結果：

- 相容的 model 在要求 no-think 時，仍應回傳 assistant content。
- 如果 `no_think=1` 產生 `finish_reason=length`、`reasoning_present=true`，且 assistant content 為空，則標記為不相容目前的 no-think request style。
- 不要只因 `reasoning_present=true` 就判定失敗；只有 assistant content 缺失或不可用才算失敗。

OpenWrt One 發現：

- 實測的 Qwen OpenAI-compatible endpoint 沒有穩定遵守 `/no_think`。
- `no_think=1` 多次把 response budget 花在 `reasoning_content`。
- 此 endpoint 的實測可用設定為 `no_think=0`。

## Use Case 4：Token Budget 與 Timeout Boundary

目的：找出每個 model 最小且可靠的 token budget 與 timeout。

用小矩陣執行 diagnostic summary：

- `max_tokens=512`
- `max_tokens=1024`
- `max_tokens=2048`
- 如果 latency 仍可接受，再測 provider-specific 的更高值。

預期結果：

- 通過的設定會回傳 `finish_reason=stop` 與 assistant content。
- Reasoning model 即使只產生短回答，也可能需要較大的 token budget。
- Timeout 應足夠完成單次 model request，但不能長到讓 LuCI UI 看起來卡住。

OpenWrt One 發現：

- `1024` tokens 對 diagnostic summary 常出現 reasoning-only response。
- `2048` tokens 搭配 `60` 秒 timeout 可取得可用的 assistant content。

## Use Case 5：Endpoint Failure 與 Local Fallback

目的：驗證 model endpoint 不可用時 router 的行為。

設定：

- 將 `base_url` 指向不可連線的 local 或 reserved address。
- 保持 `agent.local_only=0`。

預期結果：

- `model_response.status` 為 non-OK，例如 `fetch_error`。
- Agent 仍回傳 local telemetry 與 tool evidence。
- Answer 清楚說明 model inference 失敗，並包含 local fallback data。
- JSON 與 syslog 都不印出 API key。

## Use Case 6：Secret Redaction

目的：確保 credentials 不會從 observability paths 洩漏。

設定：

- Mock server test 可配置容易辨識的 fake API key；real key 只應在可信硬體上使用。

預期結果：

- `edgepulse-ctl agent ask` 顯示 `"api_key": "redacted"`。
- `logread -e edgepulse-agent` 不包含 raw key。
- Audit records 只包含 event name 與 status，不包含 secret。

## Use Case 7：Read-Only Policy Guardrail

目的：確保 MVP 維持在 read-only action set 內。

Prompt：

```text
Check system health and do not change any settings.
```

預期結果：

- 只執行 read-only tools。
- Tool summaries 包含 snapshot、shell read commands 與允許的 ubus calls。
- Agent path 不暴露 destructive command。

## Use Case 8：LuCI Helper Parity

目的：驗證 web UI backend 使用與 CLI 相同的 agent path。

Commands：

```sh
/usr/libexec/edgepulse-luci agent-status
/usr/libexec/edgepulse-luci agent-diagnose
```

預期結果：

- LuCI status 包含與 `edgepulse-ctl agent status` 相同的 model configuration fields。
- LuCI diagnostic output 與 `edgepulse-ctl agent ask` 有相同的 model/fallback behavior。
- Settings page 可以修改 model timeout、retry count、max tokens 與 no-think mode。

## Use Case 9：Model Inventory 與 Priority Failover

目的：驗證使用者可以查詢 provider 支援的 model IDs，且 preferred model 無法使用時，inference 會切到下一個已設定 model。

Commands：

```sh
edgepulse-ctl agent models list
edgepulse-ctl agent models remote-list remote_reasoner
```

設定：

- 至少設定兩個 enabled `config model` sections。
- 第一個 section 使用較小的 numeric `priority`，並指向不可連線的 local endpoint。
- 下一個 priority 保留為已知可用的 model section。

預期結果：

- `models list` 依 priority 顯示所有已設定 model sections。
- `remote-list` 從 provider `/models` endpoint 回傳 model IDs，且 API key 維持 redacted。
- `agent ask` 先記錄高優先序 model 失敗，再選用下一個可用 model。
- Diagnostic JSON 包含 `model_failover.attempts` 與 `model_failover.selected_provider`。
- Syslog 記錄每次 model attempt 的 provider、model name、priority、status、finish reason 與 reasoning flag。

## Benchmark Record Template

每個 model/configuration 記錄一列：

| Date | Target | Provider | Model | no_think | max_tokens | timeout_sec | finish_reason | reasoning_present | Result | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 2026-05-10 | OpenWrt One | OpenAI-compatible | qwen/qwen3.6-35b-a3b | 0 | 2048 | 60 | stop | true | pass | 有可用 assistant content。 |

## 通過標準

一組 model configuration 若符合以下條件，即可視為 MVP 可用：

- Use Case 2 至少連續通過兩次。
- Use Case 5 會回傳清楚的 local fallback。
- Use Case 6 沒有 secret leakage。
- Use Case 8 確認 LuCI 可以使用相同 status 與 diagnostic path。

只有 Use Case 3 在有 assistant content 的情況下通過時，才把 no-think mode 視為支援。
