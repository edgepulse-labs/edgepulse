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
