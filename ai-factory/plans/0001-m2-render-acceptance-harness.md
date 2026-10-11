# 0001 — Plan: Milestone 2 acceptance harness for rendered notes

**Goal:** Land the M2 closure table, a static render fixture vault, a POSIX demo skeleton driven
through `tests/support/fixture_vault.c`, a TeX/Poppler toolchain probe wired into `make check`,
ADR 0003 with revalidated packages, and the CI TeX subset, without changing any product code.

**Spec:** [ai-factory/specs/0001-m2-render-acceptance-harness.md](../specs/0001-m2-render-acceptance-harness.md)
(delivery `d-20261011-ff71bd`)

## Files

Create:

| Path | Why |
|---|---|
| `tests/fixtures/vaults/m2-render/.cwiki/preamble.tex` | Vault-wide preamble, plain LaTeX package lines only (R2.8.4); loads `amsmath`, `mhchem`, `tikz` (AC5). |
| `tests/fixtures/vaults/m2-render/class-note.md` | The render fixture note: inline/display math, `\ce{}`, bare `tikzpicture`, ≥5-line `align`, one failing display-math line (AC5, AC6). |
| `tests/fixtures/vaults/README.md` | Outside the vault root so it is not a note; records the AC7 hand-compile commands and observed result, and names the failing line (AC6, AC7). |
| `tests/render/check_render_fixture.sh` | Structural check of the fixture: files, required constructs, exactly one failing line (AC5, AC6). |
| `scripts/tex-probe.sh` | POSIX-sh toolchain probe: lists every required tool/file found/missing; skips or fails per `CWIKI_REQUIRE_TEX` (AC12–AC14). |
| `tests/render/test_tex_probe.sh` | Runs the probe against stub `PATH` directories for full, empty and partial installs, with and without `CWIKI_REQUIRE_TEX=1` (AC12–AC14). |
| `tests/support/m2_demo.c` | Demo driver: creates a root with `fixture_vault_create`, copies the fixture in, runs the demo script on it, destroys the root and confirms it is gone (AC8, AC9). |
| `scripts/m2-demo.sh` | Demo skeleton: runs the probe, then each step in the step list; documents the extension point (AC9–AC11). |
| `scripts/m2-demo-steps.txt` | Step list `id|slice|description`, one line per ROADMAP M2 demo step (AC9, AC11). |
| `tests/render/test_m2_demo.sh` | Runs the driver; asserts exit 0, probe output, every step reported pending with its slice, and the temporary root removed (AC8–AC11). |
| `tests/docs/check_m2_closure.sh` | Structural check of the M2 closure section: slices S1–S10 present, five deferred rows with owning milestone, checklist present (AC1–AC4). Not wired into `make check`. |
| `ai-factory/adr/0003-tex-live-and-poppler-subprocess-dependencies.md` | Dependency ADR with revalidated package matrix and CI subset (AC16–AC18, AC20). |

Modify:

| Path | Why |
|---|---|
| `tests/support/fixture_vault.h`, `tests/support/fixture_vault.c` | Add `fixture_vault_copy_in`; copy-in support is missing today (AC8). |
| `tests/support/test_fixture_vault.c` | Tests for copy-in (AC8). |
| `Makefile` | Targets `tex-probe`, `tex-probe-test`, `render-fixture-test`, `m2-demo`, `m2-demo-test`; `check: test tex-probe`; `demo` gains `m2-demo`; driver in `ANALYZE_SOURCES` (AC9, AC10, AC12–AC15). |
| `docs/DEVELOPING.md` | Document `make m2-demo`, the probe and `CWIKI_REQUIRE_TEX` (AC9, AC13a). |
| `.github/workflows/ci.yml` | Install the ADR 0003 TeX/Poppler subset, cache it, set `CWIKI_REQUIRE_TEX=1` for every `make check` (AC19). |
| `docs/ACCEPTANCE.md` | New "Milestone 2 closure" section with open/deferred rows and the user acceptance checklist (AC1–AC4, AC7 record reference). |

