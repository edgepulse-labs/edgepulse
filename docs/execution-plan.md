# Execution Plan

Review date: 2026-05-07

## Goal

Build the smallest useful EdgePulse implementation for OpenWrt One:

- A C daemon package that samples local telemetry.
- A SQLite database stored under `/tmp`.
- Periodic feature extraction for AI training data.
- A LuCI application to show runtime metrics and configure sampling.

## Phase 0: Documentation Baseline

Status: started

Deliverables:

- `docs/README.md`
- `docs/readme-review.md`
- `docs/execution-plan.md`
- `docs/openwrt-one-telemetry-mvp.md`

Exit criteria:

- The project has a clear MVP boundary.
- OpenWrt One assumptions are documented with source links.
- Metric, SQLite, and LuCI plans are explicit enough to become tickets.

## Phase 1: Package Skeleton

Create the OpenWrt feed repository and package structure:

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

- OpenWrt can consume the feed through `feeds.conf`.
- Package cross-compiles in an OpenWrt SDK for `mediatek/filogic`.
- Daemon starts through `/etc/init.d/edgepulse`.
- UCI config can enable or disable the daemon.

Reference:

- [OpenWrt feeds and repos](openwrt-feeds-and-repos.md)

## Phase 2: Minimal Raw Sampling

Implement low-risk file-based collectors first:

- CPU: `/proc/stat`
- Memory: `/proc/meminfo`
- Load: `/proc/loadavg`
- Network interfaces: `/proc/net/dev`
- Thermal: `/sys/class/thermal/thermal_zone*/temp`
- Uptime: `/proc/uptime`

Exit criteria:

- Samples are written to `/tmp/edgepulse/edgepulse.db`.
- Sampling interval is controlled by UCI.
- Collector failures are stored as status, not fatal daemon crashes.

## Phase 3: OpenWrt-Specific Collectors

Add OpenWrt integration:

- `ubus` system board information.
- `ubus` network interface status.
- Wireless status through `ubus`/`iwinfo` where available.
- Conntrack count from `/proc/sys/net/netfilter/nf_conntrack_count`.
- nftables/counter support as optional later work.

Exit criteria:

- OpenWrt One board metadata is captured.
- WAN/LAN counters can be associated with logical interfaces.
- Wi-Fi radio/interface metrics are recorded when available.

## Phase 4: Feature Windows

Compute periodic features from raw samples:

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

- Features are stored separately from raw samples.
- Feature rows include metric name, window size, start time, end time, and value.
- Export query can produce training rows.

## Phase 5: LuCI Application

Create a LuCI app:

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

- Overview: health snapshot, latest CPU, memory, thermal, network, and collector status.
- Metrics: latest raw metrics and short time-series charts.
- Features: derived windows prepared for training data.
- Settings: UCI-backed sampling interval, retention, enabled collectors, and database path.

Exit criteria:

- LuCI can read `/tmp/edgepulse/edgepulse.db` through a small RPC endpoint or JSON export command.
- Settings are persisted through UCI.
- The UI works on desktop and mobile LuCI layouts.

## Phase 6: Training Data Export

Add a local export command:

```sh
edgepulse export --format csv --window 60s --since 1h
```

Exit criteria:

- CSV export has stable column names.
- Export includes device metadata and feature timestamps.
- Missing metrics are represented consistently.

## MVP Definition

The first MVP is complete when OpenWrt One can:

- Run `edgepulse` as a lightweight daemon.
- Store volatile telemetry in `/tmp/edgepulse/edgepulse.db`.
- Derive time-window features.
- Show latest metrics and settings in LuCI.
- Export feature rows for external model training.
