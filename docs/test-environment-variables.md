# Test Environment Variables

EdgePulse uses environment variables for device-specific and secret test configuration. This keeps the repository portable while still allowing repeatable unit, integration, and end-to-end validation.

## Recommendation

The approach is appropriate, with one important boundary:

- Commit `.env-example` as documentation and a starter template.
- Keep the real `.env` file local and ignored by git.
- Do not put real API keys, private hostnames, or lab-only paths in tracked docs.
- Load `.env` from the shell before running tests instead of parsing it automatically in every Makefile target.

The shell-loading pattern is more robust for secrets than Makefile parsing, because API keys may contain characters that have special meaning to `make`.

## Files

- `.env-example`: tracked example with safe placeholder values.
- `.env`: local-only environment file, ignored by `.gitignore`.

Create the local file with:

```sh
cp .env-example .env
```

Then edit `.env` for the current machine and OpenWrt target.

## Loading Variables

Use POSIX shell syntax in `.env`:

```sh
EDGEPULSE_OPENWRT_SSH_TARGET=one
EDGEPULSE_AI_MODEL=qwen/qwen3.6-35b-a3b
EDGEPULSE_AI_MAX_TOKENS=2048
```

Before running validation commands:

```sh
set -a
. ./.env
set +a
```

After that, tests and helper scripts can read values from the process environment.

The OpenWrt end-to-end helper also loads `.env` automatically when the file exists:

```sh
make openwrt-agent-e2e
```

Set `EDGEPULSE_SKIP_DOTENV=1` to force the helper to use only the current process environment.

## Variable Groups

### OpenWrt Target

| Variable | Purpose | Example |
| --- | --- | --- |
| `EDGEPULSE_OPENWRT_SSH_TARGET` | SSH target or alias for the router. | `one` |
| `EDGEPULSE_OPENWRT_SSH_PORT` | Optional SSH port. | `22` |
| `EDGEPULSE_OPENWRT_SSH_OPTS` | Optional extra SSH flags for local scripts. | `-o ConnectTimeout=10` |

### Package Artifacts

| Variable | Purpose |
| --- | --- |
| `EDGEPULSE_OPENWRT_EDGE_PACKAGE` | Local path to the built `edgepulse` APK/IPK. |
| `EDGEPULSE_OPENWRT_LUCI_PACKAGE` | Local path to the built `luci-app-edgepulse` APK/IPK. |

### AI Agent Runtime

| Variable | Purpose | Current OpenWrt One reference |
| --- | --- | --- |
| `EDGEPULSE_AI_AGENT_ENABLED` | Desired `edgepulse.agent.enabled` value. | `1` |
| `EDGEPULSE_AI_AGENT_LOCAL_ONLY` | Desired `edgepulse.agent.local_only` value. | `0` |
| `EDGEPULSE_AI_MODEL_SECTION` | UCI model section name. | `remote_reasoner` |
| `EDGEPULSE_AI_BASE_URL` | OpenAI-compatible base URL. | private lab value |
| `EDGEPULSE_AI_MODEL` | Model name sent to the provider. | `qwen/qwen3.6-35b-a3b` |
| `EDGEPULSE_AI_API_KEY` | Provider API key. Keep only in `.env`. | not tracked |
| `EDGEPULSE_AI_API_KEY_ENV` | Environment variable name used by runtime config. | `EDGEPULSE_AI_API_KEY` |
| `EDGEPULSE_AI_TIMEOUT_SEC` | Model request timeout. | `60` |
| `EDGEPULSE_AI_RETRY_COUNT` | Retry count. | `0` |
| `EDGEPULSE_AI_MAX_TOKENS` | Model response token budget. | `2048` |
| `EDGEPULSE_AI_NO_THINK` | Whether to request no-think mode. | `0` |

### Local Tests

| Variable | Purpose | Default |
| --- | --- | --- |
| `EDGEPULSE_AGENT_TEST_PORT` | Mock OpenAI server port used by `make integration-agent-model`. | `18181` |

### End-to-End Prompts

