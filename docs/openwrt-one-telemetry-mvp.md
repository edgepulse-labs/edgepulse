# OpenWrt One Telemetry MVP

Review date: 2026-05-07

## Hardware Baseline

Initial target: OpenWrt One.

Documented hardware assumptions:

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

Purpose:

- Run a small C daemon on OpenWrt One.
- Collect local telemetry with low overhead.
- Store samples in SQLite under `/tmp`.
- Periodically compute training-oriented feature windows.
- Expose local data to LuCI.

Non-goals for the first version:

- On-device AI inference.
- Cloud synchronization.
- Long-term persistent history on flash.
- eBPF collectors.
- Full packet inspection.
- Automatic remediation.

## Runtime Paths

Use volatile storage first:

```text
/tmp/edgepulse/
  edgepulse.db
  edgepulse.pid
  edgepulse.json
```

Recommended default database path:

```text
/tmp/edgepulse/edgepulse.db
```

Reasoning:

- `/tmp` avoids frequent flash writes.
- Training data can be exported before reboot if needed.
- The MVP should measure runtime behavior before designing persistence.

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

The MVP should collect all practical local metrics that are cheap and broadly available. Some values will be missing depending on kernel config, installed packages, and drivers. Missing metrics should be recorded as unavailable rather than treated as fatal errors.

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

- At daemon start.
- On LuCI manual refresh.

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

SQLite should be used in WAL-disabled or conservative mode under `/tmp` to keep behavior simple on volatile storage.

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

The first feature extractor should run every `feature_interval_sec`.

For each metric series in a window:

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

- table of latest raw metrics
- compact charts for CPU, memory, thermal, network, and conntrack
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

Prefer a small local command first:

```sh
edgepulse-ctl status --json
edgepulse-ctl latest --json
edgepulse-ctl features --json --window 60
edgepulse-ctl export --format csv
```

LuCI can call this through rpcd with an ACL that only exposes read/status/export and UCI settings actions.

## C Daemon Design

Process:

1. Load UCI config.
2. Create `/tmp/edgepulse`.
3. Open or initialize SQLite database.
4. Register collectors.
5. Run sampling loop.
6. Store raw metric samples.
7. Run feature extraction on interval.
8. Apply retention cleanup.
9. Handle `SIGTERM` cleanly.

Collector interface:

```c
typedef int (*edgepulse_collect_fn)(struct edgepulse_context *ctx);

struct edgepulse_collector {
    const char *name;
    int enabled;
    edgepulse_collect_fn collect;
};
```

Keep collectors independent. A failing collector must update `collector_status` and allow the daemon to continue.

## Acceptance Criteria

The first implementation is acceptable when:

- `edgepulse` starts on OpenWrt One.
- SQLite database is created under `/tmp/edgepulse`.
- CPU, memory, thermal, network, and conntrack samples are recorded.
- At least one feature window is computed.
- LuCI shows live overview values from local data.
- LuCI settings can change sampling interval and collector toggles.
- A CSV export command returns feature rows suitable for external training.

