#!/bin/sh
# Structural check of the Milestone 2 closure section (AC1-AC4).
set -eu
f=${1:-docs/ACCEPTANCE.md}
fail() { echo "m2 closure: $*" >&2; exit 1; }
s=$(sed -n '/^## Milestone 2 closure/,$p' "$f")
[ -n "$s" ] || fail "missing section"
for i in 1 2 3 4 5 6 7 8 9 10; do
	printf '%s\n' "$s" | grep -q "S$i\\b" || fail "slice S$i absent"
done
for id in R1.12.7 R6.4.1 R1.12.9a R6.3.3a R1.8.7; do
	printf '%s\n' "$s" | grep -i 'deferred' | grep "$id" | grep -Eq 'M[345]' || fail "deferred $id missing"
done
printf '%s\n' "$s" | grep -q '^### User acceptance checklist' || fail "no checklist"
printf '%s\n' "$s" | grep -q '\*\*Verified\*\*' && fail "unexpected verified disposition"
exit 0
