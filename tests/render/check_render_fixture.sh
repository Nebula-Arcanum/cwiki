#!/bin/sh
# Structural check of the M2 render fixture vault (AC5, AC6).
set -eu
v=${1:-tests/fixtures/vaults/m2-render}
n=$v/class-note.md
p=$v/.cwiki/preamble.tex
fail() { echo "render fixture: $*" >&2; exit 1; }
[ -f "$p" ] || fail "missing preamble"
[ -f "$n" ] || fail "missing note"
for pkg in amsmath mhchem tikz; do
	grep -q "\\\\usepackage.*{$pkg}" "$p" || fail "preamble lacks $pkg"
done
grep -q '[^$]\$[^$]' "$n" || fail "no inline math"
grep -q '^\$\$.*\$\$$' "$n" || fail "no display math"
grep -q '\\ce{' "$n" || fail "no \\ce"
grep -q '\\begin{tikzpicture}' "$n" || fail "no tikzpicture"
grep -q '\\end{tikzpicture}' "$n" || fail "no tikzpicture end"
# align body: >=5 lines, all but last end with \\ ; last does not
body=$(sed -n '/\\begin{align}/,/\\end{align}/p' "$n" | sed '1d;$d')
cnt=$(printf '%s\n' "$body" | wc -l | tr -d ' ')
[ "$cnt" -ge 5 ] || fail "align has $cnt lines"
last=$(printf '%s\n' "$body" | tail -n 1)
case $last in *'\\') fail "last align line ends with \\\\";; esac
bad=$(printf '%s\n' "$body" | sed '$d' | grep -vc '\\\\$' || true)
[ "$bad" = 0 ] || fail "align line lacks trailing \\\\"
[ "$(grep -c 'undefinedfixturemacro' "$n")" = 1 ] || fail "need exactly one failing line"
echo "render fixture ok"