| Variable | Purpose |
| --- | --- |
| `EDGEPULSE_AGENT_E2E_PROMPT` | Prompt used by OpenWrt end-to-end model validation. |
| `EDGEPULSE_AGENT_E2E_EXPECT_FINISH_REASON` | Expected `finish_reason`, usually `stop`. |

## Example OpenWrt Validation Flow

Load the environment:

```sh
set -a
. ./.env
set +a
```

Deploy packages:

```sh
scp "$EDGEPULSE_OPENWRT_EDGE_PACKAGE" "$EDGEPULSE_OPENWRT_SSH_TARGET:/tmp/edgepulse.apk"
scp "$EDGEPULSE_OPENWRT_LUCI_PACKAGE" "$EDGEPULSE_OPENWRT_SSH_TARGET:/tmp/luci-app-edgepulse.apk"
ssh "$EDGEPULSE_OPENWRT_SSH_TARGET" 'apk add --allow-untrusted /tmp/edgepulse.apk /tmp/luci-app-edgepulse.apk'
```

Apply model settings:

```sh
ssh "$EDGEPULSE_OPENWRT_SSH_TARGET" "
uci set edgepulse.agent.enabled='$EDGEPULSE_AI_AGENT_ENABLED'
uci set edgepulse.agent.local_only='$EDGEPULSE_AI_AGENT_LOCAL_ONLY'
uci set edgepulse.$EDGEPULSE_AI_MODEL_SECTION.enabled='1'
uci set edgepulse.$EDGEPULSE_AI_MODEL_SECTION.base_url='$EDGEPULSE_AI_BASE_URL'
uci set edgepulse.$EDGEPULSE_AI_MODEL_SECTION.model='$EDGEPULSE_AI_MODEL'
uci set edgepulse.$EDGEPULSE_AI_MODEL_SECTION.api_key='$EDGEPULSE_AI_API_KEY'
uci set edgepulse.$EDGEPULSE_AI_MODEL_SECTION.api_key_env='$EDGEPULSE_AI_API_KEY_ENV'
uci set edgepulse.$EDGEPULSE_AI_MODEL_SECTION.timeout_sec='$EDGEPULSE_AI_TIMEOUT_SEC'
uci set edgepulse.$EDGEPULSE_AI_MODEL_SECTION.retry_count='$EDGEPULSE_AI_RETRY_COUNT'
uci set edgepulse.$EDGEPULSE_AI_MODEL_SECTION.max_tokens='$EDGEPULSE_AI_MAX_TOKENS'
uci set edgepulse.$EDGEPULSE_AI_MODEL_SECTION.no_think='$EDGEPULSE_AI_NO_THINK'
uci commit edgepulse
/etc/init.d/edgepulse restart
"
```

Run a diagnostic:

```sh
ssh "$EDGEPULSE_OPENWRT_SSH_TARGET" "edgepulse-ctl agent ask '$EDGEPULSE_AGENT_E2E_PROMPT'"
```

Or use the repository helper:

```sh
make openwrt-agent-e2e
```

To have the helper apply model settings from the environment before running the diagnostic:

```sh
EDGEPULSE_E2E_APPLY_CONFIG=1 make openwrt-agent-e2e
```

## Unit, Integration, and End-to-End Boundaries

Unit tests should avoid real OpenWrt devices and real providers. They can use temporary config files and fake values from the environment only when that helps cover parsing.

Integration tests should default to local mock servers. `make integration-agent-model` is the current example; it reads `EDGEPULSE_AGENT_TEST_PORT`.

End-to-end tests may use `EDGEPULSE_OPENWRT_SSH_TARGET` and real model settings. They should be opt-in because they depend on hardware, network, and secrets.

## Safety Notes

- Never commit `.env`.
- Prefer fake API keys for local mock integration tests.
- Redact command output before pasting it into issues or docs.
- If a model endpoint behaves differently under `no_think`, record the result in [AI Agent OpenWrt Model Validation Use Cases](ai-agent-openwrt-model-validation-use-cases.md).
