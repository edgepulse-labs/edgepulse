# 測試環境變數

EdgePulse 使用環境變數保存 device-specific 與 secret test configuration。這能讓 repo 保持可攜，同時讓 unit、integration 與 end-to-end validation 都能重複執行。

## 建議做法

你的方向是合適的，但需要保留一個邊界：

- 將 `.env-example` commit 到 repo，作為文件與 starter template。
- 真正的 `.env` 只放本機，並由 git 忽略。
- 不要把真實 API key、private hostname 或 lab-only path 寫進 tracked docs。
- 先從 shell 載入 `.env`，再執行測試，不要讓每個 Makefile target 都自動 parse `.env`。

這種 shell-loading pattern 比 Makefile parsing 更適合 secrets，因為 API key 可能包含對 `make` 有特殊意義的字元。

## 檔案

- `.env-example`：進版控的安全範例。
- `.env`：本機環境檔案，由 `.gitignore` 忽略。

建立本機檔案：

```sh
cp .env-example .env
```

接著依照目前機器與 OpenWrt target 修改 `.env`。

## 載入變數

`.env` 使用 POSIX shell syntax：

```sh
EDGEPULSE_OPENWRT_SSH_TARGET=one
EDGEPULSE_AI_MODEL=qwen/qwen3.6-35b-a3b
EDGEPULSE_AI_MAX_TOKENS=2048
```

執行 validation commands 前：

```sh
set -a
. ./.env
set +a
```

之後 tests 與 helper scripts 就可以從 process environment 讀取設定。

OpenWrt end-to-end helper 若發現 `.env` 存在，也會自動載入：

```sh
make openwrt-agent-e2e
```

若要強制 helper 只使用目前 process environment，可設定 `EDGEPULSE_SKIP_DOTENV=1`。

## 變數分類

### OpenWrt Target

| Variable | Purpose | Example |
| --- | --- | --- |
| `EDGEPULSE_OPENWRT_SSH_TARGET` | Router 的 SSH target 或 alias。 | `one` |
| `EDGEPULSE_OPENWRT_SSH_PORT` | Optional SSH port。 | `22` |
| `EDGEPULSE_OPENWRT_SSH_OPTS` | 給 local scripts 使用的 optional SSH flags。 | `-o ConnectTimeout=10` |

### Package Artifacts

| Variable | Purpose |
| --- | --- |
| `EDGEPULSE_OPENWRT_EDGE_PACKAGE` | 已建置的 `edgepulse` APK/IPK local path。 |
| `EDGEPULSE_OPENWRT_LUCI_PACKAGE` | 已建置的 `luci-app-edgepulse` APK/IPK local path。 |

### AI Agent Runtime

| Variable | Purpose | 目前 OpenWrt One 參考值 |
| --- | --- | --- |
| `EDGEPULSE_AI_AGENT_ENABLED` | 目標 `edgepulse.agent.enabled` 值。 | `1` |
| `EDGEPULSE_AI_AGENT_LOCAL_ONLY` | 目標 `edgepulse.agent.local_only` 值。 | `0` |
| `EDGEPULSE_AI_MODEL_SECTION` | UCI model section name。 | `remote_reasoner` |
| `EDGEPULSE_AI_BASE_URL` | OpenAI-compatible base URL。 | private lab value |
| `EDGEPULSE_AI_MODEL` | 送給 provider 的 model name。 | `qwen/qwen3.6-35b-a3b` |
| `EDGEPULSE_AI_API_KEY` | Provider API key。只放在 `.env`。 | 不進版控 |
| `EDGEPULSE_AI_API_KEY_ENV` | Runtime config 使用的 environment variable name。 | `EDGEPULSE_AI_API_KEY` |
| `EDGEPULSE_AI_TIMEOUT_SEC` | Model request timeout。 | `60` |
| `EDGEPULSE_AI_RETRY_COUNT` | Retry count。 | `0` |
| `EDGEPULSE_AI_MAX_TOKENS` | Model response token budget。 | `2048` |
| `EDGEPULSE_AI_NO_THINK` | 是否要求 no-think mode。 | `0` |

### Local Tests

| Variable | Purpose | Default |
| --- | --- | --- |
| `EDGEPULSE_AGENT_TEST_PORT` | `make integration-agent-model` 使用的 mock OpenAI server port。 | `18181` |

