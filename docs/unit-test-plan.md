# Unit Test Plan

Review date: 2026-05-09

## Goal

Define the first unit-test boundary for EdgePulse while the project is still small enough to keep tests lightweight and fast.

The first test layer should verify:

- Shared telemetry helpers in `src/edgepulse-lib/`.
- Command argument parsing.
- Snapshot calculation helpers.
- Minimal collector behavior that can run on a Linux host.
- CLI behavior for `edgepulse` and `edgepulse-ctl`.
- OpenWrt package install expectations through build-time checks.

## Test Program

The initial C unit-test program is:

```text
tests/unit/test_edgepulse.c
```

Run it with:

```sh
make test
```

The test target builds:

```text
tests/unit/test_edgepulse
```

## Current Unit Coverage

The first unit-test program covers:

- `edgepulse_parse_positive_int()`
  - accepts positive integers
  - rejects zero, negative values, suffixes, empty strings, and null input
- `edgepulse_memory_used_ratio()`
  - handles zero total memory
  - calculates the expected used ratio
- `edgepulse_collect_snapshot()`
  - can collect a snapshot from the current Linux host
  - returns non-zero memory total

## CLI Smoke Tests

These should stay as simple host checks:

```sh
make
./edgepulse status
./edgepulse-ctl status --json
./edgepulse-ctl version
```

Expected behavior:

- Status commands return JSON.
- Version command returns `EDGEPULSE_VERSION`.
- Invalid arguments return exit code `2`.

## OpenWrt Package Checks

The package build should verify that these files are installed:

```text
/usr/bin/edgepulse
/usr/bin/edgepulse-ctl
/etc/config/edgepulse
/etc/init.d/edgepulse
/usr/share/luci/menu.d/luci-app-edgepulse.json
/usr/share/rpcd/acl.d/luci-app-edgepulse.json
/www/luci-static/resources/view/edgepulse/overview.js
```

Recommended validation command:

```sh
make package/feeds/edgepulse/edgepulse/compile V=s EDGEPULSE_LOCAL_SOURCE=/path/to/edgepulse
make package/feeds/edgepulse/luci-app-edgepulse/compile V=s EDGEPULSE_LOCAL_SOURCE=/path/to/edgepulse
```

## Near-Term Test Additions

Add more unit tests as implementation grows:

- Parse fixture versions of `/proc/stat`.
- Parse fixture versions of `/proc/meminfo`.
- Parse fixture versions of `/proc/loadavg`.
- Validate JSON output shape without relying on exact timestamps.
- Validate status file write behavior under a configurable state directory.
- Validate `edgepulse-ctl latest --json` against a fixture status file.
- Add failure-path tests for missing or malformed collector inputs.

## Test Design Rules

- Keep unit tests runnable on a normal Linux host.
- Keep OpenWrt-only behavior behind package build tests or fixture-based tests.
- Prefer pure functions for parsers and calculations.
- Keep collector tests deterministic by adding fixture injection before expanding coverage.
- Do not require root privileges for unit tests.
- Do not require network access for unit tests.
