# 執行計畫

Review 日期：2026-05-07

## 目標

為 OpenWrt One 建立最小可用的 EdgePulse 實作：

- 一個採樣本地 telemetry 的 C daemon package。
- 儲存在 `/tmp` 下的 SQLite database。
- 週期性的 feature extraction，供 AI training data 使用。
- 一個顯示 runtime metrics 並設定 sampling 的 LuCI application。

## Phase 0: Documentation Baseline

Status: started

Deliverables:

- `docs/README.md`
- `docs/readme-review.md`
- `docs/execution-plan.md`
- `docs/openwrt-one-telemetry-mvp.md`

Exit criteria:

- 專案有清楚的 MVP 邊界。
- OpenWrt One 假設已附來源連結並記錄。
- Metric、SQLite 與 LuCI plan 已明確到可以轉成 tickets。

## Phase 1: Package Skeleton

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

先實作低風險的 file-based collectors：

- CPU: `/proc/stat`
- Memory: `/proc/meminfo`
- Load: `/proc/loadavg`
- Network interfaces: `/proc/net/dev`
- Thermal: `/sys/class/thermal/thermal_zone*/temp`
- Uptime: `/proc/uptime`

Exit criteria:

- Samples 寫入 `/tmp/edgepulse/edgepulse.db`。
- Sampling interval 由 UCI 控制。
- Collector failures 以 status 儲存，不造成 daemon fatal crash。

## Phase 3: OpenWrt-Specific Collectors

加入 OpenWrt integration：

- `ubus` system board information。
- `ubus` network interface status。
- 透過可用的 `ubus`/`iwinfo` 取得 wireless status。
- 從 `/proc/sys/net/netfilter/nf_conntrack_count` 取得 conntrack count。
- nftables/counter support 作為後續 optional work。

Exit criteria:

- OpenWrt One board metadata 被擷取。
- WAN/LAN counters 可與 logical interfaces 關聯。
- 可用時記錄 Wi-Fi radio/interface metrics。

## Phase 4: Feature Windows

從 raw samples 計算週期性 features：

- mean
- min
- max
- standard deviation
- delta
- rate per second
- coefficient of variation

Initial windows:

- 60 seconds
- 5 minutes
- 15 minutes

Exit criteria:

- Features 與 raw samples 分開儲存。
- Feature rows 包含 metric name、window size、start time、end time 與 value。
- Export query 可以產生 training rows。

## Phase 5: LuCI Application

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

- Overview：health snapshot、latest CPU、memory、thermal、network 與 collector status。
- Metrics：latest raw metrics 與短時間序列圖表。
- Features：為 training data 準備的 derived windows。
- Settings：UCI-backed sampling interval、retention、enabled collectors 與 database path。

Exit criteria:

- LuCI 可透過小型 RPC endpoint 或 JSON export command 讀取 `/tmp/edgepulse/edgepulse.db`。
- Settings 透過 UCI 持久化。
- UI 可在 desktop 與 mobile LuCI layouts 上運作。

## Phase 6: Training Data Export

加入本地 export command：

```sh
edgepulse export --format csv --window 60s --since 1h
```

Exit criteria:

- CSV export 有穩定的 column names。
- Export 包含 device metadata 與 feature timestamps。
- Missing metrics 以一致方式表示。

## MVP Definition

第一個 MVP 完成時，OpenWrt One 應能：

- 以輕量 daemon 方式執行 `edgepulse`。
- 將 volatile telemetry 儲存在 `/tmp/edgepulse/edgepulse.db`。
- 產生 time-window features。
- 在 LuCI 顯示最新 metrics 與 settings。
- 匯出 feature rows，供 external model training 使用。
