#!/bin/sh
# Gap checks for M2 slice S1 (AC2, AC10, AC16-AC19, AC21). Usage: check_s1_gaps.sh
set -eu
fail() { echo "s1 gaps: $*" >&2; exit 1; }
adr=$(ls ai-factory/adr/0003-*.md) || fail "AC16 no ADR 0003"
# AC16/17/18: ADR content
grep -q -- '-no-shell-escape' "$adr" || fail "AC16 ADR lacks -no-shell-escape"
grep -qi 'never linked\|not linked\|subprocess' "$adr" || fail "AC16 ADR lacks subprocess statement"
for p in FreeBSD Arch Homebrew; do grep -q "^| $p" "$adr" || fail "AC17 matrix lacks $p"; done
grep -Eq '20[0-9]{2}-[0-9]{2}-[0-9]{2}' "$adr" || fail "AC17 no revalidation date"
grep -q 'AUR' "$adr" || fail "AC17 no AUR statement"
grep -q '^## CI subset' "$adr" || fail "AC18 no CI subset"
# AC19: CI installs poppler on all jobs, requires TeX, caches macOS TeX keyed on ci.yml
ci=.github/workflows/ci.yml
[ "$(grep -c 'CWIKI_REQUIRE_TEX' $ci)" -ge 3 ] || fail "AC19 CWIKI_REQUIRE_TEX not in every job"
[ "$(grep -c 'poppler' $ci)" -ge 3 ] || fail "AC19 poppler not installed on every job"
grep -q "hashFiles('.github/workflows/ci.yml')" $ci || fail "AC19 cache not keyed on ci.yml"
# AC2: 28 M2 FEATURES rows, all named in the closure rows table
exp=$(grep -c '^|[^|]*|[^|]*|[^|]*| M2 |' docs/FEATURES.md)
got=$(sed -n '/^### FEATURES rows assigned to M2/,/^### Deferred/p' docs/ACCEPTANCE.md |
	grep '^| S[0-9]' | cut -d'|' -f3 | tr ';' '\n' | grep -c '[A-Za-z]')
[ "$exp" = "$got" ] || fail "AC2 FEATURES M2 rows $exp but closure names $got"
# AC3: deferred ids never appear in an open scope row
sed -n '/^### Scope items/,/^### FEATURES/p' docs/ACCEPTANCE.md | grep -E 'R1\.12\.7|R6\.4\.1|R1\.12\.9a|R1\.8\.7' && fail "AC3 deferred id in scope row"
# AC10: no GNU make extensions in the new targets / POSIX sh scripts
if grep -nE '\$\((shell|wildcard|patsubst|filter|foreach|if) |:=|\+=|\?=' Makefile | grep -i 'm2\|tex'; then fail "AC10 GNU make construct"; fi
grep -q '^demo:.*m2-demo' Makefile || fail "AC10 demo lacks m2-demo"
for s in scripts/tex-probe.sh scripts/m2-demo.sh; do
	[ "$(head -n1 $s)" = '#!/bin/sh' ] || fail "AC10 $s not /bin/sh"
	grep -nE '\[\[|\bfunction\b|<<<|\blocal\b' $s && fail "AC10 bashism in $s"
done
# AC21: no product or approved-evidence change since the spec commit
git diff --quiet 152787c -- src tests/snapshots tests/fixtures/keys || fail "AC21 src/snapshots/keys changed"
echo "s1 gaps ok"
