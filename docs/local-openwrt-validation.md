# Local OpenWrt Package Validation

This document records how to validate the `edgepulse` package with a local OpenWrt buildroot. The flow is useful when changing C code, package metadata, init scripts, or local feed integration before publishing source changes.

## Validated Setup

- OpenWrt checkout: `/home/nier/workspace/openwrt-build`
- Source mirror: `https://github.com/openwrt/openwrt.git`
- Feed repository: `https://github.com/edgepulse-labs/edgepulse-openwrt-feed`
- Feed checkout: `/home/nier/workspace/edgepulse-openwrt-feed`
- Local source checkout: `/home/nier/workspace/edgepulse`
- Target profile: OpenWrt One, `mediatek/filogic`
- Package artifact: `/home/nier/workspace/openwrt-build/bin/packages/aarch64_cortex-a53/edgepulse/edgepulse-1.apk`

The local package build was validated against an OpenWrt `main` checkout cloned from the GitHub mirror.

## One-Time OpenWrt Checkout

Clone OpenWrt from the GitHub mirror:

```sh
git clone --depth 1 https://github.com/openwrt/openwrt.git /home/nier/workspace/openwrt-build
cd /home/nier/workspace/openwrt-build
```

Configure the local EdgePulse feed by adding this line to `feeds.conf`:

```text
src-link edgepulse /home/nier/workspace/edgepulse-openwrt-feed
```

Update and install the feed packages:

```sh
./scripts/feeds update edgepulse
./scripts/feeds update luci
./scripts/feeds update packages
./scripts/feeds install -a -p edgepulse
./scripts/feeds install libsqlite3 luci-base rpcd csstidy luasrcdiet
```

Configure the OpenWrt One target and EdgePulse packages:

```text
CONFIG_TARGET_mediatek=y
CONFIG_TARGET_mediatek_filogic=y
CONFIG_TARGET_mediatek_filogic_DEVICE_openwrt_one=y
CONFIG_PACKAGE_edgepulse=m
CONFIG_PACKAGE_luci-app-edgepulse=m
```

Then run:

```sh
make defconfig
```

## Build Prerequisites

A fresh OpenWrt checkout may need host tools and the target toolchain before the package can build:

```sh
make tools/libdeflate/compile V=s
make tools/sed/compile V=s
make tools/meson/compile V=s
make tools/libressl/compile V=s
make toolchain/install -j$(nproc) V=s
```

The target toolchain step is required if the build fails while packaging `package/libs/toolchain` with missing `libgcc_s.so.*`.

## Validate EdgePulse

Use the local checkout as the package source:

```sh
cd /home/nier/workspace/openwrt-build
make package/feeds/edgepulse/edgepulse/clean V=s
make package/feeds/edgepulse/edgepulse/compile V=s EDGEPULSE_LOCAL_SOURCE=/home/nier/workspace/edgepulse
```

Expected artifact:

```text
/home/nier/workspace/openwrt-build/bin/packages/aarch64_cortex-a53/edgepulse/edgepulse-1.apk
```

Confirm the built binary targets OpenWrt/musl instead of the host system:

```sh
file /home/nier/workspace/openwrt-build/build_dir/target-aarch64_cortex-a53_musl/edgepulse/edgepulse
/home/nier/workspace/openwrt-build/staging_dir/toolchain-aarch64_cortex-a53_gcc-14.3.0_musl/bin/aarch64-openwrt-linux-musl-readelf -d \
  /home/nier/workspace/openwrt-build/build_dir/target-aarch64_cortex-a53_musl/edgepulse/edgepulse
```

Expected indicators:

```text
ELF 64-bit LSB executable, ARM aarch64
interpreter /lib/ld-musl-aarch64.so.1
NEEDED Shared library: [libc.so]
```

## Local Source Package Notes

The EdgePulse OpenWrt package supports local source builds through `EDGEPULSE_LOCAL_SOURCE`. In local mode, the package Makefile should avoid setting `PKG_SOURCE_URL`; otherwise OpenWrt may still enter the download pipeline and fail with:

```text
Download/default is missing the FILE field.
```

The local prepare step should also clean copied build outputs:

```make
define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) $(EDGEPULSE_LOCAL_SOURCE)/. $(PKG_BUILD_DIR)/
	$(MAKE) -C $(PKG_BUILD_DIR) clean
endef
```

This prevents a previously built host binary from being packaged. If a host binary is accidentally packaged, OpenWrt reports missing host libraries such as:

```text
Package edgepulse is missing dependencies for the following libraries:
libc.so.6
```

## Quick Rebuild Loop

After editing `edgepulse` source files:

```sh
cd /home/nier/workspace/openwrt-build
make package/feeds/edgepulse/edgepulse/clean V=s
make package/feeds/edgepulse/edgepulse/compile V=s EDGEPULSE_LOCAL_SOURCE=/home/nier/workspace/edgepulse
```

If only package metadata changed in `/home/nier/workspace/edgepulse-openwrt-feed`, rerun the same clean and compile commands so OpenWrt refreshes package staging metadata.
