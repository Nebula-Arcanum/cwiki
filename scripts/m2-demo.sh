#!/bin/sh
# M2 demo skeleton: m2-demo.sh VAULT (a disposable copy of the render fixture).
# Extension point: a later slice adds only scripts/m2-demo/<id>.sh for its step;
# scripts/m2-demo-steps.txt already names every step and owning slice.
vault=${1:?usage: m2-demo.sh VAULT}
dir=$(dirname "$0")
CWIKI_REQUIRE_TEX= sh "$dir/tex-probe.sh" || exit 1
while IFS='|' read -r id slice desc; do
	[ -n "$id" ] || continue
	if [ -f "$dir/m2-demo/$id.sh" ]; then
		echo "runnable: $id ($slice) $desc"
		sh "$dir/m2-demo/$id.sh" "$vault" </dev/null || { echo "demo step failed: $id" >&2; exit 1; }
	else
		echo "pending - owner $slice: $id: $desc"
	fi
done < "$dir/m2-demo-steps.txt"
exit 0
