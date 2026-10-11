# cwiki — instructions for AI tools

## What this is
cwiki is a personal wiki and productivity TUI written in C11 for one user: a modal source editor
with snippets, rendered LaTeX/mhchem/TikZ notes, flashcards, calendar, tasks and projects over a
Git-synchronized plain-file vault. Kitty is the only terminal target; macOS, Linux and FreeBSD are
supported. Stack: C11, POSIX make (no GNU extensions), utf8proc, PCRE2, libyaml; SQLite, Poppler
and TeX Live arrive with later milestones. Scaffolded with ai-sdlc 2.8.0.

Root `AGENTS.md` and `docs/` hold the product rules and stay authoritative: `docs/PRODUCT.md`
(goals), `docs/SPEC.md` (approved requirements and rejected alternatives), `docs/FEATURES.md`
(feature decisions), `docs/ROADMAP.md` (milestones and status), `docs/ACCEPTANCE.md` (release
evidence). Where this file and root `AGENTS.md` differ, root `AGENTS.md` wins; report the
contradiction instead of choosing silently.

## Commands
Run from the repository root. Details: `docs/DEVELOPING.md`.
- `make` — strict build of `build/cwiki` (`-std=c11 -pedantic -Wall -Wextra -Werror -Wconversion -Wshadow`).
- `make check` — strict build plus every test, fuzz target and the M1 demo replay. Affected-change check.
- `make sanitize` — clean rebuild and the same tests under ASan and UBSan.
- `make verify` — `check` then `sanitize`. Required full suite at completion.
- `make analyze` — clang-tidy static analysis, warnings as errors. Needs `clang-tidy`; CI runs it on Linux.
- `make demo` — user-runnable harness replay and M1 class-note demo.
- `make kitty-width-test` — Unicode width conformance against a real kitty; needs kitty and a display.
- `scripts/review-changes.sh` — change-review gate: whitespace, tracked artifacts, then check, sanitize, analyze.
A new source or test file is added to `APP_SOURCES`, `PRODUCT_TESTS` and `ANALYZE_SOURCES` in the root `Makefile`.
Slash commands: /t4:start /t4:continue /t4:fleet /t4:design /t4:adr /t4:analyse /t4:explore /t4:spec /t4:plan /t4:test /t4:run /t4:fix /t4:chore /t4:quick /t4:check /t4:report
Headless (CI only): make -f ai-factory/make/ai.mk ai TASK=<name> INPUT="…"

## Non-negotiable rules
- Never edit paths listed in ai-factory/docs/dont-touch.md
- Never read from or write to the user's real vault; use fixture vaults (`tests/support/fixture_vault.c`)
- Requirements come from `docs/SPEC.md`. Do not weaken a requirement or a test to get a passing result
- Stage explicit paths only; never `git add -A`, `git add .` or `git commit -a`. Never bypass a failing hook
- Do not push, merge, tag, deploy or publish without explicit approval
- Every behaviour change ships with a test
- No new dependency without an ADR in ai-factory/adr/
- Keep project work in ai-factory/; follow docs/workspace-boundary.md. Preserve existing external entry files; create no new ones without an explicit request.


## Read before working
Read the applicable protection rules and coding standards, then only the context this task needs.
Read architecture when placement is uncertain; fleet for cross-service work; the named spec/plan
when the selected workflow requires them. Reuse unchanged context already in this session.

## Workflow
Use /t4:start <request> to choose one existing workflow interactively; direct commands remain available.
Start only classifies, preserves original input and dispatches the destination independently.
Resume a contracts-enabled planned delivery with /t4:continue <delivery id>: it inspects evidence
read-only, asks when phases are uncertain, stops on spec drift for you to reconcile, and hands off
to at most one plan, test, run, check or report task in this session. Paths, quick deliveries and
contracts-disabled repos are unsupported. Headless TASK=start and TASK=continue are unsupported;
CI must name a destination task.
Small, understood local change: /t4:quick. Bug: /t4:fix. Maintenance: /t4:chore.
Uncertain requirements, authorization, public contracts, migrations, dependencies or service
boundaries: /t4:explore → /t4:spec → /t4:plan → /t4:test red → /t4:run → /t4:test gaps → /t4:check (→ /t4:report with contracts).
Preserve existing changes. Never reset a working tree to start or finish a step. File count is
not a risk classifier. Match verification to the phase and run required final checks at completion.

## Setup (once per developer)
Works with whatever provider your tool is already configured with — nothing here assumes one.
Pointing at a gateway instead: set the tool's base-URL and key env vars (Claude Code:
ANTHROPIC_BASE_URL, ANTHROPIC_API_KEY). ai-factory/models.yaml pins a model per task if you need one.
Personal preferences go in ~/.claude/CLAUDE.md or CLAUDE.local.md, never in ai-factory/.

## Owner
Owner: leon · Backup: none (solo project) · Prompt and doc changes are local commits; no MR flow.
