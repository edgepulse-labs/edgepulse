#!/bin/sh
set -eu

fail() {
	echo "FAIL: $*" >&2
	exit 1
}

if rg -n '\b(system|popen)\s*\(' src include; then
	fail "forbidden process API found; use fixed argv exec paths behind allowlists"
fi

if rg -n '\beval\b' src include; then
	fail "forbidden eval token found in source"
fi

echo "edgepulse agent safety checks passed"
