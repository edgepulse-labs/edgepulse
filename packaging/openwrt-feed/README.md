# EdgePulse OpenWrt Feed

This directory is a pointer to the standalone OpenWrt feed repository. It
intentionally does not contain package recipe copies.

The source of truth for OpenWrt package metadata is:

```text
https://github.com/edgepulse-labs/edgepulse-openwrt-feed
```

For local development, this is usually checked out next to this repository as
`../edgepulse-openwrt-feed/`.

Ownership split:

- This repository owns the EdgePulse source code, tests, scripts, and docs.
- `edgepulse-openwrt-feed` owns the OpenWrt `edgepulse` package recipe, LuCI
  package recipe, default UCI config, init scripts, uci-defaults, and LuCI
  application files.

Use it from an OpenWrt buildroot with a local feed line:

```text
src-link edgepulse /path/to/edgepulse-openwrt-feed
```

Then update and install the feed:

```sh
./scripts/feeds update edgepulse
./scripts/feeds install -a -p edgepulse
```

For local source development, build the package with:

```sh
make package/feeds/edgepulse/edgepulse/compile V=s EDGEPULSE_LOCAL_SOURCE=/path/to/edgepulse
```

## AI Agent Build Option

The EdgePulse AI agent MVP is optional in OpenWrt package builds. Configure the
package symbols in the standalone feed repository and OpenWrt buildroot, not in
this directory. To include it in an image or module build, enable the package
symbol and the agent feature symbol:

```text
CONFIG_PACKAGE_edgepulse=m
CONFIG_PACKAGE_luci-app-edgepulse=m
CONFIG_EDGEPULSE_ENABLE_AI_AGENT=y
CONFIG_EDGEPULSE_AI_DEFAULT_BASE_URL=""
CONFIG_EDGEPULSE_AI_DEFAULT_MODEL=""
```

Runtime use is still controlled by `/etc/config/edgepulse`; the default package config keeps `config agent 'agent'` disabled. Set `agent.enabled=1` and configure a `config model` section before enabling remote model routing. Keep real API keys out of firmware images and prefer runtime UCI or `api_key_env`.
