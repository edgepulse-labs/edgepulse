# Training Data Upload and Normalization

Review date: 2026-05-09

This document defines the planned mechanism for sending EdgePulse feature data to a remote collection server and the normalization strategy for model training data.

## Current State

EdgePulse currently supports local training-data preparation:

- Raw samples are stored in SQLite under `/tmp/edgepulse/edgepulse.db`.
- Feature windows are stored in `feature_rows`.
- `edgepulse-ctl export --format csv --window 60s --since 1h` exports feature rows.
- Feature rows use a long format: `device metadata + window + metric + labels + statistics`.
- Multi-interface and multi-thermal-zone devices are preserved through metric labels such as `iface=eth0,logical=wan` or `zone=0`.

EdgePulse does not yet upload feature rows to a remote server. It also does not yet emit a fixed-width tensor or stable quantized feature vector.

## Remote Upload Mechanism

The upload path should be implemented as a small, independent uploader rather than embedding network upload logic directly into every collector. The collector daemon should keep doing local sampling even when the remote server is down.

Recommended components:

- `edgepulse-uploader`: a small helper command or service that reads local feature rows and sends batches to a remote endpoint.
- Upload cursor state: store the last acknowledged `feature_rows.id` or `(window_end, metric, labels)` cursor locally.
- Upload queue/spool: keep bounded pending batches under SQLite or `/tmp/edgepulse/upload-spool`.
- Remote endpoint: HTTPS `POST` to a configurable URL.
- Payload format: JSON Lines or compact JSON by default; CSV can remain a manual export format.
- Authentication: bearer token or device enrollment token stored in UCI.
- Retry behavior: exponential backoff, bounded batch size, and no deletion until the server acknowledges the batch.
- Privacy mode: allow the device identifier to be hashed or replaced by an enrollment-issued opaque ID.

Suggested request shape:

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

The server should reply with the highest accepted cursor:

```json
{
  "accepted": true,
  "last_row_id": 1001
}
```

## UCI and LuCI Settings

The settings page should expose upload controls under the existing EdgePulse settings view.

Planned UCI options:

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

Planned LuCI fields:

- Upload enabled: on/off toggle.
- Remote collector URL: HTTPS URL.
- Authentication token: password input.
- Upload interval: integer seconds.
- Batch rows: bounded integer.
- TLS verification: on/off toggle, default on.
- Device ID mode: `opaque`, `hostname`, or `board-serial` where available.

The default must be disabled. Enabling upload should require a server URL and should not block local sampling if upload fails.

## Normalization Goals

Training data must be normalized in two separate stages:

- Device-local feature extraction: turn raw counters into time-window features.
- Training-time normalization: convert feature rows into stable model inputs.

The local device should avoid baking in final model-specific normalization constants too early. Instead, it should preserve enough metadata and labels for the collector server or training pipeline to build a stable schema.

## Current Representation

Current feature rows are sparse and label-preserving:

```text
metric=network.rx_bytes
labels=iface=eth0,logical=wan
window_sec=60
mean=...
delta=...
rate_per_sec=...
```

This representation supports variable numbers of interfaces and thermal zones because each observed source becomes a row. It is good for storage and ingestion, but it is not yet a fixed model vector.

## Multi-Interface Devices

Devices may have different network interface names, counts, and roles:

- `eth0`, `eth1`, `br-lan`, `pppoe-wan`, `wlan0`, `tailscale0`, `docker0`
- OpenWrt logical interfaces such as `wan`, `wan6`, `lan`, `guest`, `iot`

Current support:

- EdgePulse records physical interface counters from `/proc/net/dev`.
- When `ubus network.interface dump` is available, EdgePulse labels counters with logical OpenWrt names such as `logical=wan`.
- Export preserves all observed interfaces in long format.

Current limitation:

- EdgePulse does not yet map arbitrary interface sets into a fixed feature vector.

Recommended training schema:

