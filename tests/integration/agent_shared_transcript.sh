#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
BIN_DIR="/tmp/edgepulse-agent-chat-bin"
CONFIG="/tmp/edgepulse-agent-chat.conf"
DB="/tmp/edgepulse-agent-chat.db"

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
	option memory_enabled '0'
	option shell_enabled '0'
	option ubus_enabled '0'
	option chat_enabled '1'
	option mcp_enabled '1'
	option default_conversation_id 'default'
	option policy_profile 'read_only'
	option tool_timeout_sec '2'
	option max_tool_output_bytes '2048'
EOF_CONFIG

cat >"$BIN_DIR/edgepulse-luci" <<EOF_LUCI
#!/bin/sh
case "\$1" in
	agent-chat-ask)
		exec "$ROOT/edgepulse-ctl" agent chat ask "\$2" "\$3"
		;;
	agent-chat-list)
		if [ "\$#" -ge 2 ]; then
			exec "$ROOT/edgepulse-ctl" agent chat list "\$2"
		fi
		exec "$ROOT/edgepulse-ctl" agent chat list
		;;
	*)
		echo "unsupported helper command" >&2
		exit 2
		;;
esac
EOF_LUCI
chmod 700 "$BIN_DIR/edgepulse-luci"

export EDGEPULSE_CONFIG_PATH="$CONFIG"
export PATH="$BIN_DIR:$PATH"

"$ROOT/edgepulse-ctl" agent chat ask shared-cli "message from cli" >/tmp/edgepulse-agent-chat-cli.out
edgepulse-luci agent-chat-ask shared-luci "message from luci helper" >/tmp/edgepulse-agent-chat-luci.out
"$ROOT/edgepulse-ctl" agent mcp call edgepulse.agent.chat.ask shared-mcp "message from mcp" >/tmp/edgepulse-agent-chat-mcp.out

cli_list="$("$ROOT/edgepulse-ctl" agent chat list shared-cli)"
printf '%s\n' "$cli_list" | grep -q 'message from cli' ||
	fail "CLI conversation did not include CLI message"

luci_list="$(edgepulse-luci agent-chat-list shared-luci)"
printf '%s\n' "$luci_list" | grep -q 'message from luci helper' ||
	fail "LuCI-compatible helper conversation did not include helper message"

mcp_list="$("$ROOT/edgepulse-ctl" agent mcp call edgepulse.agent.chat.list shared-mcp)"
printf '%s\n' "$mcp_list" | grep -q 'message from mcp' ||
	fail "MCP conversation did not include MCP message"

all_list="$("$ROOT/edgepulse-ctl" agent chat list)"
printf '%s\n' "$all_list" | grep -q 'shared-cli' ||
	fail "Conversation index missing shared-cli"
printf '%s\n' "$all_list" | grep -q 'shared-luci' ||
	fail "Conversation index missing shared-luci"
printf '%s\n' "$all_list" | grep -q 'shared-mcp' ||
	fail "Conversation index missing shared-mcp"

rm -f /tmp/edgepulse-agent-chat-cli.out /tmp/edgepulse-agent-chat-luci.out /tmp/edgepulse-agent-chat-mcp.out
echo "edgepulse shared transcript integration passed"
