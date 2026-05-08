# OpenWrt One Telemetry MVP

Review 日期：2026-05-07

## Hardware Baseline

Initial target: OpenWrt One。

已記錄的硬體假設：

- Target/subtarget: `mediatek/filogic`
- Package architecture: `aarch64_cortex-a53`
- SoC: MediaTek MT7981BA / Filogic 820 class
- CPU: 2 cores, 1.3 GHz
- RAM: 1 GB DDR4
- Storage: 256 MiB NAND and recovery NOR
- Ethernet: 1 x 2.5 GbE WAN, 1 x 1 GbE LAN
- Wi-Fi: MediaTek MT7981BA plus MT7976CN class wireless hardware, mt76 driver
- USB: USB 2.0 and USB-C serial console
- Expansion: M.2 slot, mikroBUS socket

Sources:

- OpenWrt device page: https://openwrt.org/toh/openwrt/one
- OpenWrt Techdata page: https://openwrt.org/toh/hwdata/openwrt/openwrt_one_1

## Minimal Package Concept

Package name:

```text
edgepulse
```

OpenWrt feed package names:

```text
edgepulse
luci-app-edgepulse
```

Purpose:

- 在 OpenWrt One 上執行小型 C daemon。
- 以低 overhead 收集本地 telemetry。
- 將 samples 儲存在 `/tmp` 下的 SQLite。
- 週期性計算偏向 training 的 feature windows。
- 將本地資料 expose 給 LuCI。

Repository and feed planning:

- Core source 與 docs 保留在主要 `edgepulse` repository。
- OpenWrt package recipes 放在 `edgepulse-openwrt-feed`。
- 透過 `feeds.conf` 將 feed 加入 OpenWrt。
- 參考 [OpenWrt feeds 與 repos](openwrt-feeds-and-repos.zh-TW.md)。

第一版 non-goals：

- On-device AI inference。
- Cloud synchronization。
- Flash 上的 long-term persistent history。
- eBPF collectors。
- Full packet inspection。
- Automatic remediation。

## Runtime Paths

先使用 volatile storage：

```text
/tmp/edgepulse/
  edgepulse.db
  edgepulse.pid
  edgepulse.json
```

建議預設 database path：

```text
/tmp/edgepulse/edgepulse.db
```

Reasoning:

- `/tmp` 可避免頻繁 flash writes。
- 需要時可在 reboot 前 export training data。
- MVP 應先測量 runtime behavior，再設計 persistence。

Optional future persistent path:

```text
/overlay/edgepulse/
```

## UCI Configuration

File:

```text
/etc/config/edgepulse
```

Minimal config:

```text
config edgepulse 'main'
  option enabled '1'
  option db_path '/tmp/edgepulse/edgepulse.db'
  option sample_interval_sec '5'
  option feature_interval_sec '60'
  option retention_raw_sec '3600'
  option retention_feature_sec '86400'
  option enable_cpu '1'
  option enable_memory '1'
  option enable_network '1'
  option enable_thermal '1'
  option enable_wireless '1'
  option enable_conntrack '1'
```

## Metric Catalog

MVP 應收集所有便宜且廣泛可用的 practical local metrics。某些值會因 kernel config、已安裝 packages 與 drivers 而缺失。Missing metrics 應記錄為 unavailable，而不是視為 fatal errors。

### System Identity

Sources:

- `ubus call system board`
- `/etc/openwrt_release`
- `/proc/cpuinfo`

Metrics:

- board name
- model
- hostname
- OpenWrt release
- kernel version
- target/subtarget where available
- CPU model
- CPU core count

Sampling:

- Daemon start 時。
- LuCI manual refresh 時。

### CPU

Sources:

- `/proc/stat`
- `/proc/loadavg`
- `/sys/devices/system/cpu/cpu*/cpufreq/`

Metrics:

