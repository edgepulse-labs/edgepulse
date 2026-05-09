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

下一段實作重點是強化 MVP：加入更完整的 OpenWrt-specific collectors、retention cleanup，以及給 LuCI 使用的更窄 RPC interface。

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

Status: mostly complete

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
- [ ] Package API 穩定後，將 feed copy 移到獨立的 `edgepulse-openwrt-feed` repository。
- [ ] 加入 source archive 與 OpenWrt package `PKG_RELEASE` 更新的 release/version workflow。

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

Status: partial

加入 OpenWrt integration：

- [ ] `ubus` system board information。
- [ ] `ubus` network interface status。
- [ ] 透過可用的 `ubus`/`iwinfo` 取得 wireless status。
- [x] 從 `/proc/sys/net/netfilter/nf_conntrack_count` 取得 conntrack count。
- [ ] nftables/counter support 作為後續 optional work。

Todo:

- [ ] 加入圍繞 `libubus` 的小型 OpenWrt integration layer。
- [x] 將基本 device metadata 儲存在 device metadata table。
- [ ] 將 physical interface counters 對應到 OpenWrt logical interfaces。
- [x] 將缺少 conntrack source 視為 unavailable，而不是 fatal。
- [ ] 將缺少 wireless source 視為 unavailable，而不是 fatal。

Exit criteria:

- OpenWrt One board metadata 被擷取。
- WAN/LAN counters 可與 logical interfaces 關聯。
- 可用時記錄 Wi-Fi radio/interface metrics。

## Phase 4: Feature Windows

Status: mostly complete

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
- [ ] 等 data model 穩定後，以更窄的 RPC endpoint 取代 direct command execution。
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
- [ ] 加入 missing metric representation 的測試。

Exit criteria:

- CSV export 有穩定的 column names。
- Export 包含 device metadata 與 feature timestamps。
- Missing metrics 以一致方式表示。

## MVP Definition

第一個 MVP 完成時，OpenWrt One 應能：

- [x] 以輕量 daemon 方式執行 `edgepulse`。
- [x] 將 volatile telemetry 儲存在 `/tmp/edgepulse/edgepulse.db`。
- [x] 產生 time-window features。
- [x] 在 LuCI 顯示最新 metrics 與 settings。
- [x] 匯出 feature rows，供 external model training 使用。
