#!/bin/sh

set -eu
export LC_ALL=C

empty_tree=$(git hash-object -t tree /dev/null)
git diff --check "$empty_tree" --

artifacts=$(git ls-files -- \
	build '*.o' '*.a' '*.so' '*.dylib' '*.dll' '*.exe' \
	'*.gcda' '*.gcno' '*.profraw' compile_commands.json)
if [ -n "$artifacts" ]; then
	printf '%s\n' 'tracked build/generated artifacts:' "$artifacts" >&2
	exit 1
fi

make check
make sanitize
make analyze
