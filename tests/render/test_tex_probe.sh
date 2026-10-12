#!/bin/sh
# Runs scripts/tex-probe.sh against stub PATH directories (AC12-AC14).
set -eu
probe=$(pwd)/scripts/tex-probe.sh
root=$(mktemp -d "${TMPDIR:-/tmp}/texprobe.XXXXXX")
trap 'rm -rf "$root"' EXIT
fail() { echo "tex-probe test: $*" >&2; exit 1; }
mkdir "$root/full" "$root/empty" "$root/partial"
for t in lualatex pdflatex pdftocairo; do
	printf '#!/bin/sh\nexit 0\n' > "$root/full/$t"
	cp "$root/full/$t" "$root/partial/$t"
done
printf '#!/bin/sh\necho /stub/$1\n' > "$root/full/kpsewhich"
printf '#!/bin/sh\ncase $1 in mhchem.sty|tikz.sty) echo /stub/$1;; esac\n' > "$root/partial/kpsewhich"
chmod +x "$root"/full/* "$root"/partial/*
run() { # dir [REQUIRE]; sets out, rc
	rc=0
	out=$(env -i PATH="$1" ${2:+CWIKI_REQUIRE_TEX=$2} /bin/sh "$probe" 2>&1) || rc=$?
}
count() { printf '%s\n' "$out" | grep -c "^$1:" || true; }
run "$root/full"
[ $rc = 0 ] && [ "$(count found)" = 8 ] && [ "$(count missing)" = 0 ] || fail "full: rc=$rc $out"
run "$root/empty"
[ $rc = 0 ] && [ "$(count missing)" = 8 ] || fail "empty: rc=$rc $out"
case $out in *"TeX toolchain checks skipped"*) ;; *) fail "empty: no skip message";; esac
run "$root/partial" 1
[ $rc = 1 ] || fail "partial required: rc=$rc"
for i in amsmath.sty standalone.cls preview.sty; do
	case $out in *"missing: $i"*"TeX toolchain missing:"*"$i"*) ;; *) fail "partial required: $i not named";; esac
done
run "$root/partial"
[ $rc = 0 ] && [ "$(count found)" = 5 ] || fail "partial: rc=$rc"
case $out in *"checks skipped"*) ;; *) fail "partial: no skip message";; esac
echo "tex probe test ok"
