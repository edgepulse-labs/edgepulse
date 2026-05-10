# OpenWrt Feeds 與 Repository 規劃

Review 日期：2026-05-07

## 目標

定義 EdgePulse 應如何整合進 OpenWrt，同時避免把專案 source code 直接混入 OpenWrt buildroot。

建議做法：

- 將 EdgePulse source 與 documentation 保留在主要 project repository。
- 建立專用的 OpenWrt feed repository 來放 package recipes。
- 讓 OpenWrt 透過 `feeds.conf` consume 這個 package feed。

這符合 OpenWrt feed workflow：feed 是一組 package build recipes，可以從 git 取得、從 local path link，或由其他支援的來源提供。

官方參考：

- OpenWrt feeds documentation: https://openwrt.org/docs/guide-developer/feeds

## 建議的 Repository 拆分

MVP 使用兩個 repositories。

```text
edgepulse
  README.md
  docs/
  src/
    edgepulse-daemon/
    edgepulse-ctl/
  include/
  tests/
  packaging/
```

```text
edgepulse-openwrt-feed
  edgepulse/
    Makefile
    files/etc/config/edgepulse
    files/etc/init.d/edgepulse
  luci-app-edgepulse/
    Makefile
    root/usr/share/luci/menu.d/luci-app-edgepulse.json
    root/usr/share/rpcd/acl.d/luci-app-edgepulse.json
    htdocs/luci-static/resources/view/edgepulse/
      overview.js
      metrics.js
      features.js
      settings.js
```

## 為什麼使用兩個 Repositories

主要 `edgepulse` repository 應負責 product direction、C source code、tests、docs 與非 OpenWrt 專用 tooling。

`edgepulse-openwrt-feed` repository 應負責 OpenWrt package recipes 與 LuCI package layout。這樣可以讓 OpenWrt integration 保持乾淨，也讓 OpenWrt buildroot 使用者只用一行 feed 就能加入 EdgePulse。

MVP 階段避免拆成太多 repositories。像 `edgepulse-core`、`edgepulse-luci`、`edgepulse-training-tools`、`edgepulse-models` 這些 repositories 未來可能有用，但太早引入會增加 release 與 dependency 負擔。

## Feed Configuration

Remote git feed：

```text
src-git edgepulse https://github.com/Pod-01-Nier/edgepulse-openwrt-feed.git
```

Local package development：

```text
src-link edgepulse /path/to/edgepulse-openwrt-feed
```

Branch-specific feed：

```text
src-git edgepulse https://github.com/Pod-01-Nier/edgepulse-openwrt-feed.git;main
```

Pinned feed commit：

```text
src-git edgepulse https://github.com/Pod-01-Nier/edgepulse-openwrt-feed.git^<commit-hash>
```

## OpenWrt Build Workflow

在 OpenWrt buildroot 內：

```sh
cp feeds.conf.default feeds.conf
```

把上面其中一行 EdgePulse feed 加入 `feeds.conf`。

接著 update 並 install feed：

```sh
./scripts/feeds update edgepulse
./scripts/feeds install -a -p edgepulse
make menuconfig
```

安裝後，feed 中的 packages 會 link 到：

```text
package/feeds/edgepulse/
```

預期 package targets：

```text
package/feeds/edgepulse/edgepulse
package/feeds/edgepulse/luci-app-edgepulse
```

開發時只 build EdgePulse packages：

```sh
make package/feeds/edgepulse/edgepulse/compile V=s
make package/feeds/edgepulse/luci-app-edgepulse/compile V=s
```

可選的 AI agent package configuration：

```text
CONFIG_PACKAGE_edgepulse=m
CONFIG_PACKAGE_luci-app-edgepulse=m
CONFIG_EDGEPULSE_ENABLE_AI_AGENT=y
CONFIG_EDGEPULSE_AI_DEFAULT_BASE_URL=""
CONFIG_EDGEPULSE_AI_DEFAULT_MODEL=""
```

AI agent runtime 預設仍是停用。使用 remote model routing 前，需在 `/etc/config/edgepulse` 啟用 `config agent 'agent'` 並設定 `config model` section。不要把真正的 API keys 放進 firmware images；優先使用 runtime UCI 或 `api_key_env`。

## Package Split

### `edgepulse`

Core runtime package。

Responsibilities:

- C daemon。
- SQLite storage under `/tmp/edgepulse/edgepulse.db`。
- UCI config at `/etc/config/edgepulse`。
- init script at `/etc/init.d/edgepulse`。
- 用於 JSON status 與 CSV export 的 CLI 或 helper command。

Initial dependencies:

- `libsqlite3`
- `libubox`
- `libubus`
- `libblobmsg-json`
- `ubus`
- `uclient-fetch`
- `libustream-mbedtls`

### `luci-app-edgepulse`

LuCI web package。

Responsibilities:

- Overview page。
- Raw metrics page。
- Feature windows page。
- Settings page backed by UCI。
- rpcd ACL，安全暴露 read/status/export/settings actions。

Initial dependencies:

- `edgepulse`
- `luci-base`
- `rpcd`
- 依選定的 LuCI pattern，使用 `ucode` 或 LuCI JavaScript APIs。

## Source Version Strategy

早期 local development：

- 對 feed 使用 `src-link`。
- API 快速變動時，讓 feed package 指向 local 或 branch source。

Reproducible CI 或 release builds：

- 使用 `PKG_SOURCE_PROTO:=git`。
- 將 `PKG_SOURCE_VERSION` pin 到主要 `edgepulse` repository 的 commit hash。
- 當 OpenWrt packaging 改變但 source 未變時，更新 `PKG_RELEASE`。
- Release 時同時 tag 兩個 repositories。

OpenWrt package recipe 中的 source section 範例：

```make
PKG_NAME:=edgepulse
PKG_RELEASE:=1

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/Pod-01-Nier/edgepulse.git
PKG_SOURCE_VERSION:=<edgepulse-commit-hash>
PKG_MIRROR_HASH:=skip
```

穩定 release 時，把 `PKG_MIRROR_HASH:=skip` 換成真正的 hash。

## MVP Repo Policy

在第一個 device demo 成功之前，使用這個結構：

- 一個 source repository：`edgepulse`。
- 一個 OpenWrt feed repository：`edgepulse-openwrt-feed`。
- 尚不建立獨立 model repository。
- 尚不建立獨立 dataset repository。
- 尚不建立獨立 LuCI repository。

只有在出現具體邊界時才增加 repositories：

- 大型 training datasets。
- Model release artifacts。
- Cross-device training tools。
- 具有 signed release artifacts 的 public package feed。

## Release Flow

1. 在 `edgepulse` 實作並測試 C source。
2. Commit source changes。
3. 更新 `edgepulse-openwrt-feed/edgepulse/Makefile`，使它指向 source commit。
4. 使用 OpenWrt SDK 或 buildroot build。
5. 在 OpenWrt One 上測試。
6. Tag source repository。
7. Tag feed repository。
8. Packaging 穩定後再 publish package artifacts。

## MVP Acceptance

Feed plan 可接受的條件：

- OpenWrt 可以用一行 `feeds.conf` 加入 EdgePulse。
- `./scripts/feeds update edgepulse` 成功。
- `./scripts/feeds install -a -p edgepulse` exposes `edgepulse` 與 `luci-app-edgepulse`。
- `make menuconfig` 可以選取兩個 packages。
- Package build 不需要修改 OpenWrt core files。
