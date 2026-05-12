#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
BIN_DIR="/tmp/edgepulse-agent-mcp-bin"
CONFIG="/tmp/edgepulse-agent-mcp.conf"
DB="/tmp/edgepulse-agent-mcp.db"

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
	option shell_enabled '0'
	option ubus_enabled '1'
	option mcp_enabled '1'
	option policy_profile 'read_only'
	option tool_timeout_sec '2'
	option max_tool_output_bytes '2048'
	option mcp_allow_uci_get_edgepulse '0'
EOF_CONFIG

cat >"$BIN_DIR/ubus" <<'EOF_UBUS'
#!/bin/sh
if [ "$1 $2 $3" = "call network.interface dump" ]; then
	printf '{"interface":[{"interface":"wan","up":true}]}\n'
	exit 0
fi
if [ "$1 $2 $3" = "call network.wireless status" ]; then
	printf '{"radio0":{"up":true}}\n'
	exit 0
fi
printf '{}\n'
EOF_UBUS

cat >"$BIN_DIR/uci" <<'EOF_UCI'
#!/bin/sh
printf 'edgepulse.agent.mcp_enabled=1\n'
EOF_UCI

chmod 700 "$BIN_DIR"/*

export EDGEPULSE_CONFIG_PATH="$CONFIG"
export PATH="$BIN_DIR:$PATH"

methods_out="$("$ROOT/edgepulse-ctl" agent mcp methods)"
printf '%s\n' "$methods_out" | grep -q '"name": "edgepulse.uci.get.edgepulse".*"allowed": false' ||
	fail "mcp methods did not report disabled UCI method"

network_out="$("$ROOT/edgepulse-ctl" agent mcp call edgepulse.ubus.status.network)"
printf '%s\n' "$network_out" | grep -q '"status": "ok"' ||
	fail "MCP ubus network method did not succeed"
printf '%s\n' "$network_out" | grep -q 'wan' ||
	fail "MCP ubus network method did not return fixture output"

uci_out="$("$ROOT/edgepulse-ctl" agent mcp call edgepulse.uci.get.edgepulse)"
printf '%s\n' "$uci_out" | grep -q '"status": "disabled_by_policy"' ||
	fail "MCP UCI method ACL did not block disabled method"

tools_out="$(printf '{"jsonrpc":"2.0","id":7,"method":"tools/list"}\n' |
	"$ROOT/edgepulse-ctl" agent mcp serve)"
printf '%s\n' "$tools_out" | grep -q '"id":7' ||
	fail "MCP stdio tools/list did not preserve numeric id"
printf '%s\n' "$tools_out" | grep -q 'edgepulse.ubus.status.network' ||
	fail "MCP stdio tools/list did not include allowed ubus method"
printf '%s\n' "$tools_out" | grep -q 'edgepulse.uci.get.edgepulse' &&
	fail "MCP stdio tools/list exposed disabled UCI method"

echo "edgepulse MCP fixture integration passed"
