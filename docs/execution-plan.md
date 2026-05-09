# Execution Plan

Review date: 2026-05-09

## Current Status

The project has completed the first MVP path on OpenWrt One:

- [x] C library shared by the daemon and CLI.
- [x] `edgepulse` daemon command with periodic JSON status output.
- [x] `edgepulse-ctl` minimum CLI with `status`, `latest`, `features`, `export`, and `version` commands.
- [x] Unit test program for the current shared telemetry helpers.
- [x] OpenWrt feed package skeleton for `edgepulse` and `luci-app-edgepulse`.
- [x] Local OpenWrt buildroot validation for `.apk` package output.
- [x] OpenWrt One install verification with `apk add --allow-untrusted`.
- [x] SQLite-backed raw sample storage under `/tmp/edgepulse/edgepulse.db`.
- [x] Stored feature windows with mean, min, max, standard deviation, delta, rate, and coefficient of variation.
- [x] CSV export for training rows backed by stored feature rows.
- [x] LuCI overview, metrics, features, and settings pages packaged and installed.

The next implementation focus is to harden the MVP further: add retention cleanup, optional nftables counters, and longer-running reliability validation.

## Goal

Build the smallest useful EdgePulse implementation for OpenWrt One:

- A C daemon package that samples local telemetry.
- A SQLite database stored under `/tmp`.
- Periodic feature extraction for AI training data.
- A LuCI application to show runtime metrics and configure sampling.

## Phase 0: Documentation Baseline

Status: complete

Todo:

- [x] Create `docs/README.md`.
- [x] Create `docs/readme-review.md`.
- [x] Create `docs/execution-plan.md`.
- [x] Create `docs/openwrt-one-telemetry-mvp.md`.
- [x] Create Traditional Chinese translations for project docs.
- [x] Document local OpenWrt package validation.
- [x] Document the unit-test plan.

Exit criteria:

- The project has a clear MVP boundary.
- OpenWrt One assumptions are documented with source links.
- Metric, SQLite, and LuCI plans are explicit enough to become tickets.

## Phase 1: Package Skeleton

Status: complete for MVP

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

Todo:

- [x] Add `packaging/openwrt-feed/edgepulse/Makefile`.
- [x] Add `packaging/openwrt-feed/edgepulse/files/etc/config/edgepulse`.
- [x] Add `packaging/openwrt-feed/edgepulse/files/etc/init.d/edgepulse`.
- [x] Add `packaging/openwrt-feed/luci-app-edgepulse/Makefile`.
- [x] Add LuCI menu metadata.
- [x] Add rpcd ACL metadata.
- [x] Install both `edgepulse` and `edgepulse-ctl` into the OpenWrt package.
- [x] Build `edgepulse-1.apk` in the local OpenWrt buildroot.
- [x] Build `luci-app-edgepulse-1.apk` in the local OpenWrt buildroot.
- [x] Install and verify both packages on OpenWrt One.
- [x] Sync the feed copy into the standalone `edgepulse-openwrt-feed` repository for local OpenWrt builds.
- [x] Add release/version workflow for source archives and OpenWrt package `PKG_RELEASE` updates.

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

Status: complete for MVP

Implement low-risk file-based collectors first:

- [x] CPU: `/proc/stat`
- [x] Memory: `/proc/meminfo`
- [x] Load: `/proc/loadavg`
- [x] Network interfaces: `/proc/net/dev`
- [x] Thermal: `/sys/class/thermal/thermal_zone*/temp`
- [x] Uptime: `/proc/uptime`

Todo:

- [x] Add shared `edgepulse_collect_snapshot()` helper.
- [x] Emit a current JSON status snapshot.
- [x] Keep daemon output under `/tmp/edgepulse`.
- [x] Add SQLite schema initialization under `/tmp/edgepulse/edgepulse.db`.
- [x] Write raw samples into SQLite instead of only `edgepulse.json`.
- [x] Read daemon interval from UCI through the init script.
- [x] Record per-collector status so one failed collector does not fail the whole sample.
- [x] Add fixture-based tests for parsing `/proc` and `/sys` files.

Exit criteria:

- Samples are written to `/tmp/edgepulse/edgepulse.db`.
- Sampling interval is controlled by UCI.
- Collector failures are stored as status, not fatal daemon crashes.

## Phase 3: OpenWrt-Specific Collectors

Status: complete for MVP

Add OpenWrt integration:

- [x] `ubus` system board information.
- [x] `ubus` network interface status.
- [x] Wireless status through `/proc/net/wireless` where available.
- [x] Conntrack count from `/proc/sys/net/netfilter/nf_conntrack_count`.
- [ ] nftables/counter support as optional later work.

Todo:

- [x] Add a small OpenWrt integration layer around `libubus`.
- [x] Store basic device metadata in a device metadata table.
- [x] Map physical interface counters to logical OpenWrt interfaces.
- [x] Treat missing conntrack sources as unavailable, not fatal.
- [x] Treat missing wireless sources as unavailable, not fatal.

Exit criteria:

- OpenWrt One board metadata is captured.
- WAN/LAN counters can be associated with logical interfaces.
- Wi-Fi interface metrics are recorded from `/proc/net/wireless` when available.

## Phase 4: Feature Windows

Status: complete for MVP

Compute periodic features from raw samples:

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

- [x] Define the feature table schema.
- [x] Add feature-window computation over SQLite raw samples.
- [x] Add `edgepulse-ctl features --json --window 60` implementation.
- [x] Add unit tests for feature calculations.

Exit criteria:

- Features are stored separately from raw samples.
- Feature rows include metric name, window size, start time, end time, and value.
- Export query can produce training rows.

## Phase 5: LuCI Application

Status: complete for MVP

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

- [x] Overview: initial health snapshot, load, memory, and uptime.
- [x] Overview: latest CPU, thermal, network, and collector status.
- [x] Metrics: latest raw metrics table.
- [x] Features: derived windows prepared for training data.
- [x] Settings: UCI-backed sampling interval, retention, enabled collectors, and database path.

Todo:

- [x] Add LuCI overview route.
- [x] Add rpcd ACL allowing LuCI to execute `edgepulse-ctl`.
- [x] Wire overview page to `edgepulse-ctl status --json`.
- [x] Add `metrics.js`.
- [x] Add `features.js`.
- [x] Add `settings.js`.
- [x] Replace direct command execution with a narrower RPC endpoint when the data model stabilizes.
- [x] Verify LuCI page rendering in a browser on OpenWrt One.

Exit criteria:

- LuCI can read `/tmp/edgepulse/edgepulse.db` through a small RPC endpoint or JSON export command.
- Settings are persisted through UCI.
- The UI works on desktop and mobile LuCI layouts.

## Phase 6: Training Data Export

Status: complete for MVP

Add a local export command:

```sh
edgepulse-ctl export --format csv --window 60s --since 1h
```

Todo:

- [x] Add placeholder `edgepulse-ctl export` command.
- [x] Implement CSV export from computed feature rows.
- [x] Add `--format`, `--window`, and `--since` argument parsing.
- [x] Include device metadata in exported rows.
- [x] Add stable CSV headers.
- [x] Add tests for missing metric representation.

Exit criteria:

- CSV export has stable column names.
- Export includes device metadata and feature timestamps.
- Missing metrics are represented consistently.

## MVP Definition

The first MVP is complete when OpenWrt One can:

- [x] Run `edgepulse` as a lightweight daemon.
- [x] Store volatile telemetry in `/tmp/edgepulse/edgepulse.db`.
- [x] Derive time-window features.
- [x] Show latest metrics and settings in LuCI.
- [x] Export feature rows for external model training.
