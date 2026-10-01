#!/bin/sh

set -eu

if [ "$#" -ne 1 ]; then
	printf 'usage: %s TEST-BINARY\n' "$0" >&2
	exit 2
fi

case "$1" in
	/*) CWIKI_KITTY_WIDTH_BINARY=$1 ;;
	*) CWIKI_KITTY_WIDTH_BINARY=$(pwd)/$1 ;;
esac
CWIKI_KITTY_WIDTH_RESULT=$(mktemp -d "${TMPDIR:-/tmp}/cwiki-kitty-width.XXXXXX")
export CWIKI_KITTY_WIDTH_BINARY CWIKI_KITTY_WIDTH_RESULT
trap 'rm -rf "$CWIKI_KITTY_WIDTH_RESULT"' EXIT HUP INT TERM

"${KITTY:-kitty}" --config NONE --override macos_quit_when_last_window_closed=yes sh -c '
   "$CWIKI_KITTY_WIDTH_BINARY"
   printf "%s\n" "$?" >"$CWIKI_KITTY_WIDTH_RESULT/status"
'

if [ ! -f "$CWIKI_KITTY_WIDTH_RESULT/status" ]; then
	printf 'FAIL: Kitty closed before the width probe reported a result\n' >&2
	exit 1
fi
status=$(cat "$CWIKI_KITTY_WIDTH_RESULT/status")
case "$status" in
	0) printf 'kitty width conformance: ok\n' ;;
	*[!0-9]*|'')
		printf 'FAIL: invalid Kitty width probe status: %s\n' "$status" >&2
		exit 1
		;;
	*)
		printf 'FAIL: Kitty width probe exited with status %s\n' "$status" >&2
		exit "$status"
		;;
esac
