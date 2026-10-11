#!/usr/bin/env bash
# Explicit host adapters; the default changes no external files.
set -euo pipefail
exec node "$(dirname "$0")/sync-adapters.js" "$@"