- Prefer logical roles over physical names: `wan`, `lan`, `wifi`, `guest`, `vpn`, `loopback`, `other`.
- Aggregate by role where possible:
  - `network.role.wan.rx_bps_sum`
  - `network.role.lan.tx_bps_sum`
  - `network.role.wifi.rx_bps_sum`
  - `network.role.other.rx_bps_sum`
- Keep top-N slots for high-cardinality interfaces:
  - `network.iface.top1.rx_bps`
  - `network.iface.top2.rx_bps`
  - `network.iface.top3.rx_bps`
- Add masks:
  - `network.iface.top1.present`
  - `network.role.wifi.present`

This lets a fixed model input cover devices with one WAN interface, several LAN bridges, or extra VPN/container interfaces.

## Multi-Thermal-Zone Devices

Devices may expose one or many thermal zones. Zone numbering is not stable across hardware.

Current support:

- EdgePulse records `thermal.temp_c` labeled by `zone=N`.
- Export preserves every observed zone row.

Current limitation:

- EdgePulse does not yet read `/sys/class/thermal/thermal_zone*/type`, so `zone=0` has no semantic meaning across devices.
- EdgePulse does not yet derive fixed thermal aggregate features.

Recommended training schema:

- Record zone type when available:
  - `zone=0,type=cpu-thermal`
  - `zone=1,type=wifi`
- Derive stable aggregate features:
  - `thermal.max_temp_c`
  - `thermal.mean_temp_c`
  - `thermal.top1_temp_c`
  - `thermal.top2_temp_c`
  - `thermal.hot_zone_count`
  - `thermal.max_rate_c_per_sec`
- Add masks:
  - `thermal.top1.present`
  - `thermal.top2.present`

For OpenWrt One, model inputs should prefer aggregate thermal features until zone types are reliably collected.

## Quantization and Scaling

Fixed quantization parameters should be tied to canonical feature names, not raw device labels.

Good canonical features:

- `memory.used_ratio.mean`
- `network.role.wan.rx_bps.rate`
- `network.role.lan.tx_bps.rate`
- `thermal.max_temp_c.max`
- `network.conntrack.used_ratio.mean`

Weak canonical features:

- `network.rx_bytes.iface=eth0.mean`
- `thermal.temp_c.zone=0.max`

Recommended process:

1. Ingest long-format rows on the collection server.
2. Map rows to canonical feature names using a schema registry.
3. Aggregate variable labels into role-based and top-N features.
4. Emit a fixed vector plus a mask vector.
5. Compute normalization parameters from the training corpus:
   - ratios: clip to `[0, 1]`
   - temperatures: clip to an operational range such as `[-20, 120]` C before scaling
   - byte counters: use delta/rate features, then `log1p`
   - counts: use `log1p`
   - jiffies/counters: use rates or ratios rather than raw monotonic values
6. Version the schema and normalization parameters together.

Example fixed model input:

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

## Near-Term Implementation Plan

1. Add upload UCI options and LuCI settings fields.
2. Add an `edgepulse-ctl export --format json` or `jsonl` mode.
3. Add a small `edgepulse-upload` command that:
   - reads UCI upload settings,
   - exports unsent feature rows,
   - posts a bounded batch to the remote server,
   - stores the acknowledged cursor.
4. Add a procd service or timer loop for periodic upload.
5. Add server-side schema documentation for accepted payloads and acknowledgements.
6. Add thermal zone type collection.
7. Add role/top-N canonicalization in the server-side training pipeline first.
8. Later, add optional on-device canonical feature export once the schema stabilizes.

## Answer to Current Support

EdgePulse currently supports collecting and exporting variable interface and thermal-zone metrics as labeled feature rows. It does not yet support remote upload, fixed-width tensor generation, or stable quantized model inputs.

The recommended design is to keep the device-side export sparse and label-preserving, then let the collection server normalize it into canonical role-based and top-N features with explicit masks. This allows one fixed quantization schema to cover devices whose interface and thermal-zone counts vary.