- total CPU usage percent
- per-core CPU usage percent
- user, nice, system, idle, iowait, irq, softirq deltas
- load average 1, 5, 15 minutes
- runnable process count
- current CPU frequency if exposed
- min/max CPU frequency if exposed

Feature candidates:

- cpu_usage_mean
- cpu_usage_max
- cpu_usage_stddev
- cpu_iowait_rate
- cpu_irq_rate
- load1_mean
- load15_slope

### Memory

Source:

- `/proc/meminfo`

Metrics:

- MemTotal
- MemFree
- MemAvailable
- Buffers
- Cached
- SwapTotal
- SwapFree
- Slab
- SReclaimable
- SUnreclaim

Feature candidates:

- memory_used_ratio
- memory_available_min
- memory_pressure_slope
- slab_growth_rate

### Thermal

Sources:

- `/sys/class/thermal/thermal_zone*/temp`
- `/sys/class/thermal/thermal_zone*/type`

Metrics:

- thermal zone type
- thermal zone temperature Celsius
- max temperature across zones
- temperature delta per zone

Feature candidates:

- temp_cpu_mean
- temp_cpu_max
- temp_max_all_zones
- temp_rise_rate
- temp_cv
- cpu_temp_correlation

### Network Interfaces

Sources:

- `/proc/net/dev`
- `ubus call network.interface dump`
- `/sys/class/net/*/statistics/`

Metrics:

- rx_bytes
- tx_bytes
- rx_packets
- tx_packets
- rx_errors
- tx_errors
- rx_dropped
- tx_dropped
- interface operstate
- interface speed if exposed
- logical OpenWrt interface mapping where available

Feature candidates:

- wan_rx_bps
- wan_tx_bps
- lan_rx_bps
- lan_tx_bps
- packet_rate
- error_rate
- drop_rate
- traffic_burst_ratio

### Wireless

Sources:

- `ubus` wireless status where available
- `iwinfo` if installed
- `iw` if installed
- `/sys/kernel/debug/ieee80211/` when mounted and readable

Metrics:

- radio enabled state
- channel
- band
- bandwidth
- tx power
- noise floor if available
- associated station count
- station signal
- station tx/rx bitrate
- station rx/tx bytes if available
- retry or failure counters if available

Feature candidates:

- wifi_station_count_mean
- wifi_signal_min
- wifi_signal_stddev
- wifi_noise_mean
- wifi_retry_rate
- wifi_airtime_pressure

### Conntrack And Firewall Pressure

Sources:

- `/proc/sys/net/netfilter/nf_conntrack_count`
- `/proc/sys/net/netfilter/nf_conntrack_max`
- `nft list counters` as optional future collector

Metrics:

- conntrack_count
- conntrack_max
- conntrack_used_ratio
- firewall counters where configured

Feature candidates:

- conntrack_used_ratio_mean
- conntrack_growth_rate
- conntrack_spike_score

### Storage And Filesystem

Sources:

- `statvfs()`
- `/proc/mounts`
- `/sys/block/*/stat`

Metrics:

- filesystem total bytes
- filesystem free bytes
- filesystem used ratio
- block read/write sectors where available
- block I/O time where available

Feature candidates:

- tmp_free_min
- overlay_free_min
- block_write_rate
- block_io_busy_ratio

### Kernel And Process Pressure

Sources:

- `/proc/interrupts`
- `/proc/softirqs`
- `/proc/vmstat`
- `/proc/net/softnet_stat`

Metrics:

- IRQ count by line
- softirq counters
- context switches
- forks
- page faults
- softnet drops/time_squeeze where available

Feature candidates:

- irq_rate_total
- softirq_net_rx_rate
- context_switch_rate
- softnet_drop_rate

## SQLite Plan

SQLite 在 `/tmp` 下應使用 WAL-disabled 或 conservative mode，讓 volatile storage 上的行為保持簡單。

Recommended pragmas:

```sql
PRAGMA journal_mode = MEMORY;
PRAGMA synchronous = NORMAL;
PRAGMA temp_store = MEMORY;
```

