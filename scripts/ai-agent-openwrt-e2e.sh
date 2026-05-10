#!/bin/sh

set -eu

ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"

if [ "${EDGEPULSE_SKIP_DOTENV:-0}" != "1" ] && [ -f "$ROOT/.env" ]; then
	set -a
	. "$ROOT/.env"
	set +a
fi

quote_sh()
{
	printf "'%s'" "$(printf "%s" "$1" | sed "s/'/'\\\\''/g")"
}

require_var()
{
	name="$1"
	eval "value=\${$name:-}"
	if [ -z "$value" ]; then
		echo "missing required environment variable: $name" >&2
		exit 2
	fi
}

ssh_target="${EDGEPULSE_OPENWRT_SSH_TARGET:-}"
ssh_port="${EDGEPULSE_OPENWRT_SSH_PORT:-}"
ssh_opts="${EDGEPULSE_OPENWRT_SSH_OPTS:-}"
prompt="${EDGEPULSE_AGENT_E2E_PROMPT:-Summarize CPU, memory, uptime, and network health from the provided telemetry in one concise sentence.}"
expect_finish="${EDGEPULSE_AGENT_E2E_EXPECT_FINISH_REASON:-stop}"
remote_out="${EDGEPULSE_AGENT_E2E_REMOTE_OUT:-/tmp/edgepulse-agent-e2e.json}"

require_var EDGEPULSE_OPENWRT_SSH_TARGET

ssh_cmd="ssh"
if [ -n "$ssh_port" ]; then
	ssh_cmd="$ssh_cmd -p $(quote_sh "$ssh_port")"
fi
if [ -n "$ssh_opts" ]; then
	ssh_cmd="$ssh_cmd $ssh_opts"
fi
ssh_cmd="$ssh_cmd $(quote_sh "$ssh_target")"

if [ "${EDGEPULSE_E2E_APPLY_CONFIG:-0}" = "1" ]; then
	section="${EDGEPULSE_AI_MODEL_SECTION:-remote_reasoner}"
	require_var EDGEPULSE_AI_BASE_URL
	require_var EDGEPULSE_AI_MODEL
	remote_apply="
uci set edgepulse.agent.enabled=$(quote_sh "${EDGEPULSE_AI_AGENT_ENABLED:-1}")
uci set edgepulse.agent.local_only=$(quote_sh "${EDGEPULSE_AI_AGENT_LOCAL_ONLY:-0}")
uci set edgepulse.$section.enabled='1'
uci set edgepulse.$section.base_url=$(quote_sh "$EDGEPULSE_AI_BASE_URL")
uci set edgepulse.$section.model=$(quote_sh "$EDGEPULSE_AI_MODEL")
uci set edgepulse.$section.api_key=$(quote_sh "${EDGEPULSE_AI_API_KEY:-}")
uci set edgepulse.$section.api_key_env=$(quote_sh "${EDGEPULSE_AI_API_KEY_ENV:-EDGEPULSE_AI_API_KEY}")
uci set edgepulse.$section.timeout_sec=$(quote_sh "${EDGEPULSE_AI_TIMEOUT_SEC:-60}")
uci set edgepulse.$section.retry_count=$(quote_sh "${EDGEPULSE_AI_RETRY_COUNT:-0}")
uci set edgepulse.$section.max_tokens=$(quote_sh "${EDGEPULSE_AI_MAX_TOKENS:-2048}")
uci set edgepulse.$section.no_think=$(quote_sh "${EDGEPULSE_AI_NO_THINK:-0}")
uci commit edgepulse
/etc/init.d/edgepulse restart >/dev/null 2>&1 || true
"
	eval "$ssh_cmd $(quote_sh "$remote_apply")"
fi

remote_prompt="$(quote_sh "$prompt")"
remote_out_q="$(quote_sh "$remote_out")"
remote_test="
edgepulse-ctl agent status
edgepulse-ctl agent ask $remote_prompt >$remote_out_q
echo E2E_STATUS=\$(jsonfilter -i $remote_out_q -e @.status)
echo E2E_MODEL_STATUS=\$(jsonfilter -i $remote_out_q -e @.model_response.status)
echo E2E_FINISH_REASON=\$(jsonfilter -i $remote_out_q -e @.model_response.finish_reason)
echo E2E_REASONING_PRESENT=\$(jsonfilter -i $remote_out_q -e @.model_response.reasoning_present)
echo E2E_ANSWER=\$(jsonfilter -i $remote_out_q -e @.answer)
logread -e edgepulse-agent | tail -8
"

output="$(eval "$ssh_cmd $(quote_sh "$remote_test")")"
printf "%s\n" "$output"

finish="$(printf "%s\n" "$output" | sed -n 's/^E2E_FINISH_REASON=//p' | tail -1)"
model_status="$(printf "%s\n" "$output" | sed -n 's/^E2E_MODEL_STATUS=//p' | tail -1)"

if [ "$model_status" != "ok" ]; then
	echo "E2E failed: expected model status ok, got $model_status" >&2
	exit 1
fi

if [ -n "$expect_finish" ] && [ "$finish" != "$expect_finish" ]; then
	echo "E2E failed: expected finish_reason $expect_finish, got $finish" >&2
	exit 1
fi

echo "edgepulse OpenWrt AI agent E2E passed"
