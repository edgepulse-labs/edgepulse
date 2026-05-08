# 本地 OpenWrt Package 驗證

這份文件記錄如何使用本地 OpenWrt buildroot 驗證 `edgepulse` package。修改 C 程式、package metadata、init script，或 local feed 整合方式後，都可以用這個流程在發布前先確認 package 能被 OpenWrt 正常編譯與打包。

## 已驗證環境

- OpenWrt checkout：`/home/nier/workspace/openwrt-build`
- Source mirror：`https://github.com/openwrt/openwrt.git`
- Feed checkout：`/home/nier/workspace/edgepulse-openwrt-feed`
- Local source checkout：`/home/nier/workspace/edgepulse`
- Target profile：OpenWrt One，`mediatek/filogic`
- Package artifact：`/home/nier/workspace/openwrt-build/bin/packages/aarch64_cortex-a53/edgepulse/edgepulse-1.apk`

這次驗證使用的是從 GitHub mirror clone 下來的 OpenWrt `main` checkout。

## 一次性的 OpenWrt Checkout

從 GitHub mirror clone OpenWrt：

```sh
git clone --depth 1 https://github.com/openwrt/openwrt.git /home/nier/workspace/openwrt-build
cd /home/nier/workspace/openwrt-build
```

在 `feeds.conf` 加入本地 EdgePulse feed：

```text
src-link edgepulse /home/nier/workspace/edgepulse-openwrt-feed
```

更新並安裝 feed packages：

```sh
./scripts/feeds update edgepulse
./scripts/feeds update luci
./scripts/feeds update packages
./scripts/feeds install -a -p edgepulse
./scripts/feeds install libsqlite3 luci-base rpcd csstidy luasrcdiet
```

設定 OpenWrt One target 與 EdgePulse packages：

```text
CONFIG_TARGET_mediatek=y
CONFIG_TARGET_mediatek_filogic=y
CONFIG_TARGET_mediatek_filogic_DEVICE_openwrt_one=y
CONFIG_PACKAGE_edgepulse=m
CONFIG_PACKAGE_luci-app-edgepulse=m
```

接著執行：

```sh
make defconfig
```

## Build 前置項目

全新的 OpenWrt checkout 可能需要先編 host tools 與 target toolchain：

```sh
make tools/libdeflate/compile V=s
make tools/sed/compile V=s
make tools/meson/compile V=s
make tools/libressl/compile V=s
make toolchain/install -j$(nproc) V=s
```

如果 package build 在 `package/libs/toolchain` 階段因為找不到 `libgcc_s.so.*` 失敗，就需要先跑完 target toolchain。

## 驗證 EdgePulse

使用本地 checkout 當作 package source：

```sh
cd /home/nier/workspace/openwrt-build
make package/feeds/edgepulse/edgepulse/clean V=s
make package/feeds/edgepulse/edgepulse/compile V=s EDGEPULSE_LOCAL_SOURCE=/home/nier/workspace/edgepulse
```

預期產物：

```text
/home/nier/workspace/openwrt-build/bin/packages/aarch64_cortex-a53/edgepulse/edgepulse-1.apk
```

確認編出來的是 OpenWrt/musl 目標 binary，而不是本機 host binary：

```sh
file /home/nier/workspace/openwrt-build/build_dir/target-aarch64_cortex-a53_musl/edgepulse/edgepulse
/home/nier/workspace/openwrt-build/staging_dir/toolchain-aarch64_cortex-a53_gcc-14.3.0_musl/bin/aarch64-openwrt-linux-musl-readelf -d \
  /home/nier/workspace/openwrt-build/build_dir/target-aarch64_cortex-a53_musl/edgepulse/edgepulse
```

預期會看到：

```text
ELF 64-bit LSB executable, ARM aarch64
interpreter /lib/ld-musl-aarch64.so.1
NEEDED Shared library: [libc.so]
```

## Local Source Package 注意事項

EdgePulse OpenWrt package 透過 `EDGEPULSE_LOCAL_SOURCE` 支援本地 source build。使用 local mode 時，package Makefile 不應設定 `PKG_SOURCE_URL`；否則 OpenWrt 仍可能進入 download pipeline，並出現：

```text
Download/default is missing the FILE field.
```

local prepare step 也要清掉被複製進 build dir 的舊 build output：

```make
define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) $(EDGEPULSE_LOCAL_SOURCE)/. $(PKG_BUILD_DIR)/
	$(MAKE) -C $(PKG_BUILD_DIR) clean
endef
```

這可以避免把先前在 host 上編出來的 binary 包進 OpenWrt package。若不小心打包到 host binary，OpenWrt 會回報缺少 host library，例如：

```text
Package edgepulse is missing dependencies for the following libraries:
libc.so.6
```

## 快速重編流程

修改 `edgepulse` source files 後：

```sh
cd /home/nier/workspace/openwrt-build
make package/feeds/edgepulse/edgepulse/clean V=s
make package/feeds/edgepulse/edgepulse/compile V=s EDGEPULSE_LOCAL_SOURCE=/home/nier/workspace/edgepulse
```

如果只修改 `/home/nier/workspace/edgepulse-openwrt-feed` 裡的 package metadata，也使用同一組 clean 與 compile 命令，讓 OpenWrt 重新整理 package staging metadata。
