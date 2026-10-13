#!/bin/sh
# AC24: compile the generated class-note.tex with the real engine and map the
# TeX error line back to note line 23. Usage: tex_compile_check.sh ROOT
root=$1
cd "$root" || exit 1
probe=$(sh "$OLDPWD/scripts/tex-probe.sh" 2>&1) || { echo "$probe" >&2; exit 1; }
# The probe exits 1 when CWIKI_REQUIRE_TEX=1 and anything is missing (above);
# otherwise any missing item (lualatex, standalone.cls, amsmath...) skips.
case $probe in
*"TeX toolchain missing:"*)
	echo "tex-compile-test skipped: $(echo "$probe" | tr '\n' ' ')"; exit 0 ;;
esac
lualatex -no-shell-escape -interaction=nonstopmode -file-line-error \
	class-note.tex >lualatex.stdout 2>&1
pages=$(sed -n 's/.*Output written on .*(\([0-9]*\) page.*/\1/p' class-note.log | tail -n 1)
if [ "${pages:-0}" -lt 5 ]; then
	echo "expected at least 5 pages, got ${pages:-none}" >&2
	exit 1
fi
n=$(sed -n 's,^\(\./\)*class-note\.tex:\([0-9]*\): Undefined control sequence.*,\2,p' class-note.log | head -n 1)
if [ -z "$n" ]; then
	echo "no Undefined control sequence error in log" >&2
	exit 1
fi
mapped=$(awk -v n="$n" '$1 == n && $2 == "block" { print $3 }' class-note.lines)
if [ "$mapped" != 23 ]; then
	echo "TeX line $n maps to note line '${mapped:-none}', expected 23" >&2
	exit 1
fi
echo "tex-compile-test ok: $pages pages, error line $n -> note line 23"
