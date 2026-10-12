#!/bin/sh
# Runs the M2 demo driver and checks its report (AC8-AC11). Usage: test_m2_demo.sh DRIVER
driver=${1:?usage: test_m2_demo.sh DRIVER}
out=$("$driver" tests/fixtures/vaults/m2-render scripts/m2-demo.sh 2>&1) || { echo "$out"; echo "FAIL: driver exit"; exit 1; }
fail() { echo "$out"; echo "FAIL: $1"; exit 1; }
root=$(printf '%s\n' "$out" | sed -n 's/^fixture vault: //p')
[ -n "$root" ] || fail "no vault root printed"
[ ! -e "$root" ] || fail "root not removed: $root"
printf '%s\n' "$out" | grep -q 'found: lualatex\|missing: lualatex' || fail "no probe output"
while IFS='|' read -r id slice _; do
	printf '%s\n' "$out" | grep -q "pending - owner $slice: $id:" || fail "step $id not pending with $slice"
done < scripts/m2-demo-steps.txt
echo "m2 demo test ok"