### End-to-End Prompts

| Variable | Purpose |
| --- | --- |
| `EDGEPULSE_AGENT_E2E_PROMPT` | OpenWrt end-to-end model validation 使用的 prompt。 |
| `EDGEPULSE_AGENT_E2E_EXPECT_FINISH_REASON` | 預期 `finish_reason`，通常是 `stop`。 |

## OpenWrt Validation Flow 範例

載入環境：

```sh
set -a
. ./.env
set +a
```

部署 packages：

```sh
scp "$EDGEPULSE_OPENWRT_EDGE_PACKAGE" "$EDGEPULSE_OPENWRT_SSH_TARGET:/tmp/edgepulse.apk"
scp "$EDGEPULSE_OPENWRT_LUCI_PACKAGE" "$EDGEPULSE_OPENWRT_SSH_TARGET:/tmp/luci-app-edgepulse.apk"
ssh "$EDGEPULSE_OPENWRT_SSH_TARGET" 'apk add --allow-untrusted /tmp/edgepulse.apk /tmp/luci-app-edgepulse.apk'
```

套用 model settings：

```sh
ssh "$EDGEPULSE_OPENWRT_SSH_TARGET" "
uci set edgepulse.agent.enabled='$EDGEPULSE_AI_AGENT_ENABLED'
uci set edgepulse.agent.local_only='$EDGEPULSE_AI_AGENT_LOCAL_ONLY'
uci set edgepulse.$EDGEPULSE_AI_MODEL_SECTION.enabled='1'
uci set edgepulse.$EDGEPULSE_AI_MODEL_SECTION.base_url='$EDGEPULSE_AI_BASE_URL'
uci set edgepulse.$EDGEPULSE_AI_MODEL_SECTION.model='$EDGEPULSE_AI_MODEL'
uci set edgepulse.$EDGEPULSE_AI_MODEL_SECTION.api_key='$EDGEPULSE_AI_API_KEY'
uci set edgepulse.$EDGEPULSE_AI_MODEL_SECTION.api_key_env='$EDGEPULSE_AI_API_KEY_ENV'
uci set edgepulse.$EDGEPULSE_AI_MODEL_SECTION.timeout_sec='$EDGEPULSE_AI_TIMEOUT_SEC'
uci set edgepulse.$EDGEPULSE_AI_MODEL_SECTION.retry_count='$EDGEPULSE_AI_RETRY_COUNT'
uci set edgepulse.$EDGEPULSE_AI_MODEL_SECTION.max_tokens='$EDGEPULSE_AI_MAX_TOKENS'
uci set edgepulse.$EDGEPULSE_AI_MODEL_SECTION.no_think='$EDGEPULSE_AI_NO_THINK'
uci commit edgepulse
/etc/init.d/edgepulse restart
"
```

執行 diagnostic：

```sh
ssh "$EDGEPULSE_OPENWRT_SSH_TARGET" "edgepulse-ctl agent ask '$EDGEPULSE_AGENT_E2E_PROMPT'"
```

或使用 repo helper：

```sh
make openwrt-agent-e2e
```

若希望 helper 在 diagnostic 前先套用環境變數裡的 model settings：

```sh
EDGEPULSE_E2E_APPLY_CONFIG=1 make openwrt-agent-e2e
```

## Unit、Integration 與 End-to-End 邊界

Unit tests 應避免依賴真實 OpenWrt device 與真實 provider。它們可以使用暫存 config files，也可以在有助於覆蓋 parsing 時使用 fake environment values。

Integration tests 預設應使用 local mock servers。`make integration-agent-model` 是目前範例；它會讀取 `EDGEPULSE_AGENT_TEST_PORT`。

End-to-end tests 可以使用 `EDGEPULSE_OPENWRT_SSH_TARGET` 與真實 model settings。這類測試應該 opt-in，因為它依賴硬體、網路與 secrets。

## 安全注意事項

- 永遠不要 commit `.env`。
- Local mock integration tests 優先使用 fake API keys。
- 將 command output 貼到 issue 或 docs 前，先確認已 redacted。
- 如果 model endpoint 在 `no_think` 下有不同表現，請記錄到 [AI Agent OpenWrt Model 驗證 Use Cases](ai-agent-openwrt-model-validation-use-cases.zh-TW.md)。