Not touched: `src/`, `tests/snapshots/`, `tests/fixtures/keys/`, `docs/SPEC.md` (AC20, AC21). The
ROADMAP S1 row already carries the delivery ID; setting it to complete is the session's
slice-end action under the milestone autopilot, not a plan step.

## Server vs client components

Not applicable: cwiki is one local C process; this slice adds shell scripts, one C test driver and
documentation.

## Steps

- [x] Step 1 — Fixture-vault copy-in. AC8. Add `int fixture_vault_copy_in(const struct
  fixture_vault *vault, const char *source_dir)` that recursively copies regular files and
  directories (including dot-directories) into the root, opening the source read-only; any symlink
  or other file type fails with `EINVAL` and nothing outside the root is written. Tests in
  `test_fixture_vault.c`: copy a nested tree with `.cwiki/preamble.tex` and a note from one fixture
  vault into another and compare bytes; source tree unchanged afterwards; a symlink in the source
  is rejected. Expected red: `build/test-fixture-vault` fails to build because
  `fixture_vault_copy_in` is undeclared. Verify (red): `make support-test` Verify: `make support-test`
- [ ] Step 2 — Render fixture vault. AC5, AC6, AC7. Write `preamble.tex` (no `\documentclass`, no
  `\begin{document}`), `class-note.md` and `tests/fixtures/vaults/README.md`. The failing block is
  display math `$$…$$` on one line using an undefined control sequence (`\undefinedfixturemacro`),
  so R2.9.4 would send it to TeX and the error is "Undefined control sequence" at that single line.
  Hand-compile each block with `lualatex -no-shell-escape` in a scratch directory (never in the
  repo) inside `\documentclass[varwidth]{standalone}` plus `\input` of the fixture preamble; record
  the exact commands, TeX Live version and result in the README. Add `check_render_fixture.sh`
  (files exist; preamble loads the three packages; note has `$…$`, `$$…$$`, `\ce{`,
  `\begin{tikzpicture}`, an `align` of ≥5 lines all but the last ending `\\`; exactly one line
  contains `\undefinedfixturemacro`) and target `render-fixture-test` in `test`.
  Verify: `sh tests/render/check_render_fixture.sh`
- [ ] Step 3 — Toolchain probe. AC12, AC13, AC13a, AC14, AC15. `scripts/tex-probe.sh` uses only
  shell builtins plus the probed tools: checks `lualatex`, `pdflatex`, `pdftocairo` via `command
  -v`, and `mhchem.sty`, `tikz.sty`, `amsmath.sty`, `standalone.cls`, `preview.sty` via
  `kpsewhich` (each reported missing, with the reason, when `kpsewhich` is absent). Prints one
  found/missing line per item; when any is missing prints one summary naming all missing items and
  either "TeX toolchain checks skipped" (exit 0) or, when `CWIKI_REQUIRE_TEX=1`, a failure (exit 1).
  `test_tex_probe.sh` builds stub tool directories inside a `mktemp -d` root under `TMPDIR`, runs
  `/bin/sh scripts/tex-probe.sh` with `PATH` set to the stub directory, and asserts: full stub →
  exit 0, eight `found` lines; empty `PATH` with variable unset → exit 0, eight `missing` lines and
  the skip message; partial stub with `CWIKI_REQUIRE_TEX=1` → exit 1 naming every missing item;
  partial with variable unset → exit 0 and skip message. Makefile: `tex-probe-test` in `test`;
  `tex-probe` runs the script; `check: test tex-probe` (so `make sanitize` does not rerun it, and
  `verify`/`review-changes.sh` reach it through `check`). Verify: `sh tests/render/test_tex_probe.sh`
  Verify: `make check` Verify: `env CWIKI_REQUIRE_TEX=1 make check`
