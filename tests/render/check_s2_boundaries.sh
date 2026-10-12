#!/bin/sh
# AC27: render-input modules are pure computation (plus one file read): no
# subprocess, socket, or terminal/stdout output. Identifiers are matched as
# whole words so snprintf and cwiki_regex_execute do not trip it.
cd "$(dirname "$0")/../.." || exit 2
files="src/tex_env.c src/tex_block.c src/tex_source.c"
pat='(^|[^A-Za-z0-9_])(fork|exec[lv][pe]?|execvp|popen|system|socket|connect|printf|fputs|puts)[[:space:]]*\(|STDOUT_FILENO|(^|[^A-Za-z0-9_])stdout([^A-Za-z0-9_]|$)'
# shellcheck disable=SC2086
if grep -nE "$pat" $files; then
	echo "s2 boundary violation: forbidden call in render-input module" >&2
	exit 1
fi
echo "s2 boundaries ok"
