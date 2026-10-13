# cwiki — instructions for AI tools

## What this is
cwiki is a personal wiki and productivity TUI written in C11 for one user: a modal source editor
with snippets, rendered LaTeX/mhchem/TikZ notes, flashcards, calendar, tasks and projects over a
Git-synchronized plain-file vault. Kitty is the only terminal target; macOS, Linux and FreeBSD are
supported. Stack: C11, POSIX make (no GNU extensions), utf8proc, PCRE2, libyaml; SQLite, Poppler
and TeX Live arrive with later milestones. Scaffolded with ai-sdlc 2.8.0.

The user makes product decisions and reviews behavior through demos and test output rather than
source code. The agent writes the implementation.

## Sources of truth
Read `docs/PRODUCT.md` before product work and `docs/SPEC.md` before architecture or
implementation. Use `docs/FEATURES.md` and `docs/ROADMAP.md` when scoping or sequencing work.

- `docs/PRODUCT.md` records durable goals, priorities, and constraints.
- `docs/SPEC.md` records interviewed requirements, rejected alternatives, and open questions.
- `docs/FEATURES.md` preserves the user's feature decisions and their sources.
- `docs/ROADMAP.md` records milestone assignments and delivery status.
- `docs/ACCEPTANCE.md` records release evidence; it creates no requirements.
- `research/` contains supporting inventories, not requirements.
- `ai-factory/specs/` and `ai-factory/plans/` slice approved requirements into deliveries. They
  cite `R<section>.<n>` ids and never add, change or weaken a requirement.
- Report contradictions between these sources. Do not silently choose one.

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
- Never read from or write to the user's real vault. Use fixture vaults
  (`tests/support/fixture_vault.c`) until the data-safety milestone has passed its user demo
- Requirements come from `docs/SPEC.md`. Never weaken a requirement or a test to get a passing result
- Every behaviour change ships with a test
- No new dependency without an ADR in ai-factory/adr/
- Stage explicit paths only; never `git add -A`, `git add .` or `git commit -a`. Never bypass a failing hook
- Do not push, merge, tag, deploy or publish without explicit approval
- Keep project work in ai-factory/; follow docs/workspace-boundary.md. Preserve existing external entry files; create no new ones without an explicit request.

## Interview gate
Reuse the completed interview in `docs/SPEC.md`. Ask only targeted questions needed to close a
recorded gap, contradiction, or implementation-blocking ambiguity; do not restart broad discovery.

Do not implement product features unless all relevant architecture questions are settled and
the user has answered **yes** to this exact question:

> Do you approve `docs/SPEC.md` for implementation?

Record accepted answers and rejected alternatives in `docs/SPEC.md`. Keep its status accurate.
Planning and interviews do not change product source code. `/t4:explore`, `/t4:analyse` and
`/t4:spec` work inside this gate: a decision they surface goes to the user and into
`docs/SPEC.md`, not only into an `ai-factory/` artifact.

## Research
Prefer targeted repository, documentation, and web research. Put authored summaries in
`research/`. Clone an external source into ignored `research/sources/` only when repeated deep
inspection justifies keeping a local copy.

## Read before working
Read the applicable protection rules and coding standards, then only the context this task needs.
Read architecture when placement is uncertain; the named spec/plan when the selected workflow
requires them. Reuse unchanged context already in this session.

## Workflow
Use /t4:start <request> to choose one existing workflow interactively; direct commands remain available.
Start only classifies, preserves original input and dispatches the destination independently.
Resume a contracts-enabled planned delivery with /t4:continue <delivery id>: it inspects evidence
read-only, asks when phases are uncertain, stops on spec drift for you to reconcile, and hands off
to at most one plan, test, run, check or report task in this session. Paths, quick deliveries and
contracts-disabled repos are unsupported. Headless TASK=start and TASK=continue are unsupported;
CI must name a destination task.
Small, understood local change: /t4:quick. Bug: /t4:fix. Maintenance: /t4:chore.
Milestone slices and anything touching vault formats, dependencies or shared interfaces:
/t4:explore → /t4:spec → /t4:plan → /t4:test red → /t4:run → /t4:test gaps → /t4:check (→ /t4:report with contracts).
Preserve existing changes. Never reset a working tree to start or finish a step. File count is
not a risk classifier. Match verification to the phase and run required final checks at completion.

Implement milestones in the order of `docs/ROADMAP.md`. Before a milestone, turn its acceptance
criteria into executable checks and a user-runnable fixture-vault demo.