### Tables

```sql
CREATE TABLE device_info (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL,
  updated_at INTEGER NOT NULL
);

CREATE TABLE metric_sample (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  ts INTEGER NOT NULL,
  source TEXT NOT NULL,
  metric TEXT NOT NULL,
  labels_json TEXT NOT NULL DEFAULT '{}',
  value REAL,
  unit TEXT,
  status INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX idx_metric_sample_lookup
ON metric_sample(metric, ts);

CREATE TABLE feature_sample (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  window_start INTEGER NOT NULL,
  window_end INTEGER NOT NULL,
  window_sec INTEGER NOT NULL,
  feature TEXT NOT NULL,
  labels_json TEXT NOT NULL DEFAULT '{}',
  value REAL,
  unit TEXT,
  status INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX idx_feature_sample_lookup
ON feature_sample(feature, window_end);

CREATE TABLE collector_status (
  collector TEXT PRIMARY KEY,
  enabled INTEGER NOT NULL,
  last_run_ts INTEGER,
  last_status INTEGER NOT NULL DEFAULT 0,
  last_error TEXT
);
```

Status convention:

- `0`: ok
- `1`: unavailable
- `2`: parse error
- `3`: permission denied
- `4`: timeout

## Feature Extraction Plan

第一個 feature extractor 應每 `feature_interval_sec` 執行一次。

對 window 中的每個 metric series：

- count
- mean
- min
- max
- standard deviation
- first value
- last value
- delta
- per-second rate when the metric is monotonic
- coefficient of variation when mean is not zero

Training row idea:

```text
device_id,window_start,window_end,cpu_usage_mean,cpu_usage_max,mem_used_ratio,wan_rx_bps,wan_tx_bps,temp_max,conntrack_used_ratio,wifi_station_count
```

## LuCI Plan

LuCI package name:

```text
luci-app-edgepulse
```

### Views

Overview:

- latest sample time
- daemon status
- CPU usage
- memory usage
- max thermal zone temperature
- WAN/LAN throughput
- Wi-Fi station count
- conntrack usage
- collector health list

Metrics:

- latest raw metrics table
- CPU、memory、thermal、network 與 conntrack 的 compact charts
- filter by source and metric

Features:

- latest 60-second and 5-minute feature windows
- feature completeness score
- CSV export action

Settings:

- enable daemon
- sample interval
- feature interval
- raw retention
- feature retention
- database path
- collector toggles

### Data Access

先偏好使用小型 local command：

```sh
edgepulse-ctl status --json
edgepulse-ctl latest --json
edgepulse-ctl features --json --window 60
edgepulse-ctl export --format csv
```

LuCI 可透過 rpcd 呼叫此命令，ACL 只暴露 read/status/export 與 UCI settings actions。

## C Daemon Design

Process:

1. Load UCI config。
2. Create `/tmp/edgepulse`。
3. Open or initialize SQLite database。
4. Register collectors。
5. Run sampling loop。
6. Store raw metric samples。
7. Run feature extraction on interval。
8. Apply retention cleanup。
9. Handle `SIGTERM` cleanly。

Collector interface:

```c
typedef int (*edgepulse_collect_fn)(struct edgepulse_context *ctx);

struct edgepulse_collector {
    const char *name;
    int enabled;
    edgepulse_collect_fn collect;
};
```

Collectors 應保持獨立。失敗的 collector 必須更新 `collector_status`，並允許 daemon 繼續執行。

## Acceptance Criteria

第一版實作可接受的條件：

- `edgepulse` 可在 OpenWrt One 上啟動。
- SQLite database 建立在 `/tmp/edgepulse` 下。
- CPU、memory、thermal、network 與 conntrack samples 被記錄。
- 至少計算一個 feature window。
- LuCI 從本地資料顯示 live overview values。
- LuCI settings 可變更 sampling interval 與 collector toggles。
- CSV export command 回傳適合 external training 的 feature rows。
