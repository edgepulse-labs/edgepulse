# AI Agent OpenWrt Operations Scenarios

Review date: 2026-05-10

This document defines common user conversation scenarios for the EdgePulse AI Agent when it is used as an OpenWrt operations assistant. The agent should translate user intent into policy-gated OpenWrt actions, execute only approved tools, and report what it did with evidence.

## Current Status

The first CLI implementation exists and has been package-tested on OpenWrt One.
Read-only `status`, `wifi-status`, and `logs-recent` are implemented. Confirmed
`reconnect-wan` and `wifi-set` exist behind the `operator_confirmed` policy and
explicit `--confirm` path. The LuCI AI Agent page can run diagnostic/chat
requests today; dedicated operation buttons and confirmation UX remain future
work.

## Operating Model

The agent has two action levels:

- Read-only: status checks, telemetry summaries, `ubus` status queries, and bounded recent log reads. These can run without extra confirmation when the agent is enabled.
- Confirmed operations: UCI writes, Wi-Fi reloads, WAN reconnects, or any action that can interrupt connectivity. These require `policy_profile=operator_confirmed` and an explicit `--confirm` execution path from CLI or LuCI.

Every operation must return:

- requested intent and selected action,
- policy decision,
- tool calls and exit status,
- relevant command output or structured `ubus` evidence,
- user-facing answer that states whether the request completed, needs confirmation, or failed.

## Intent Catalog

| User intent       | Example user request           | Agent action    | Tools                                                                                  | Confirmation |
|-------------------|--------------------------------|-----------------|----------------------------------------------------------------------------------------|--------------|
| Router status     | "How is my router doing?"      | `status`        | `uptime`, `ubus network.interface dump`, `ubus network.wireless status`                | No           |
| Wi-Fi status      | "Is Wi-Fi online?"             | `wifi-status`   | `ubus network.wireless status`                                                         | No           |
| Connection status | "Check WAN/LAN connectivity."  | `status`        | `ubus network.interface dump` plus telemetry                                           | No           |
| Recent anomalies  | "Any abnormal logs recently?"  | `logs-recent`   | `logread -l 80`                                                                        | No           |
| Reconnect WAN     | "Reconnect the internet."      | `reconnect-wan` | `ifdown wan`, `ifup wan`, follow-up `ubus` status                                      | Yes          |
| Configure Wi-Fi   | "Set Wi-Fi SSID to EdgePulse." | `wifi-set`      | `uci set wireless...`, `uci commit wireless`, `wifi reload`, follow-up wireless status | Yes          |

## Conversation Scenarios

### 1. Query Router Health

User:

```text
路由器現在狀態正常嗎？
```

Expected behavior:

- Agent maps the request to `status`.
- It collects uptime, interface status, wireless status, and EdgePulse telemetry.
- It answers with a concise health summary and includes tool evidence.

CLI path:

```sh
edgepulse-ctl agent action status
```

### 2. Query Wi-Fi Status

User:

```text
Wi-Fi 有開嗎？目前 SSID 和 radio 狀態如何？
```

Expected behavior:

- Agent maps the request to `wifi-status`.
- It calls `ubus call network.wireless status`.
- It reports radio/interface status and whether tool output was available.

CLI path:

```sh
edgepulse-ctl agent action wifi-status
```

### 3. Reconnect WAN

User:

```text
網路好像斷了，幫我重新撥接 WAN。
```

Expected behavior:

- First response should explain that reconnecting WAN changes network state and may interrupt connectivity.
- The operation must not run unless the caller uses the confirmed action path.
- After confirmation, the agent runs `ifdown wan`, `ifup wan`, then queries interface status and reports the result.

CLI path:

```sh
edgepulse-ctl agent action reconnect-wan
edgepulse-ctl agent action reconnect-wan --confirm
```

### 4. Configure Wi-Fi

User:

```text
把 Wi-Fi 名稱改成 EdgePulseLab，密碼設成一組新的 WPA2 密碼。
```

Expected behavior:

- Agent extracts SSID, encryption mode, and key.
- It refuses missing or unsafe values and asks for the missing field.
- It requires confirmation because it writes UCI config and reloads Wi-Fi.
- After execution, it reports each UCI/write/reload step and then checks wireless status.

CLI path:

```sh
edgepulse-ctl agent action wifi-set --ssid EdgePulseLab --key '<new-password>'
edgepulse-ctl agent action wifi-set --ssid EdgePulseLab --key '<new-password>' --confirm
```

### 5. Recent Abnormal Records

User:

```text
最近有沒有異常紀錄？
```

Expected behavior:

- Agent maps the request to `logs-recent`.
- It reads a bounded recent log window with `logread -l 80`.
- It summarizes errors, warnings, reconnects, service restarts, DNS failures, authentication failures, or reports that no obvious issue was found.

CLI path:

```sh
edgepulse-ctl agent action logs-recent
```

## Implementation Plan

- [x] Add an `edgepulse-ctl agent action` CLI entrypoint for common OpenWrt operations.
- [x] Add read-only actions for router status, Wi-Fi status, and recent logs.
- [x] Add confirmed action support for WAN reconnect and Wi-Fi setting changes.
- [x] Require `operator_confirmed` policy plus `--confirm` for state-changing actions.
- [x] Audit every action request and tool result in SQLite.
- [x] Return structured JSON with action status, request ID, tools, and final answer.
- [ ] Add LuCI controls that route natural-language or button-driven operations to the same `agent action` path.
- [ ] Add a small intent classifier that maps common Chinese and English requests to action IDs before model fallback.
- [ ] Add richer post-action verification for WAN IP, DNS reachability, associated Wi-Fi clients, and radio up/down state.
- [ ] Add integration tests with fixture `ubus`, `uci`, `ifup`, `ifdown`, `wifi`, and `logread` binaries.
- [ ] Add per-action permission switches so deployments can allow WAN reconnect without allowing Wi-Fi mutation.

## Future Expansion

- Add a LuCI operation panel with button-driven status, Wi-Fi status, logs,
  reconnect WAN, and Wi-Fi setup flows.
- Use the shared chat transcript to show what action was selected, which tools
  ran, and what verification followed.
- Add per-action UCI switches so deployments can separately allow WAN
  reconnect, Wi-Fi reload, Wi-Fi mutation, and log inspection.
- Add an intent layer that is deterministic for common Chinese/English router
  requests and only falls back to a model when the intent is ambiguous.
- Add richer verification after mutation, including WAN address changes, DNS
  probes, gateway reachability, radio state, and associated clients.

## Safety Rules

- Default policy remains read-only.
- State-changing operations must never run from an unconfirmed natural-language prompt alone.
- The agent must prefer structured `ubus` status over free-form shell output when available.
- The agent must not print Wi-Fi keys, API keys, or tokens in JSON, logs, or audit records.
- Failed partial operations must be reported as partial, with the failing tool status preserved.
