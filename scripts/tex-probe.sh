#!/bin/sh
# TeX/Poppler toolchain probe (AC12-AC14). Exit 1 only when CWIKI_REQUIRE_TEX=1 and something is missing.
missing=
note() { # status item [reason]
	printf '%s: %s%s\n' "$1" "$2" "${3:+ ($3)}"
	[ "$1" = missing ] && missing="$missing${missing:+, }$2"
	return 0
}
for t in lualatex pdflatex pdftocairo; do
	if command -v "$t" >/dev/null 2>&1; then note found "$t"; else note missing "$t" "not on PATH"; fi
done
have_kpse=0
command -v kpsewhich >/dev/null 2>&1 && have_kpse=1
for f in mhchem.sty tikz.sty amsmath.sty standalone.cls preview.sty; do
	if [ "$have_kpse" = 0 ]; then note missing "$f" "kpsewhich not on PATH"
	elif [ -n "$(kpsewhich "$f" 2>/dev/null)" ]; then note found "$f"
	else note missing "$f" "kpsewhich cannot find it"; fi
done
[ -z "$missing" ] && exit 0
if [ "${CWIKI_REQUIRE_TEX:-}" = 1 ]; then
	echo "TeX toolchain missing: $missing" >&2
	exit 1
fi
echo "TeX toolchain missing: $missing; TeX toolchain checks skipped"
exit 0