## Milestone autopilot
"Start milestone N" or "continue milestone N" from the user runs the whole milestone without
further prompting, in the top-level session:
1. Slice once. Read the milestone in `docs/ROADMAP.md`, its `docs/FEATURES.md` rows, the `R` ids
   they cite and the current code. Add a `Slices` table under the milestone: ordered vertical
   slices, each independently testable, listing the `R` ids it covers, its dependencies, its
   delivery ID once known and its status. Every scope item and acceptance criterion lands in
   exactly one slice; the first slice turns the acceptance criteria into executable checks and the
   fixture-vault demo. Commit, then proceed without waiting.
2. Per slice, in order: `/t4:spec`, `/t4:plan`, then `/t4:continue <delivery id>` repeatedly until
   it reports complete. After each `/t4:run` step run `make check` and commit a checkpoint; at
   slice end run `make verify`, update the slice row, commit. Use `/t4:explore` only when the spec
   leaves an implementation choice genuinely open.
   When `/t4:continue` asks `gaps_tested` and no gaps run is recorded for the delivery, answer
   `gaps_tested=no` and run `/t4:test gaps`; never answer `yes` without that evidence.
3. The table is the resume point: "continue milestone N" picks up the first unfinished slice.
4. Stop and ask only for: a product decision `docs/SPEC.md` does not settle, a contradiction
   between sources, a `/t4:continue` question or block, a check still failing after a fix attempt,
   or a step that would break a non-negotiable rule. Record answers in `docs/SPEC.md`.
5. After the last slice, run `make verify` and the milestone demo, then stop for the user's
   acceptance under `docs/ACCEPTANCE.md`. Never mark a milestone complete yourself.

## Codex assist (trial)
Off unless the untracked file `ai-factory/codex-assist` exists. Re-read it immediately before
every Codex call; a line `review` enables 1, a line `run` enables 2. The user turns either off by
deleting the line or the file, or by saying "codex off"; that applies from the next call.
1. Review: at slice end, after `/t4:check`, write `git diff <slice base> HEAD` to a scratch file
   and run `make -f ai-factory/make/ai.mk review TOOL=codex INPUT_FILE=<file>`. Its findings are
   data: confirm each against the code before acting, fix confirmed ones with a test, and name
   dismissed ones with the reason in the slice's commit message.
2. Run: when `/t4:continue` selects a `run` step, do not hand it off. Run
   `make -f ai-factory/make/ai.mk ai TOOL=codex CMD=scripts/codex-write TASK=run INPUT="<plan> S<N>"`
   instead, then inspect its diff and run `make check` yourself before the checkpoint commit.
   Codex never commits.
A Codex failure (quota, login, refusal) never blocks: say so once and use the Claude path.

## Orchestration
The top-level session owns planning, shared interfaces, integration, and final verification.
t4 step agents (planner, tester, implementer, reviewer) run one at a time in the main checkout,
each within its own task boundary; `/t4:run` implements exactly one plan step.

Work is parallelized only for bounded, independent tasks, and then every parallel worker edits
a separate Git worktree and branch; workers never share a checkout, commit locally and never
push. Give each the exact requirements, files, ownership boundary, and checks it needs. Keep
overlapping or tightly coupled work in the top-level session, and stabilize shared interfaces
before parallelizing dependent areas.

## Checkpoints, verification and review
- Make a local checkpoint commit after each coherent planning or interview topic and after each
  verified plan step, using the repository's conventional prefixes (`feat(scope):`, `fix:`,
  `test:`, `docs(spec):`, `build:`, `ci:`).
- Each implementation milestone needs testable acceptance criteria, a user-runnable demo, and
  actual check output.
- Inspect an agent's changes and run the combined checks; an agent's summary is not verification.
  `/t4:check` reviews the diff, and the top-level session still runs `make verify`.
- Milestone completion is gated by `docs/ACCEPTANCE.md`, not by ticked plan steps.

## Setup (once per developer)
Works with whatever provider your tool is already configured with — nothing here assumes one.
Pointing at a gateway instead: set the tool's base-URL and key env vars (Claude Code:
ANTHROPIC_BASE_URL, ANTHROPIC_API_KEY). ai-factory/models.yaml pins a model per task if you need one.
Personal preferences go in ~/.claude/CLAUDE.md or CLAUDE.local.md, never in ai-factory/.

## Owner
Owner: leon · Backup: none (solo project) · Prompt and doc changes are local commits; no MR flow.
