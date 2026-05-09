# Unit Test Plan

Review 日期：2026-05-09

## 目標

在 EdgePulse 還很小的階段，先定義第一層 unit-test 邊界，讓測試保持輕量且快速。

第一層測試應驗證：

- `src/edgepulse-lib/` 內的 shared telemetry helpers。
- Command argument parsing。
- Snapshot calculation helpers。
- 可在 Linux host 上執行的 minimal collector behavior。
- `edgepulse` 與 `edgepulse-ctl` 的 CLI behavior。
- 透過 build-time checks 驗證 OpenWrt package install expectations。

## Test Program

初始 C unit-test program：

```text
tests/unit/test_edgepulse.c
```

執行方式：

```sh
make test
```

Test target 會 build：

```text
tests/unit/test_edgepulse
```

## 目前 Unit Coverage

第一版 unit-test program 涵蓋：

- `edgepulse_parse_positive_int()`
  - 接受 positive integers
  - 拒絕 zero、negative values、suffixes、empty strings 與 null input
- `edgepulse_memory_used_ratio()`
  - 處理 zero total memory
  - 計算預期的 used ratio
- `edgepulse_collect_snapshot()`
  - 可從目前 Linux host 收集 snapshot
  - 回傳 non-zero memory total

## CLI Smoke Tests

這些應維持為簡單 host checks：

```sh
make
./edgepulse status
./edgepulse-ctl status --json
./edgepulse-ctl version
```

Expected behavior:

- Status commands 回傳 JSON。
- Version command 回傳 `EDGEPULSE_VERSION`。
- Invalid arguments 回傳 exit code `2`。

## OpenWrt Package Checks

Package build 應驗證這些檔案會被安裝：

```text
/usr/bin/edgepulse
/usr/bin/edgepulse-ctl
/etc/config/edgepulse
/etc/init.d/edgepulse
/usr/share/luci/menu.d/luci-app-edgepulse.json
/usr/share/rpcd/acl.d/luci-app-edgepulse.json
/www/luci-static/resources/view/edgepulse/overview.js
```

建議驗證指令：

```sh
make package/feeds/edgepulse/edgepulse/compile V=s EDGEPULSE_LOCAL_SOURCE=/path/to/edgepulse
make package/feeds/edgepulse/luci-app-edgepulse/compile V=s EDGEPULSE_LOCAL_SOURCE=/path/to/edgepulse
```

## 近期測試補強

隨著實作擴大，加入更多 unit tests：

- Parse fixture versions of `/proc/stat`。
- Parse fixture versions of `/proc/meminfo`。
- Parse fixture versions of `/proc/loadavg`。
- 驗證 JSON output shape，但不要依賴 exact timestamps。
- 在 configurable state directory 下驗證 status file write behavior。
- 用 fixture status file 驗證 `edgepulse-ctl latest --json`。
- 為 missing 或 malformed collector inputs 加入 failure-path tests。

## Test Design Rules

- Unit tests 必須能在一般 Linux host 上執行。
- OpenWrt-only behavior 放在 package build tests 或 fixture-based tests。
- Parsers 與 calculations 優先寫成 pure functions。
- 擴大 coverage 前，collector tests 先加入 fixture injection 以保持 deterministic。
- Unit tests 不需要 root privileges。
- Unit tests 不需要 network access。
