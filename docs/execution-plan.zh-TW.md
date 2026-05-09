# 執行計畫

Review 日期：2026-05-09

## 目前狀態

專案已在 OpenWrt One 上完成第一條 MVP 路徑：

- [x] daemon 與 CLI 共用的 C library。
- [x] `edgepulse` daemon command，可週期性輸出 JSON status。
- [x] `edgepulse-ctl` 最小 CLI，包含 `status`、`latest`、`features`、`export` 與 `version` commands。
- [x] 針對目前共用 telemetry helpers 的 unit test program。
- [x] `edgepulse` 與 `luci-app-edgepulse` 的 OpenWrt feed package skeleton。
- [x] 本地 OpenWrt buildroot 驗證 `.apk` package 輸出。
- [x] 在 OpenWrt One 上用 `apk add --allow-untrusted` 完成安裝驗證。
- [x] SQLite-backed raw sample storage，資料寫入 `/tmp/edgepulse/edgepulse.db`。
- [x] Stored feature windows，包含 mean、min、max、standard deviation、delta、rate 與 coefficient of variation。
- [x] 以 stored feature rows 為基礎的 training rows CSV export。
- [x] LuCI overview、metrics、features 與 settings pages 已完成 package 與安裝驗證。

下一段實作重點是加入遠端訓練資料上傳、canonical feature normalization、較長時間的可靠性驗證、讓 collector toggles 實際控制採樣行為，並加入一個可選的 AI agent runtime，讓它能從 OpenWrt/LuCI 啟用、設定與操作。

## 目標

為 OpenWrt One 建立最小可用的 EdgePulse 實作：

- 一個採樣本地 telemetry 的 C daemon package。
- 儲存在 `/tmp` 下的 SQLite database。
- 週期性的 feature extraction，供 AI training data 使用。
- 一個顯示 runtime metrics 並設定 sampling 的 LuCI application。

## Phase 0: Documentation Baseline

Status: complete

Todo:

- [x] 建立 `docs/README.md`。
- [x] 建立 `docs/readme-review.md`。
- [x] 建立 `docs/execution-plan.md`。
- [x] 建立 `docs/openwrt-one-telemetry-mvp.md`。
- [x] 為專案文件建立繁體中文翻譯。
- [x] 記錄本地 OpenWrt package 驗證流程。
- [x] 記錄 unit-test plan。

Exit criteria:

- 專案有清楚的 MVP 邊界。
- OpenWrt One 假設已附來源連結並記錄。
- Metric、SQLite 與 LuCI plan 已明確到可以轉成 tickets。

## Phase 1: Package Skeleton

Status: complete for MVP

建立 OpenWrt feed repository 與 package structure：

```text
edgepulse-openwrt-feed/
  edgepulse/
    Makefile
    files/etc/config/edgepulse
    files/etc/init.d/edgepulse
  luci-app-edgepulse/
    Makefile
    root/usr/share/luci/menu.d/luci-app-edgepulse.json
    root/usr/share/rpcd/acl.d/luci-app-edgepulse.json
```

Todo:

- [x] 加入 `packaging/openwrt-feed/edgepulse/Makefile`。
- [x] 加入 `packaging/openwrt-feed/edgepulse/files/etc/config/edgepulse`。
- [x] 加入 `packaging/openwrt-feed/edgepulse/files/etc/init.d/edgepulse`。
- [x] 加入 `packaging/openwrt-feed/luci-app-edgepulse/Makefile`。
- [x] 加入 LuCI menu metadata。
- [x] 加入 rpcd ACL metadata。
- [x] 將 `edgepulse` 與 `edgepulse-ctl` 都安裝進 OpenWrt package。
- [x] 在本地 OpenWrt buildroot 編出 `edgepulse-1.apk`。
- [x] 在本地 OpenWrt buildroot 編出 `luci-app-edgepulse-1.apk`。
- [x] 在 OpenWrt One 上安裝並驗證兩個 packages。
- [x] 將 feed copy 同步到獨立的 `edgepulse-openwrt-feed` repository，供本地 OpenWrt build 使用。
- [x] 加入 source archive 與 OpenWrt package `PKG_RELEASE` 更新的 release/version workflow。

