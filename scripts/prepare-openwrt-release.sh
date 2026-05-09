#!/bin/sh

set -eu

version="${1:-}"
release="${2:-}"
out_dir="${3:-dist}"

if [ -z "$version" ] || [ -z "$release" ]; then
	echo "Usage: $0 <version> <pkg_release> [out_dir]" >&2
	exit 2
fi

case "$version" in
	*[!0-9A-Za-z._-]*|'')
		echo "invalid version: $version" >&2
		exit 2
		;;
esac

case "$release" in
	*[!0-9]*|'')
		echo "invalid PKG_RELEASE: $release" >&2
		exit 2
		;;
esac

sed -i "s/^#define EDGEPULSE_VERSION .*/#define EDGEPULSE_VERSION \"$version\"/" include/edgepulse.h
sed -i "s/^PKG_RELEASE:=.*/PKG_RELEASE:=$release/" packaging/openwrt-feed/edgepulse/Makefile
sed -i "s/^PKG_RELEASE:=.*/PKG_RELEASE:=$release/" packaging/openwrt-feed/luci-app-edgepulse/Makefile

mkdir -p "$out_dir"
git archive --format=tar --prefix="edgepulse-$version/" HEAD | gzip -n > "$out_dir/edgepulse-$version.tar.gz"

printf '%s\n' "Prepared EdgePulse $version with PKG_RELEASE=$release"
printf '%s\n' "Source archive: $out_dir/edgepulse-$version.tar.gz"
