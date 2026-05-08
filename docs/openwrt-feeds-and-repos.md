# OpenWrt Feeds And Repository Plan

Review date: 2026-05-07

## Goal

Define how EdgePulse should be integrated into OpenWrt without mixing project source code directly into the OpenWrt buildroot.

The recommended approach is:

- Keep EdgePulse source and documentation in the main project repository.
- Create a dedicated OpenWrt feed repository for package recipes.
- Let OpenWrt consume the package feed through `feeds.conf`.

This matches the OpenWrt feed workflow, where a feed is a collection of package build recipes that can be pulled from git, linked from a local path, or provided through another supported source.

Official reference:

- OpenWrt feeds documentation: https://openwrt.org/docs/guide-developer/feeds

## Recommended Repository Split

Use two repositories for the MVP.

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

## Why Two Repositories

The main `edgepulse` repository should own product direction, C source code, tests, docs, and non-OpenWrt-specific tooling.

The `edgepulse-openwrt-feed` repository should own OpenWrt package recipes and LuCI package layout. This keeps the OpenWrt integration clean and allows OpenWrt buildroot users to add EdgePulse through a single feed line.

Avoid splitting into many repositories during MVP. Repositories such as `edgepulse-core`, `edgepulse-luci`, `edgepulse-training-tools`, and `edgepulse-models` may become useful later, but they add release and dependency overhead too early.

## Feed Configuration

For a remote git feed:

```text
src-git edgepulse https://github.com/Pod-01-Nier/edgepulse-openwrt-feed.git
```

For local package development:

```text
src-link edgepulse /path/to/edgepulse-openwrt-feed
```

For a branch-specific feed:

```text
src-git edgepulse https://github.com/Pod-01-Nier/edgepulse-openwrt-feed.git;main
```

For a pinned feed commit:

```text
src-git edgepulse https://github.com/Pod-01-Nier/edgepulse-openwrt-feed.git^<commit-hash>
```

## OpenWrt Build Workflow

Inside the OpenWrt buildroot:

```sh
cp feeds.conf.default feeds.conf
```

Add one of the EdgePulse feed lines above to `feeds.conf`.

Then update and install the feed:

```sh
./scripts/feeds update edgepulse
./scripts/feeds install -a -p edgepulse
make menuconfig
```

After installation, packages from the feed are linked under:

```text
package/feeds/edgepulse/
```

Expected package targets:

```text
package/feeds/edgepulse/edgepulse
package/feeds/edgepulse/luci-app-edgepulse
```

Build only EdgePulse packages during development:

```sh
make package/feeds/edgepulse/edgepulse/compile V=s
make package/feeds/edgepulse/luci-app-edgepulse/compile V=s
```

## Package Split

### `edgepulse`

Core runtime package.

Responsibilities:

- C daemon.
- SQLite storage under `/tmp/edgepulse/edgepulse.db`.
- UCI config at `/etc/config/edgepulse`.
- init script at `/etc/init.d/edgepulse`.
- CLI or helper command for JSON status and CSV export.

Initial dependencies:

- `libsqlite3`
- `libubox`
- `libubus`
- `libblobmsg-json`

### `luci-app-edgepulse`

LuCI web package.

Responsibilities:

- Overview page.
- Raw metrics page.
- Feature windows page.
- Settings page backed by UCI.
- rpcd ACL for safe read/status/export/settings actions.

Initial dependencies:

- `edgepulse`
- `luci-base`
- `rpcd`
- `ucode` or LuCI JavaScript APIs as required by the chosen LuCI pattern.

## Source Version Strategy

During early local development:

- Use `src-link` for the feed.
- Let the feed package point to local or branch source while APIs are moving quickly.

For reproducible CI or release builds:

- Use `PKG_SOURCE_PROTO:=git`.
- Pin `PKG_SOURCE_VERSION` to a commit hash from the main `edgepulse` repository.
- Update `PKG_RELEASE` when the OpenWrt packaging changes without source changes.
- Tag both repositories for releases.

Example source section in the OpenWrt package recipe:

```make
PKG_NAME:=edgepulse
PKG_RELEASE:=1

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/Pod-01-Nier/edgepulse.git
PKG_SOURCE_VERSION:=<edgepulse-commit-hash>
PKG_MIRROR_HASH:=skip
```

Replace `PKG_MIRROR_HASH:=skip` with a real hash for stable releases.

## MVP Repo Policy

Use this structure until the first device demo works:

- One source repository: `edgepulse`.
- One OpenWrt feed repository: `edgepulse-openwrt-feed`.
- No separate model repository yet.
- No separate dataset repository yet.
- No separate LuCI repository yet.

Add more repositories only when there is a concrete boundary:

- Large training datasets.
- Model release artifacts.
- Cross-device training tools.
- Public package feed with signed release artifacts.

## Release Flow

1. Implement and test C source in `edgepulse`.
2. Commit source changes.
3. Update `edgepulse-openwrt-feed/edgepulse/Makefile` to point at the source commit.
4. Build with OpenWrt SDK or buildroot.
5. Test on OpenWrt One.
6. Tag the source repository.
7. Tag the feed repository.
8. Publish package artifacts later when the packaging is stable.

## MVP Acceptance

The feed plan is acceptable when:

- OpenWrt can add EdgePulse with one `feeds.conf` line.
- `./scripts/feeds update edgepulse` succeeds.
- `./scripts/feeds install -a -p edgepulse` exposes `edgepulse` and `luci-app-edgepulse`.
- `make menuconfig` can select both packages.
- The package build does not require editing OpenWrt core files.