Initial dependencies:

- `libsqlite3`
- `libubus`
- `libubox`
- `libblobmsg-json`

Exit criteria:

- OpenWrt 可以透過 `feeds.conf` consume 這個 feed。
- Package 可在 `mediatek/filogic` 的 OpenWrt SDK cross-compile。
- Daemon 可透過 `/etc/init.d/edgepulse` 啟動。
- UCI config 可啟用或停用 daemon。

Reference:

- [OpenWrt feeds 與 repos](openwrt-feeds-and-repos.zh-TW.md)

## Phase 2: Minimal Raw Sampling

Status: complete for MVP

先實作低風險的 file-based collectors：

- [x] CPU: `/proc/stat`
- [x] Memory: `/proc/meminfo`
- [x] Load: `/proc/loadavg`
- [x] Network interfaces: `/proc/net/dev`
- [x] Thermal: `/sys/class/thermal/thermal_zone*/temp`
- [x] Uptime: `/proc/uptime`

Todo:

- [x] 加入共用的 `edgepulse_collect_snapshot()` helper。
- [x] 輸出 current JSON status snapshot。
- [x] 讓 daemon output 放在 `/tmp/edgepulse`。
- [x] 在 `/tmp/edgepulse/edgepulse.db` 初始化 SQLite schema。
- [x] 將 raw samples 寫入 SQLite，而不只是 `edgepulse.json`。
- [x] 透過 init script 從 UCI 讀取 daemon interval。
- [x] 記錄 per-collector status，讓單一 collector 失敗不會導致整次 sample 失敗。
- [x] 加入 fixture-based tests，測試 `/proc` 與 `/sys` file parsing。

Exit criteria:

- Samples 寫入 `/tmp/edgepulse/edgepulse.db`。
- Sampling interval 由 UCI 控制。
- Collector failures 以 status 儲存，不造成 daemon fatal crash。

## Phase 3: OpenWrt-Specific Collectors

Status: complete for MVP

加入 OpenWrt integration：

- [x] `ubus` system board information。
- [x] `ubus` network interface status。
- [x] 透過可用的 `/proc/net/wireless` 取得 wireless status。
- [x] 從 `/proc/sys/net/netfilter/nf_conntrack_count` 取得 conntrack count。
- [x] nftables/counter support 作為後續 optional work。

Todo:

- [x] 加入圍繞 `libubus` 的小型 OpenWrt integration layer。
- [x] 將基本 device metadata 儲存在 device metadata table。
- [x] 將 physical interface counters 對應到 OpenWrt logical interfaces。
- [x] 將缺少 conntrack source 視為 unavailable，而不是 fatal。
- [x] 將缺少 wireless source 視為 unavailable，而不是 fatal。

Exit criteria:

- OpenWrt One board metadata 被擷取。
- WAN/LAN counters 可與 logical interfaces 關聯。
- 可用時從 `/proc/net/wireless` 記錄 Wi-Fi interface metrics。

## Phase 4: Feature Windows

Status: complete for MVP

從 raw samples 計算週期性 features：

- [x] mean
- [x] min
- [x] max
- [x] standard deviation
- [x] delta
- [x] rate per second
- [x] coefficient of variation

Initial windows:

- [x] 60 seconds
- [x] 5 minutes
- [x] 15 minutes

Todo:

- [x] 定義 feature table schema。
- [x] 加入針對 SQLite raw samples 的 feature-window computation。
- [x] 實作 `edgepulse-ctl features --json --window 60`。
- [x] 加入 feature calculation 的 unit tests。

Exit criteria:

- Features 與 raw samples 分開儲存。
- Feature rows 包含 metric name、window size、start time、end time 與 value。
- Export query 可以產生 training rows。

