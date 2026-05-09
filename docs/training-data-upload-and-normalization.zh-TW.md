# 訓練資料上傳與標準化

Review 日期：2026-05-09

這份文件定義 EdgePulse 將 feature data 送到遠端資料蒐集伺服器的規劃機制，以及模型訓練資料的標準化策略。

## 目前狀態

EdgePulse 目前支援本地端訓練資料準備：

- Raw samples 儲存在 `/tmp/edgepulse/edgepulse.db` 的 SQLite database。
- Feature windows 儲存在 `feature_rows`。
- `edgepulse-ctl export --format csv --window 60s --since 1h` 可匯出 feature rows。
- Feature rows 使用 long format：`device metadata + window + metric + labels + statistics`。
- 多網路介面與多 thermal zone 裝置會透過 labels 保留，例如 `iface=eth0,logical=wan` 或 `zone=0`。

EdgePulse 目前尚未將 feature rows 上傳到遠端伺服器，也尚未輸出固定寬度 tensor 或穩定的量化 feature vector。

## 遠端上傳機制

上傳路徑應實作成小型、獨立的 uploader，而不是把網路上傳邏輯直接塞進每個 collector。遠端伺服器故障時，collector daemon 仍應繼續本地採樣。

建議元件：

- `edgepulse-uploader`：小型 helper command 或 service，讀取本地 feature rows 並批次送到遠端 endpoint。
- Upload cursor state：在本地儲存最後被伺服器確認的 `feature_rows.id` 或 `(window_end, metric, labels)` cursor。
- Upload queue/spool：在 SQLite 或 `/tmp/edgepulse/upload-spool` 保留有界的 pending batches。
- Remote endpoint：透過 HTTPS `POST` 到可設定 URL。
- Payload format：預設 JSON Lines 或 compact JSON；CSV 保留給手動 export。
- Authentication：bearer token 或 device enrollment token，儲存在 UCI。
- Retry behavior：exponential backoff、有界 batch size，伺服器 ack 前不刪除資料。
- Privacy mode：允許 device identifier 被 hash，或替換成 enrollment 時發出的 opaque ID。

建議 request 形狀：

```json
{
  "protocol_version": 1,
  "device_id": "opaque-device-id",
  "sequence": 123,
  "sent_at": 1778299928,
  "features": [
    {
      "row_id": 1001,
      "window_sec": 60,
      "window_start": 1778299860,
      "window_end": 1778299920,
      "metric": "network.rx_bytes",
      "labels": "iface=eth0,logical=wan",
      "count": 12,
      "mean": 123456.0,
      "min": 120000.0,
      "max": 130000.0,
      "stddev": 1900.0,
      "delta": 10000.0,
      "rate_per_sec": 178.57,
      "coefficient_of_variation": 0.015
    }
  ],
  "metadata": {
    "board.model": "OpenWrt One",
    "board.release_distribution": "OpenWrt",
    "board.release_version": "SNAPSHOT"
  }
}
```

伺服器應回覆最高已接受 cursor：

```json
{
  "accepted": true,
  "last_row_id": 1001
}
```

## UCI 與 LuCI 設定

設定頁面應在既有 EdgePulse settings view 中暴露 upload controls。

規劃中的 UCI options：

```text
config edgepulse 'main'
  option upload_enabled '0'
  option upload_url 'https://collector.example.com/v1/edgepulse/features'
  option upload_token ''
  option upload_interval_sec '300'
  option upload_batch_rows '500'
  option upload_format 'json'
  option upload_tls_verify '1'
  option upload_device_id_mode 'opaque'
```

規劃中的 LuCI fields：

- Upload enabled：啟用/停用 toggle。
- Remote collector URL：HTTPS URL。
- Authentication token：password input。
- Upload interval：整數秒。
- Batch rows：有界整數。
- TLS verification：啟用/停用 toggle，預設啟用。
- Device ID mode：`opaque`、`hostname`，或可用時使用 `board-serial`。

預設必須停用。啟用上傳時必須設定 server URL，而且上傳失敗不能阻塞本地採樣。

## 標準化目標

訓練資料應分成兩個階段標準化：

- Device-local feature extraction：把 raw counters 轉成 time-window features。
- Training-time normalization：把 feature rows 轉成穩定的模型輸入。

本地裝置不應太早把最終模型專用的 normalization constants 寫死。它應保留足夠 metadata 與 labels，讓 collector server 或 training pipeline 建立穩定 schema。

## 目前表示法

目前 feature rows 是 sparse 且保留 label 的表示法：

```text
metric=network.rx_bytes
labels=iface=eth0,logical=wan
window_sec=60
mean=...
delta=...
rate_per_sec=...
```

這種表示法可以支援可變數量的 interface 與 thermal zone，因為每個被觀測到的來源都會成為一列。它適合儲存與 ingestion，但還不是固定的 model vector。

## 多網路介面裝置

不同裝置可能有不同網路介面名稱、數量與角色：

- `eth0`、`eth1`、`br-lan`、`pppoe-wan`、`wlan0`、`tailscale0`、`docker0`
- OpenWrt logical interfaces，例如 `wan`、`wan6`、`lan`、`guest`、`iot`

目前支援：

