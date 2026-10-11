# Do not edit — enforced by the guard-paths hook

Lines starting with "- `" are rules; the backticked prefix is matched against the target path.

- `build/`
- `research/sources/`
- `.git/`
- `.claude/`
- `.cursor/`
- `.codex/`
- `ai-factory/runs/`
- `ai-factory/.sdlc.json`


`build/` is generated; change the root `Makefile` instead. `research/sources/` holds ignored
third-party clones. To change adapters, edit ai-factory/ and run `/t4:sync-sdlc`.

Not expressible as a repository path, and equally binding: the user's real vault lives outside
this repository and is never read or modified. Approved snapshots under `tests/snapshots/` and
key recordings under `tests/fixtures/keys/` change only when the spec'd behaviour changes, never
to make a failing test pass.
