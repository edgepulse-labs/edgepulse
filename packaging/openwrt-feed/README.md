# EdgePulse OpenWrt Feed

This directory is a development copy of the OpenWrt feed layout planned for the standalone `edgepulse-openwrt-feed` repository.

Use it from an OpenWrt buildroot with a local feed line:

```text
src-link edgepulse /path/to/edgepulse/packaging/openwrt-feed
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

The EdgePulse AI agent MVP is optional in OpenWrt package builds. To include it in an image or module build, enable the package symbol and the agent feature symbol:

```text
CONFIG_PACKAGE_edgepulse=m
CONFIG_PACKAGE_luci-app-edgepulse=m
CONFIG_EDGEPULSE_ENABLE_AI_AGENT=y
CONFIG_EDGEPULSE_AI_DEFAULT_BASE_URL=""
CONFIG_EDGEPULSE_AI_DEFAULT_MODEL=""
```

Runtime use is still controlled by `/etc/config/edgepulse`; the default package config keeps `config agent 'main'` disabled. Set `agent.enabled=1` and configure a `config model` section before enabling remote model routing. Keep real API keys out of firmware images and prefer runtime UCI or `api_key_env`.