- [ ] Step 4 — Demo skeleton. AC8, AC9, AC10, AC11. `tests/support/m2_demo.c` → `build/cwiki-m2-demo
  FIXTURE_DIR SCRIPT`: create root, copy in, print the root, `fork`/`execl("/bin/sh", …)` the script
  with the root as its only argument, destroy, verify with `access` that the root is gone, exit
  with the script status (nonzero also on any harness failure). `scripts/m2-demo.sh VAULT` runs the
  probe with `CWIKI_REQUIRE_TEX=` (demo always exits 0 on a missing toolchain), then for each line of
  `scripts/m2-demo-steps.txt` runs `scripts/m2-demo/<id>.sh VAULT` when that file exists
  ("runnable", fail the demo if it fails) or prints "pending — owner <slice>". Header comment states
  the extension point: a later slice adds only `scripts/m2-demo/<id>.sh` (and its own commands);
  the list already names every step. Step list, mapping each ROADMAP M2 demo step to its slice:
  `split-view|S7|Open the fixture note in source and rendered windows`;
  `save-edit|S6|Save an edit; the rendered view refreshes from disk`;
  `placeholder|S6|Observe non-blocking stale/placeholder behaviour and the eventual image`;
  `tex-error|S6|Trigger a TeX error and see its diagnostic in place`;
  `raw-log|S10|Inspect the failing block's raw TeX log`;
  `sync-jumps|S9|Jump source→rendered and rendered→source`.
  Makefile: build the driver with `-Itests/support`, add it to `ANALYZE_SOURCES`; `m2-demo` runs the
  driver on `tests/fixtures/vaults/m2-render` and `scripts/m2-demo.sh`; `demo: replay-test
  m1-demo-test m2-demo`; `m2-demo-test` runs `tests/render/test_m2_demo.sh` and is in `test`.
  `docs/DEVELOPING.md` documents `make m2-demo`, the step list, the probe and `CWIKI_REQUIRE_TEX`.
  Verify: `sh tests/render/test_m2_demo.sh build/cwiki-m2-demo` Verify: `make demo`
- [ ] Step 5 — ADR 0003 and CI subset. AC16, AC17, AC18, AC19, AC20. Write ADR 0003 in the
  `0000-template.md` shape: TeX Live (LuaLaTeX, pdfLaTeX) and Poppler `pdftocairo` are runtime
  subprocess dependencies, never linked, always invoked with `-no-shell-escape` until M3's
  machine-local trust policy; package matrix revalidated today against the FreeBSD ports/packages
  index, Arch package index and Homebrew formula/cask index with observed name and version per
  platform and an explicit AUR/non-core-tap/ports-build statement; the CI subset per platform
  (smallest packaged set for which the probe reports all eight items found; macOS cask `basictex`
  plus `tlmgr install` of the named packages; FreeBSD a TeX Live subset) and its caching; the SPEC §0
  matrix stays the user-facing install. CI: Arch adds the subset and `poppler` to its pacman line
  (download cache already keyed on `ci.yml`); macOS installs `poppler` and the BasicTeX subset,
  caching the TeX Live tree with `actions/cache` keyed on `hashFiles('.github/workflows/ci.yml')`
  and skipping install on a hit; FreeBSD installs the subset uncached; every job runs `make check`
  with `CWIKI_REQUIRE_TEX=1` (Arch through the `Review changes` step env). Report any SPEC §0
  name/version/availability change in the step report for the user; do not edit `docs/SPEC.md`.
  Verify: `env CWIKI_REQUIRE_TEX=1 make check`
- [ ] Step 6 — Milestone 2 closure table. AC1, AC2, AC3, AC4. Add "## Milestone 2 closure" to
  `docs/ACCEPTANCE.md`: one **open** row per ROADMAP M2 acceptance criterion (eight) naming its
  owning slice; one **open** row per M2 scope item mapped to exactly one slice with that slice's R
  ids; per-slice FEATURES rows listing the names of the 28 FEATURES rows marked `M2`, each named
  exactly once; **deferred** rows for R1.12.7 pull warming/external refresh (M3), R6.4.1
  shell-escape override (M3), R1.12.9a per-row stale markers (M5), R6.3.3a rendered link
  focus/follow/back stack (M4) and R1.8.7/R1.8.7a sync jump-list recording and toggle (M5), each
  citing its decision source; the AC7 hand-compile record by reference to the fixture README; and a
  user acceptance checklist mirroring `scripts/m2-demo-steps.txt` with the same slice per step. Add
  no requirement absent from SPEC/ROADMAP/FEATURES; report any contradiction found instead of
  resolving it. `check_m2_closure.sh` asserts the section, S1–S10, the five deferred ids each on a
  deferred line naming M3/M4/M5, the checklist heading, and no **verified** disposition yet.
  Verify: `sh tests/docs/check_m2_closure.sh`
