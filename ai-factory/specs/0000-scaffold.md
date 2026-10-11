# 0000 — Setup check

Confirm that ai-sdlc 2.8.0 is ready to use in this project.

## Acceptance criteria
- AC1 — The project checks listed in `ai-factory/AGENTS.md` pass.
- AC2 — A small change through `/t4:quick` (Codex: `$t4-quick`) follows the project instructions and produces a checked, reviewable diff.
- AC3 — With the plugin hooks active, edits to protected paths in `ai-factory/docs/dont-touch.md` are blocked.
- AC4 — With the plugin hooks active, completed turns appear in `ai-factory/runs/log.pending.csv`; committing moves them to `log.csv`.

## Out of scope
Application features. This is only a setup check.