## Phase 5: LuCI Application

Status: complete for MVP

建立 LuCI app：

```text
luci-app-edgepulse/
  htdocs/luci-static/resources/view/edgepulse/
    overview.js
    metrics.js
    features.js
    settings.js
  root/usr/share/luci/menu.d/luci-app-edgepulse.json
  root/usr/share/rpcd/acl.d/luci-app-edgepulse.json
```

Views:

- [x] Overview：初版 health snapshot、load、memory 與 uptime。
- [x] Overview：latest CPU、thermal、network 與 collector status。
- [x] Metrics：latest raw metrics table。
- [x] Features：為 training data 準備的 derived windows。
- [x] Settings：UCI-backed sampling interval、retention、enabled collectors 與 database path。

Todo:

- [x] 加入 LuCI overview route。
- [x] 加入允許 LuCI 執行 `edgepulse-ctl` 的 rpcd ACL。
- [x] 將 overview page 接到 `edgepulse-ctl status --json`。
- [x] 加入 `metrics.js`。
- [x] 加入 `features.js`。
- [x] 加入 `settings.js`。
- [x] 等 data model 穩定後，以更窄的 RPC endpoint 取代 direct command execution。
- [x] 在 OpenWrt One 上用瀏覽器驗證 LuCI page rendering。

Exit criteria:

- LuCI 可透過小型 RPC endpoint 或 JSON export command 讀取 `/tmp/edgepulse/edgepulse.db`。
- Settings 透過 UCI 持久化。
- UI 可在 desktop 與 mobile LuCI layouts 上運作。

## Phase 6: Training Data Export

Status: complete for MVP

加入本地 export command：

```sh
edgepulse-ctl export --format csv --window 60s --since 1h
```

Todo:

- [x] 加入 placeholder `edgepulse-ctl export` command。
- [x] 從 computed feature rows 實作 CSV export。
- [x] 加入 `--format`、`--window` 與 `--since` argument parsing。
- [x] 在 exported rows 中包含 device metadata。
- [x] 加入 stable CSV headers。
- [x] 加入 missing metric representation 的測試。

Exit criteria:

- CSV export 有穩定的 column names。
- Export 包含 device metadata 與 feature timestamps。
- Missing metrics 以一致方式表示。

## Phase 7: Remote Training Data Upload and Normalization

Status: planned

加入可選的遠端上傳路徑，將週期性收集到的 training feature rows 傳送到遠端資料蒐集伺服器。

Todo:

- [ ] 加入 upload UCI options：enabled flag、remote URL、token、interval、batch size、TLS verification 與 device ID mode。
- [ ] 在 LuCI settings 加入啟用/停用 upload 與設定 remote collector server 的 controls。
- [ ] 加入 `edgepulse-ctl export --format json` 或 `jsonl`，供 machine-to-machine upload payload 使用。
- [ ] 新增 `edgepulse-upload` helper 或 service，用有界 batch 傳送資料並儲存 acknowledged cursor。
- [ ] 加入 retry、backoff 與 offline-safe spool behavior，確保 upload 失敗不會阻塞本地採樣。
- [ ] 記錄 remote server request 與 acknowledgement schema。
- [ ] 加入 thermal zone type collection，讓多 thermal-zone 裝置能更可靠地 normalization。
- [ ] 定義 canonical feature schema，使用 logical network roles、top-N variable slots、aggregate thermal features 與 mask vectors。
- [ ] 在 canonical schema 穩定前，device-side export 維持 sparse 且保留 labels。

Exit criteria:

- Upload 預設停用，且可從 LuCI 啟用。
- Remote collector URL 與 token 可從 LuCI/UCI 設定。
- 網路或伺服器故障後，feature upload 能安全恢復。
- Training pipelines 可將可變 interface 與 thermal-zone rows 對應成固定 schema vectors。

Reference:

- [訓練資料上傳與標準化](training-data-upload-and-normalization.zh-TW.md)

## Phase 8: Optional AI Agent Runtime