- EdgePulse 從 `/proc/net/dev` 記錄 physical interface counters。
- `ubus network.interface dump` 可用時，EdgePulse 會把 counter 標上 OpenWrt logical name，例如 `logical=wan`。
- Export 會以 long format 保留所有觀測到的 interfaces。

目前限制：

- EdgePulse 尚未把任意 interface set 對應成固定 feature vector。

建議 training schema：

- 優先使用 logical roles，而不是 physical names：`wan`、`lan`、`wifi`、`guest`、`vpn`、`loopback`、`other`。
- 可行時依 role 聚合：
  - `network.role.wan.rx_bps_sum`
  - `network.role.lan.tx_bps_sum`
  - `network.role.wifi.rx_bps_sum`
  - `network.role.other.rx_bps_sum`
- 對高基數 interfaces 保留 top-N slots：
  - `network.iface.top1.rx_bps`
  - `network.iface.top2.rx_bps`
  - `network.iface.top3.rx_bps`
- 加入 masks：
  - `network.iface.top1.present`
  - `network.role.wifi.present`

這能讓固定模型輸入涵蓋只有一個 WAN interface、有多個 LAN bridge，或額外 VPN/container interfaces 的設備。

## 多 Thermal Zone 裝置

裝置可能暴露一個或多個 thermal zones。Zone 編號不一定能跨硬體穩定對應。

目前支援：

- EdgePulse 會記錄 `thermal.temp_c`，並以 `zone=N` 標籤區分。
- Export 會保留每個觀測到的 zone row。

目前限制：

- EdgePulse 尚未讀取 `/sys/class/thermal/thermal_zone*/type`，因此 `zone=0` 在不同裝置之間沒有語意穩定性。
- EdgePulse 尚未產生固定 thermal aggregate features。

建議 training schema：

- 可用時記錄 zone type：
  - `zone=0,type=cpu-thermal`
  - `zone=1,type=wifi`
- 產生穩定 aggregate features：
  - `thermal.max_temp_c`
  - `thermal.mean_temp_c`
  - `thermal.top1_temp_c`
  - `thermal.top2_temp_c`
  - `thermal.hot_zone_count`
  - `thermal.max_rate_c_per_sec`
- 加入 masks：
  - `thermal.top1.present`
  - `thermal.top2.present`

對 OpenWrt One 而言，在 zone type 能穩定收集前，模型輸入應優先使用 aggregate thermal features。

## 量化與縮放

固定量化參數應綁定 canonical feature names，而不是 raw device labels。

適合的 canonical features：

- `memory.used_ratio.mean`
- `network.role.wan.rx_bps.rate`
- `network.role.lan.tx_bps.rate`
- `thermal.max_temp_c.max`
- `network.conntrack.used_ratio.mean`

較弱的 canonical features：

- `network.rx_bytes.iface=eth0.mean`
- `thermal.temp_c.zone=0.max`

建議流程：

1. Collector server ingest long-format rows。
2. 透過 schema registry 將 rows 對應到 canonical feature names。
3. 將可變 labels 聚合成 role-based 與 top-N features。
4. 輸出 fixed vector 加上 mask vector。
5. 從 training corpus 計算 normalization parameters：
   - ratios：clip 到 `[0, 1]`
   - temperatures：先 clip 到操作範圍，例如 `[-20, 120]` C，再 scale
   - byte counters：使用 delta/rate features，再套 `log1p`
   - counts：使用 `log1p`
   - jiffies/counters：使用 rates 或 ratios，不直接使用 raw monotonic values
6. 將 schema 與 normalization parameters 一起 version。

固定模型輸入範例：

```text
schema=edgepulse-openwrt-v1
features=[
  memory.used_ratio.mean,
  network.role.wan.rx_bps.log_rate,
  network.role.lan.tx_bps.log_rate,
  thermal.max_temp_c.scaled,
  network.conntrack_count.log_mean
]
masks=[
  memory.used_ratio.present,
  network.role.wan.present,
  network.role.lan.present,
  thermal.max_temp_c.present,
  network.conntrack_count.present
]
```

## 近期實作計畫

1. 加入 upload UCI options 與 LuCI settings fields。
2. 加入 `edgepulse-ctl export --format json` 或 `jsonl` mode。
3. 新增小型 `edgepulse-upload` command：
   - 讀取 UCI upload settings，
   - 匯出尚未傳送的 feature rows，
   - 將有界 batch POST 到遠端伺服器，
   - 儲存伺服器確認的 cursor。
4. 加入 procd service 或 timer loop 做週期性上傳。
5. 加入 server-side schema 文件，定義接受的 payload 與 ack。
6. 加入 thermal zone type collection。
7. 先在 server-side training pipeline 加入 role/top-N canonicalization。
8. Schema 穩定後，再加入 optional on-device canonical feature export。

## 對目前支援度的回答

EdgePulse 目前支援以 labeled feature rows 收集與匯出可變 interface 與 thermal-zone metrics。它尚未支援遠端上傳、固定寬度 tensor generation，或穩定的量化模型輸入。

建議設計是讓 device-side export 保持 sparse 並保留 labels，再由 collection server 標準化成 canonical role-based 與 top-N features，並附上 explicit masks。這樣同一套固定量化 schema 就能涵蓋 interface 與 thermal-zone 數量不同的設備。
