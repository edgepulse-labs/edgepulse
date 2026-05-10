#!/bin/sh

set -eu

ROOT="$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)"
PORT="${EDGEPULSE_AGENT_TEST_PORT:-18181}"
CONFIG="/tmp/edgepulse-agent-model-test.conf"
DB="/tmp/edgepulse-agent-model-test.db"
OUT="/tmp/edgepulse-agent-model-test.out"
SERVER_PID=""

cleanup()
{
	if [ -n "$SERVER_PID" ]; then
		kill "$SERVER_PID" 2>/dev/null || true
		wait "$SERVER_PID" 2>/dev/null || true
	fi
	rm -f "$CONFIG" "$DB" "$DB-wal" "$DB-shm" "$OUT"
}

trap cleanup EXIT INT TERM

rm -f "$CONFIG" "$DB" "$DB-wal" "$DB-shm" "$OUT"

"$ROOT/tests/integration/mock_openai_server" "$PORT" &
SERVER_PID="$!"
sleep 1

cat >"$CONFIG" <<EOF_CONFIG
config edgepulse 'main'
	option db_path '$DB'

config agent 'agent'
	option enabled '1'
	option local_only '0'
	option memory_enabled '1'
	option shell_enabled '1'
	option ubus_enabled '0'
	option policy_profile 'read_only'
	option request_timeout_sec '30'
	option heartbeat_interval_sec '60'
	option tool_timeout_sec '5'
	option max_tool_output_bytes '2048'

config model 'remote_reasoner'
	option enabled '1'
	option role 'planner,analyzer,responder'
	option base_url 'http://127.0.0.1:$PORT/v1'
	option model 'mock-openai'
	option api_key 'integration-secret'
	option api_key_env ''
	option timeout_sec '5'
	option retry_count '0'
EOF_CONFIG

EDGEPULSE_CONFIG_PATH="$CONFIG" "$ROOT/edgepulse-ctl" agent ask "Check local telemetry." >"$OUT"

grep -q '"model_status": "configured"' "$OUT"
grep -q '"status": "ok"' "$OUT"
grep -q '"api_key": "redacted"' "$OUT"
grep -q '"answer": "local model response"' "$OUT"
if grep -q 'integration-secret' "$OUT"; then
	echo "secret leaked in agent output" >&2
	exit 1
fi

echo "edgepulse agent model integration passed"