Status: planned

為 EdgePulse 加入可選、受 policy 控制的 AI agent，讓 OpenWrt 裝置可以使用本地 telemetry、安全 shell commands、read-only `ubus` queries、local memory，以及一個或多個已設定的 model API servers 回答診斷問題。

Initial boundary:

- AI agent 預設停用，除非 package build 或 UCI config 明確啟用。
- 第一版實作應以 diagnostic 與 read-only 為主。
- Remote model 使用必須明確且可設定。
- Tool execution 必須受 policy gate 控制，並可被稽核。
- LuCI 必須提供互動頁面與設定頁面，才能視為 user-ready feature。

OpenWrt package 與 build configuration todo:

- [ ] 在 `packaging/openwrt-feed/edgepulse/Makefile` 加入 OpenWrt package build option，用來在 build time include 或 exclude AI agent support。
- [ ] 定義 AI agent defaults 的 package config symbols，例如 `EDGEPULSE_ENABLE_AI_AGENT`、default model provider、default remote base URL、default model name、default local-only mode 與 default policy profile。
- [ ] 預設不要把真正的 secrets bake 進 firmware images；build-time API key placeholder 僅供 development images 使用，正式使用時優先透過 runtime UCI 或 environment-based secret configuration。
- [ ] 決定第一版 package shape 是 optional `edgepulse-agent` subpackage，或是編進現有 `edgepulse` package 的 feature。
- [ ] 加入第一版 agent implementation 需要的 package dependencies，例如 TLS/HTTP client support、JSON handling、`libuci`、`libubus` 與 SQLite memory。
- [ ] 確保 image builders 可以為 low-memory targets 選擇不含 AI agent support 的 `edgepulse`。
- [ ] 記錄在 standalone `edgepulse-openwrt-feed` workflow 中啟用 agent 的 `.config` 範例。

UCI configuration todo:

- [ ] 擴充 `packaging/openwrt-feed/edgepulse/files/etc/config/edgepulse`，加入 `agent` section，包含 `enabled`、`local_only`、`memory_enabled`、`shell_enabled`、`ubus_enabled`、`policy_profile`、request timeout、tool timeout 與 max tool output size。
- [ ] 加入至少一個 remote OpenAI-compatible endpoint 的 model configuration sections，包含 `enabled`、`role`、`base_url`、`model`、`api_key`、`api_key_env`、timeout 與 retry settings。
- [ ] 加入 defaults，讓沒有 API key 或 local model endpoint 時，agent 能回報清楚的 "not configured" status。
- [ ] 在 status output、logs、CLI commands 與 LuCI 中支援 `api_key` redacted handling。
- [ ] 加入 UCI validation，檢查 URL format、model name presence、timeout ranges、memory toggle、shell toggle 與 read-only policy mode。
- [ ] 加入 migration-safe defaults，確保安裝新 package 不會覆蓋既有 telemetry settings 或 secret fields。

Agent runtime implementation todo:

- [ ] 加入 `edgepulse-agentd` daemon，或在現有 daemon 中加入 agent mode，並納入 procd lifecycle management。
- [ ] 加入 `edgepulse-agent` 或 `edgepulse-ctl agent` CLI，支援 `ask`、`diagnose`、`status`、`memory list`、`memory delete` 與 `policy show`。
- [ ] 實作 request context tracking，包含 request ID、user message、selected model、tool call history、compacted observation summary 與 final answer。
- [ ] 實作 read-only shell executor，具備 allowlist、structured argument schemas、timeout、output size limits、exit code capture 與 audit logging。
- [ ] 實作 read-only `ubus` adapter，支援 `system`、`network.interface`、`network.device`、`network.wireless`、`service`、`iwinfo` 與 selected status methods。
- [ ] 實作 OpenAI-compatible model client，支援 configurable base URL、model、API key source、timeout、retries 與 response/error normalization。
- [ ] 實作 model routing，支援 classifier、planner、analyzer、responder 與 fallback 等 roles。
- [ ] 加入 local SQLite memory tables，儲存 observations、user facts、diagnostic summaries、sensitivity level、TTL 與 source metadata。
- [ ] 加入 policy engine，預設封鎖 destructive shell commands、UCI mutation、package installation/removal、service restarts、firewall changes 與 file deletion。
- [ ] 為每次 request、model call、tool call、policy decision 與 memory write 加入 audit logs。
- [ ] 加入 redaction helpers，避免 secrets 被寫入 logs 或送到 remote models。