- [ ] Step 7 — Final verification. AC21 and all ACs. No file under `src/` and no approved snapshot
  or key recording changed since the spec commit; the full suite passes with the toolchain
  required. Verify (final): `git diff --exit-code 152787c -- src tests/snapshots tests/fixtures/keys`
  Verify (final): `make verify` Verify (final): `env CWIKI_REQUIRE_TEX=1 make check`
  Verify (final): `make demo`

## Risks and how each is checked

| Risk | Check |
|---|---|
| Probe or demo script uses a GNU/bash extension, or the Makefile gains a GNU make construct. | Scripts run under `/bin/sh` in the tests; macOS `make` is GNU, so the real POSIX-make proof is the FreeBSD CI job (bmake). `make analyze` and FreeBSD CI only run after the user approves a push. |
| `PATH`-stubbed probe test accidentally finds real TeX tools. | Test sets `PATH` to the stub directory alone and invokes `/bin/sh` by absolute path; the empty-install case asserts eight `missing` lines on this TeX-equipped machine. |
| Copy-in writes outside the root or follows a symlink out of the fixture. | Step 1 symlink-rejection test; copy-in builds destination paths only under `vault->root`. |
| Demo leaves a temporary vault behind. | Driver asserts the root is gone; `test_m2_demo.sh` checks the printed root no longer exists. |
| Fixture blocks compile differently in a later standalone template (S2). | AC7 records the exact wrapper; S2 owns the real template and must re-assert the same failing line. |
| CI TeX subset too small (a package dependency missing, e.g. `chemgreek` for `mhchem`, LuaTeX format). | `CWIKI_REQUIRE_TEX=1` makes the probe fail the job; first CI run after push approval is the evidence. Locally only the MacTeX install is exercised. |
| BasicTeX cache restored but stale or `PATH` not set. | Probe in `make check` fails with named items under `CWIKI_REQUIRE_TEX=1`. |
| Package revalidation drifts from SPEC §0. | Step 5 reports changes to the user (AC20); SPEC untouched. |
| Closure table misses or duplicates a FEATURES M2 row. | `check_m2_closure.sh` checks structure only; the exactly-once mapping of the 28 rows is checked by `/t4:check` review against `docs/FEATURES.md`. |

## Verification

| Phase | Command | Covers |
|---|---|---|
| red | `make support-test` — expected to fail: `build/test-fixture-vault` does not compile, `fixture_vault_copy_in` undeclared | AC8 |
| step | `make support-test` | AC8 |
| step | `sh tests/render/check_render_fixture.sh` | AC5, AC6 (AC7 by the README record) |
| step | `sh tests/render/test_tex_probe.sh`; `make check`; `env CWIKI_REQUIRE_TEX=1 make check` | AC12–AC15 |
| step | `sh tests/render/test_m2_demo.sh build/cwiki-m2-demo`; `make demo` | AC8–AC11 |
| step | `env CWIKI_REQUIRE_TEX=1 make check` (CI evidence pending push approval) | AC16–AC20 |
| step | `sh tests/docs/check_m2_closure.sh` | AC1–AC4 |
| final | `git diff --exit-code 152787c -- src tests/snapshots tests/fixtures/keys`; `make verify`; `env CWIKI_REQUIRE_TEX=1 make check`; `make demo` | AC21, all |

Red tests are scoped to Step 1. Final completion requires every final command to pass;
`scripts/review-changes.sh` additionally needs `clang-tidy`, which is not installed locally, so
`make analyze` evidence comes from Linux CI.
