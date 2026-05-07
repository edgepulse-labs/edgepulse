# README Review

Review date: 2026-05-07

## Summary

The current `README.md` is strong as a project vision document. It clearly positions EdgePulse as an edge telemetry and AI feature platform for OpenWrt, CPE, Raspberry Pi, Linux gateways, and desktop systems.

The document already explains:

- Why telemetry should become AI training data.
- The major collection surfaces: `/proc`, `/sys`, `ubus`, `iw`, `tc`, `conntrack`, `nftables`, thermal zones, and logs.
- The feature extraction direction: time-window statistics, variance, correlation, and tensorization.
- The first target device: OpenWrt One.
- Long-term AI goals: anomaly detection, health scoring, root cause analysis, and self-healing.

## Gaps To Fill

The README does not yet define the first executable milestone. The next documents should make these items concrete:

- Minimal OpenWrt package name, layout, daemon behavior, and build dependencies.
- OpenWrt One hardware assumptions and observable telemetry sources.
- SQLite schema for raw samples, derived features, metadata, and retention.
- `/tmp` storage policy, because OpenWrt runtime storage is volatile and flash writes should be minimized.
- LuCI views for dashboard, metrics, feature windows, and settings.
- UCI configuration options for sampling interval, retention, enabled collectors, and LuCI display behavior.
- A first training-data export format.
- Acceptance criteria for the smallest useful package.

## Recommended README Direction

Keep the README as the high-level product and research narrative. Avoid overloading it with implementation details. Link to `docs/` for:

- OpenWrt One MVP implementation plan.
- Telemetry metric catalog.
- Database schema.
- LuCI design.
- Package development notes.

## Immediate Documentation Action

Create a `docs/` directory and use it as the source of truth for implementation planning. The first detailed document should focus on the smallest C-based OpenWrt One package that can collect local telemetry into SQLite under `/tmp`, derive periodic feature windows, and expose current data through LuCI.