LuCI application todo:

- [ ] 在 `luci-app-edgepulse` 底下加入 AI Agent menu entry。
- [ ] 加入 interaction page，讓使用者可以提出 diagnostic question，並看到 answer、tool evidence、model used 與 policy decisions。
- [ ] 加入 diagnostic shortcut page 或 mode，支援 WAN down、DNS failure、Wi-Fi instability、high CPU、high memory 與 package/service health 等常見任務。
- [ ] 擴充 settings page，加入 AI agent enable/disable、local-only mode、model provider、remote base URL、model name、API key 或 API key environment variable、memory toggle、shell toggle、`ubus` toggle、policy profile 與 timeout settings。
- [ ] 在 LuCI 中 redacted API keys，並要求明確 replacement 才能變更。
- [ ] 加入 status panel，顯示 agent 是否啟用、model backend 是否已設定、last request status、memory database status 與 policy mode。
- [ ] 更新 rpcd ACLs，讓 LuCI 只能呼叫必要的 agent status、ask、diagnostic、memory 與 settings endpoints。

Testing and validation todo:

- [ ] 加入 policy allow/deny decisions 與 command argument validation 的 unit tests。
- [ ] 加入 model request construction、redaction、timeout handling、retry handling 與 fallback behavior 的 tests。
- [ ] 加入 agent 與 model sections 的 UCI parsing tests。
- [ ] 加入 shell 與 `ubus` tool output summarization 的 fixture tests。
- [ ] 驗證 AI agent disabled 與 enabled 兩種 package builds。
- [ ] 驗證 OpenWrt One 安裝後 AI agent 預設停用。
- [ ] 設定 UCI model parameters 後，驗證 configured remote model 可以回答 read-only diagnostic request。
- [ ] 驗證 LuCI interaction 與 settings pages 在 desktop 與 mobile layouts 上可用。

Exit criteria:

- AI agent support 可以在 OpenWrt package build time 被選取或省略。
- 安裝後可透過 runtime UCI 啟用或停用 agent。
- UCI/LuCI 可以設定 default remote model base URL、model name、API key source、local-only mode、memory behavior 與 tool policy。
- 若 agent 已安裝但沒有 model credentials，它會回報清楚的 configuration status，而不是 silent failure。
- 使用者可以從 CLI 與 LuCI 提出 diagnostic question。
- 第一版實作中，agent 只執行 read-only、policy-approved shell 與 `ubus` actions。
- 每次 model call 與 tool call 都會記錄 log，且 secrets 會被 redacted。

Reference:

- [OpenWrt AI Agent 專案需求計畫](openwrt_ai_agent_requirements_plan.zh-TW.md)

## MVP Definition

第一個 MVP 完成時，OpenWrt One 應能：

- [x] 以輕量 daemon 方式執行 `edgepulse`。
- [x] 將 volatile telemetry 儲存在 `/tmp/edgepulse/edgepulse.db`。
- [x] 產生 time-window features。
- [x] 在 LuCI 顯示最新 metrics 與 settings。
- [x] 匯出 feature rows，供 external model training 使用。

AI agent extension 進入 implementation-ready 狀態時應完成：

- [ ] Build-time 與 runtime feature toggles 已文件化。
- [ ] Default model 與 credential configuration paths 已定義。
- [ ] LuCI interaction 與 settings pages 已拆成具體任務。
- [ ] 第一版 read-only diagnostic policy 已實作並測試。
