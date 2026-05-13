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
	option allow_service_restart '1'
EOF_CONFIG

cat >"$BIN_DIR/ubus" <<'EOF_UBUS'
#!/bin/sh
if [ "$1 $2 $3" = "call network.interface dump" ]; then
	printf '{"interface":[{"interface":"wan","up":true,"ipv4-address":[{"address":"198.51.100.10"}]}]}\n'
	exit 0
fi
if [ "$1 $2 $3" = "call network.interface.wan status" ]; then
	printf '{"interface":"wan","up":true,"proto":"dhcp","ipv4-address":[{"address":"198.51.100.10"}],"data":{"lease-acquired":12345}}\n'
	exit 0
fi
if [ "$1 $2 $3" = "call network.interface.lan status" ]; then
	printf '{"interface":"lan","up":true,"proto":"static","ipv4-address":[{"address":"192.0.2.1"}]}\n'
	exit 0
fi
if [ "$1 $2 $3" = "call network.wireless status" ]; then
	printf '{"radio0":{"up":true,"interfaces":[{"section":"default_radio0","config":{"ssid":"EdgePulse","key":"fixture-secret"},"stations":["02:00:00:00:00:01"]}]}}\n'
	exit 0
fi
if [ "$1 $2 $3" = "call service list" ]; then
	printf '{"dnsmasq":{"instances":{"instance1":{"running":true}}}}\n'
	exit 0
fi
printf '{}\n'
EOF_UBUS

cat >"$BIN_DIR/logread" <<'EOF_LOGREAD'
#!/bin/sh
printf 'edgepulse-agent: fixture recent log\n'
printf 'daemon.err edgepulse-agent: api_key=fixture-api-key token=fixture-token failed to reach backend\n'
printf 'daemon.warn netifd: wan warning recovered\n'
printf 'daemon.info edgepulse-agent: routine status ok\n'
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

cat >"$BIN_DIR/service" <<'EOF_SERVICE'
#!/bin/sh
printf 'service %s %s\n' "$1" "$2"
EOF_SERVICE

cat >"$BIN_DIR/iwinfo" <<'EOF_IWINFO'
#!/bin/sh
if [ "$1 $2" = "wlan0 info" ]; then
	printf 'ESSID: "EdgePulse"\n'
	printf 'Channel: 11\n'
	printf 'Signal: -45 dBm\n'
	printf 'Channel utilization: 18/255\n'
	exit 0
fi
if [ "$1 $2" = "wlan0 assoclist" ]; then
	printf '02:00:00:00:00:01  -48 dBm / -95 dBm (SNR 47)  120 ms ago\n'
	exit 0
fi
exit 1
EOF_IWINFO

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

iface_out="$("$ROOT/edgepulse-ctl" agent action interface-status --interface lan)"
printf '%s\n' "$iface_out" | grep -q '"action": "interface-status"' ||
	fail "interface-status did not complete action path"
printf '%s\n' "$iface_out" | grep -q '192.0.2.1' ||
	fail "interface-status did not return interface fixture output"

dhcp_out="$("$ROOT/edgepulse-ctl" agent action dhcp-status --interface wan)"
printf '%s\n' "$dhcp_out" | grep -q '"action": "dhcp-status"' ||
	fail "dhcp-status did not complete action path"
printf '%s\n' "$dhcp_out" | grep -q 'lease-acquired' ||
	fail "dhcp-status did not return DHCP state fixture output"

logs_out="$("$ROOT/edgepulse-ctl" agent action logs-recent --contains backend --level error)"
printf '%s\n' "$logs_out" | grep -q '"action": "logs-recent"' ||
	fail "logs-recent did not complete action path"
printf '%s\n' "$logs_out" | grep -q 'failed to reach backend' ||
	fail "logs-recent did not keep matching filtered line"
printf '%s\n' "$logs_out" | grep -q 'routine status ok' &&
	fail "logs-recent did not filter non-matching lines"
printf '%s\n' "$logs_out" | grep -q 'fixture-api-key' &&
	fail "logs-recent leaked API key"
printf '%s\n' "$logs_out" | grep -q 'fixture-token' &&
	fail "logs-recent leaked token"
printf '%s\n' "$logs_out" | grep -q 'api_key=redacted' ||
	fail "logs-recent did not redact API key"

wifi_metrics_out="$("$ROOT/edgepulse-ctl" agent action wifi-metrics --wifi-interface wlan0)"
printf '%s\n' "$wifi_metrics_out" | grep -q '"action": "wifi-metrics"' ||
	fail "wifi-metrics did not complete action path"
printf '%s\n' "$wifi_metrics_out" | grep -q '"name": "iwinfo.radio.info"' ||
	fail "wifi-metrics did not collect iwinfo radio info"
printf '%s\n' "$wifi_metrics_out" | grep -q '"name": "iwinfo.radio.assoclist"' ||
	fail "wifi-metrics did not collect iwinfo assoclist"
printf '%s\n' "$wifi_metrics_out" | grep -q 'Channel utilization' ||
	fail "wifi-metrics did not return utilization fixture output"
printf '%s\n' "$wifi_metrics_out" | grep -q '02:00:00:00:00:01' ||
	fail "wifi-metrics did not return station fixture output"

service_restart_out="$("$ROOT/edgepulse-ctl" agent action service-restart --service dnsmasq --confirm)"
printf '%s\n' "$service_restart_out" | grep -q '"action": "service-restart"' ||
	fail "service-restart did not complete action path"
printf '%s\n' "$service_restart_out" | grep -q '"name": "procd.service.restart"' ||
	fail "service-restart did not run restart tool"
printf '%s\n' "$service_restart_out" | grep -q 'service dnsmasq restart' ||
	fail "service-restart did not restart allowlisted service"
printf '%s\n' "$service_restart_out" | grep -q '"name": "ubus.service.list"' ||
	fail "service-restart did not verify service list"

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
