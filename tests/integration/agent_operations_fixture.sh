#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
BIN_DIR="/tmp/edgepulse-agent-ops-bin"
CONFIG="/tmp/edgepulse-agent-ops.conf"
DB="/tmp/edgepulse-agent-ops.db"

cleanup() {
	rm -rf "$BIN_DIR"
	rm -f "$CONFIG" "$DB" "$DB-wal" "$DB-shm"
}
trap cleanup EXIT

fail() {
	echo "FAIL: $*" >&2
	exit 1
}

mkdir -p "$BIN_DIR"
rm -f "$DB" "$DB-wal" "$DB-shm"

cat >"$CONFIG" <<EOF_CONFIG
config edgepulse 'main'
	option db_path '$DB'

config agent 'agent'
	option enabled '1'
	option local_only '1'
	option shell_enabled '1'
	option ubus_enabled '1'
	option policy_profile 'operator_confirmed'
	option tool_timeout_sec '2'
	option max_tool_output_bytes '2048'
	option allow_reconnect_wan '1'
	option allow_wifi_restart '1'
	option allow_wifi_set '1'
EOF_CONFIG

cat >"$BIN_DIR/ubus" <<'EOF_UBUS'
#!/bin/sh
if [ "$1 $2 $3" = "call network.interface dump" ]; then
	printf '{"interface":[{"interface":"wan","up":true,"ipv4-address":[{"address":"198.51.100.10"}]}]}\n'
	exit 0
fi
if [ "$1 $2 $3" = "call network.wireless status" ]; then
	printf '{"radio0":{"up":true,"interfaces":[{"section":"default_radio0","config":{"ssid":"EdgePulse","key":"fixture-secret"},"stations":["02:00:00:00:00:01"]}]}}\n'
	exit 0
fi
printf '{}\n'
EOF_UBUS

cat >"$BIN_DIR/logread" <<'EOF_LOGREAD'
#!/bin/sh
printf 'edgepulse-agent: fixture recent log\n'
EOF_LOGREAD

cat >"$BIN_DIR/uci" <<'EOF_UCI'
#!/bin/sh
printf 'uci fixture: %s %s\n' "$1" "${2:-}"
EOF_UCI

cat >"$BIN_DIR/ifdown" <<'EOF_IFDOWN'
#!/bin/sh
printf 'ifdown %s\n' "$1"
EOF_IFDOWN

cat >"$BIN_DIR/ifup" <<'EOF_IFUP'
#!/bin/sh
printf 'ifup %s\n' "$1"
EOF_IFUP

cat >"$BIN_DIR/wifi" <<'EOF_WIFI'
#!/bin/sh
printf 'wifi %s\n' "$1"
EOF_WIFI

cat >"$BIN_DIR/ping" <<'EOF_PING'
#!/bin/sh
printf 'PING %s ok\n' "$5"
EOF_PING

chmod 700 "$BIN_DIR"/*

export EDGEPULSE_CONFIG_PATH="$CONFIG"
export PATH="$BIN_DIR:$PATH"

status_out="$("$ROOT/edgepulse-ctl" agent ask "Wi-Fi 有開嗎？")"
printf '%s\n' "$status_out" | grep -q '"action": "wifi-status"' ||
	fail "Wi-Fi intent did not route to wifi-status"
printf '%s\n' "$status_out" | grep -q 'key=redacted' ||
	fail "Wi-Fi status output did not redact key"
printf '%s\n' "$status_out" | grep -q 'fixture-secret' &&
	fail "Wi-Fi status leaked fixture secret"

wan_out="$("$ROOT/edgepulse-ctl" agent action reconnect-wan --confirm)"
printf '%s\n' "$wan_out" | grep -q '"name": "net.ping.ip"' ||
	fail "WAN reconnect did not run IP reachability verification"
printf '%s\n' "$wan_out" | grep -q '"name": "net.ping.dns"' ||
	fail "WAN reconnect did not run DNS reachability verification"

wifi_restart_out="$("$ROOT/edgepulse-ctl" agent action wifi-restart --confirm)"
printf '%s\n' "$wifi_restart_out" | grep -q '"action": "wifi-restart"' ||
	fail "wifi-restart did not complete action path"
printf '%s\n' "$wifi_restart_out" | grep -q '"name": "wifi.reload"' ||
	fail "wifi-restart did not reload Wi-Fi"
printf '%s\n' "$wifi_restart_out" | grep -q '"name": "ubus.network.wireless.status"' ||
	fail "wifi-restart did not verify wireless status"

wifi_out="$("$ROOT/edgepulse-ctl" agent action wifi-set --ssid EdgePulse --key fixture-secret --confirm)"
printf '%s\n' "$wifi_out" | grep -q '"action": "wifi-set"' ||
	fail "wifi-set did not complete action path"
printf '%s\n' "$wifi_out" | grep -q 'fixture-secret' &&
	fail "wifi-set leaked key in output"

echo "edgepulse agent operations fixture integration passed"
