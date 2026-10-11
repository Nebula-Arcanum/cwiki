# 0001 — Milestone 2 acceptance harness for rendered notes

Milestone 2 slice S1: the closure table, render fixture vault, growing demo skeleton, TeX
toolchain dependency ADR, toolchain probe and CI TeX package subset that every later M2 slice
proves itself against. No product behaviour changes.

## User story
As the user who reviews cwiki through demos and test output, I want Milestone 2's acceptance
criteria, fixture note, demo entry point and TeX/Poppler toolchain gate in place before any
rendering code is written, so that every later slice lands against a fixed, checkable target and a
missing TeX installation is reported plainly instead of failing silently.

## Sources
`docs/ROADMAP.md` Milestone 2 (outcome, scope, acceptance criteria, demo, Slices row S1);
`docs/ACCEPTANCE.md` release gate ("Each future milestone gets its closure table before
implementation is delegated"); `docs/SPEC.md` R6.1.1–R6.1.3, R2.8.4, R2.9.4, R6.5.3, R1.8.11, R1.8.7a, R6.3.3a, §0 package
matrix ("Revalidate this matrix before implementation first introduces each dependency");
`ai-factory/AGENTS.md` ("No new dependency without an ADR"; "Before a milestone, turn its acceptance
criteria into executable checks and a user-runnable fixture-vault demo"); ADR 0001, ADR 0002;
developer answers of 2026-10-11 recorded under Decisions below.

## Acceptance criteria

**Closure table**

- **AC1** — Given `docs/ACCEPTANCE.md`, when this slice lands, then it contains a "Milestone 2
  closure" section whose table has one row for each of the eight Milestone 2 acceptance criteria
  in `docs/ROADMAP.md` (uncached switch returns without waiting; successful formula, chemistry and
  TikZ blocks replace placeholders; one failing block shows its mapped diagnostic while others stay
  visible; source/preamble/colour/cell-metric/engine changes invalidate only incompatible
  artifacts; no partial artifact displayed; source and inverse jumps land at line granularity
  inside multi-line blocks; missing or unparseable sidecar degrades to scrolling and says so;
  render and SyncTeX fuzz targets pass), each naming its owning slice S1–S10 from the ROADMAP
  Slices table, and each with disposition **open**.
- **AC2** — Given the same section, when it is read, then every Milestone 2 scope item listed in
  `docs/ROADMAP.md` appears in exactly one row, mapped to exactly one slice S1–S10, citing the `R`
  ids that slice lists in the ROADMAP Slices table, with disposition **open**. `docs/FEATURES.md`
  rows assigned milestone `M2` may be grouped into one row per slice, but each such row lists the
  names of the FEATURES rows it covers, and every M2 FEATURES row is named in exactly one closure
  row.
- **AC3** — Given the scope the ROADMAP Slices note moves out of Milestone 2 (pull-triggered cache
  warming and external-write refresh R1.12.7 to M3; trusted-vault shell-escape override R6.4.1 to
  M3; per-row stale markers R1.12.9a to M5) and the 2026-10-11 schedule decisions (rendered link
  focus, Enter to follow and back stack, R6.3.3a, to M4; sync jump-list recording R1.8.7 and its
  toggle, R1.8.7a, to M5), when the closure table is read, then each appears as a
  **deferred** row naming its owning milestone and the decision source, and none is mapped to an M2
  slice.
- **AC4** — Given the closure section, when it is read, then it states the Milestone 2 demo
  steps from `docs/ROADMAP.md` as a user acceptance checklist, each step naming the slice whose
  landing makes it runnable, and it adds no requirement absent from `docs/SPEC.md`,
  `docs/ROADMAP.md` or `docs/FEATURES.md`.

**Render fixture vault**

- **AC5** — Given the repository, when this slice lands, then a static render fixture vault exists
  under `tests/fixtures/` with a `.cwiki/` marker directory, the vault-wide preamble as plain LaTeX
  at `.cwiki/preamble.tex` (R2.8.4), and at
  least one note containing: inline math `$…$`; display math `$$…$$`; an `\ce{…}` expression; a
  bare `\begin{tikzpicture}` … `\end{tikzpicture}` block; a multi-line `\begin{align}` …
  `\end{align}` derivation of at least five source lines each ending with `\\` except the last; and
  one deliberately failing block.
- **AC6** — Given the failing block, when it is inspected, then it is written in an environment or
  math delimiter that R2.9.4/R2.9.5 make compilable with the fixture's preamble and packages
  (so it would be sent to TeX), and it fails only through a TeX error on a single identifiable
  source line, so R6.5.3's "structured error in place, other blocks usable" can later be asserted
  against that line.
- **AC7** — Given the fixture files, when they are compiled by hand with `lualatex
  -no-shell-escape` inside a document using the fixture's vault preamble, then every block except
  the failing one compiles, and the failing one reports an error on its identified line. This is a
  check recorded in the fixture's README or the closure table with the command used, not a product
  test.
- **AC8** — Given any test or demo that uses the render fixture, when it runs, then it copies the
  fixture into a root created by `tests/support/fixture_vault.c` and never writes into the
  committed fixture directory or any path outside that temporary root.

**Demo skeleton**

- **AC9** — Given a checkout, when the user runs a documented `make` target for the Milestone 2
  demo (named in `docs/DEVELOPING.md`), then it copies the render fixture into a temporary fixture
  vault, runs the toolchain probe, prints which M2 demo steps are runnable and which
  are pending with their owning slice, exits 0, and removes the temporary vault.
- **AC10** — Given the demo target, when it is run, then it is invoked by the existing `make demo`
  target and the target and its script are POSIX-make and POSIX-shell compatible (no GNU make
  extensions), matching the existing build constraint.
- **AC11** — Given the demo skeleton, when a later slice lands a demo step, then adding that step
  changes only the step list consumed by the skeleton and the step's own commands; the skeleton
  documents this extension point.

**Toolchain probe**

- **AC12** — Given a machine where `lualatex`, `pdflatex` and `pdftocairo` are on `PATH` and
  `kpsewhich` resolves `mhchem.sty`, `tikz.sty`, `amsmath.sty`, `standalone.cls` and `preview.sty`,
  when `make check` runs, then the probe reports each tool and file as found and passes.
- **AC13** — Given a machine where `CWIKI_REQUIRE_TEX` is unset and one or more required tools or
  packages are missing (none present, or a partial install), when `make check` runs, then the probe
  prints one message naming every missing tool and package and stating that the TeX toolchain
  checks were skipped, and `make check` does not fail because of the probe.
- **AC13a** — Given `CWIKI_REQUIRE_TEX=1` in the environment and any one required tool or package
  missing, when `make check` runs, then the probe names every missing item and `make check` fails.
- **AC14** — Given the probe's output in either case, when it is read, then each required item is
  listed by name with found/missing status; no item is omitted and no skip is silent.
- **AC15** — Given the probe, when `make verify` and `scripts/review-changes.sh` run, then the probe
  runs through `make check` in the same way and the build remains free of GNU make extensions.

**Dependency ADR**

- **AC16** — Given `ai-factory/adr/`, when this slice lands, then `0003-*.md` exists in the
  `0000-template.md` format, introducing LuaLaTeX/pdfLaTeX (TeX Live) and Poppler `pdftocairo` as
  runtime subprocess dependencies under R6.1.1–R6.1.3, stating they are invoked as subprocesses and
  never linked, and that every invocation passes `-no-shell-escape` (R6.4) until M3's machine-local
  trust policy exists.
- **AC17** — Given ADR 0003, when it is read, then it contains a package matrix for TeX Live and
  Poppler on FreeBSD, Arch Linux and Homebrew, revalidated against the official FreeBSD
  ports/packages, Arch package index and Homebrew formula/cask index, with the revalidation date and
  the package name and version observed on each, and it states whether any requires the AUR, a
  non-core Homebrew tap or a FreeBSD port built from source (PRODUCT platform boundary).
- **AC18** — Given ADR 0003, when it is read, then it records the CI TeX package subset per
  platform: the packages to install, chosen as the smallest packaged set that makes the probe
  pass with every item found, and how each is cached between runs. The CI subset may differ from
  the user-facing SPEC §0 matrix (macOS: cask `basictex` plus `tlmgr install` of the named
  packages; FreeBSD: a TeX Live subset instead of `texlive-full`); the ADR states that the SPEC
  matrix remains the documented user install.

**CI subset**

- **AC19** — Given `.github/workflows/ci.yml`, when this slice lands, then the Arch, macOS and
  FreeBSD jobs install exactly the subset recorded in ADR 0003 and Poppler, the Arch and macOS jobs
  cache the installed TeX packages keyed so that a change to the subset invalidates the cache (the
  FreeBSD install may be uncached), and every job runs `make check` with `CWIKI_REQUIRE_TEX=1` so a
  missing item fails the job rather than skipping.
- **AC20** — Given `docs/SPEC.md` §0's package-matrix table, when ADR 0003 revalidates it, then any
  package name or availability change is reported to the user for recording in `docs/SPEC.md`
  rather than silently edited there.

**No product change**

- **AC21** — Given the diff of this slice, when it is reviewed, then no file under `src/` changes,
  existing approved snapshots under `tests/snapshots/` and key recordings under `tests/fixtures/keys/`
  are unchanged, and `make verify` passes.

## Out of scope
- Any rendering code, block extraction, TeX source generation, cache, kitty graphics, rendered mode,
  sync or diagnostics (S2–S10).
- Executing the Milestone 2 acceptance criteria or demo steps; this slice only records them and
  provides the skeleton they plug into.
- Changing `docs/SPEC.md` requirements or `docs/ROADMAP.md` scope; contradictions found while
  writing the closure table are reported, not resolved.
- Real-kitty or graphics-protocol checks; the trusted-vault shell-escape override (M3).
- Using the user's real vault.

## Decisions (developer, 2026-10-11)
- Vault preamble: fixed, non-configurable `<vault>/.cwiki/preamble.tex`, plain LaTeX, synchronized
  through Git; recorded in `docs/SPEC.md` as R2.8.4 with "configurable path" rejected.
- Probe: locally, an absent or partial TeX install skips with a message naming every missing item;
  with `CWIKI_REQUIRE_TEX=1` (set by CI) any missing item fails.
- Required items: both engines (`lualatex`, `pdflatex`, since the pdfLaTeX override is M2 scope per
  R6.1.1), `pdftocairo`, and `mhchem`, `tikz`, `amsmath`, `standalone`, `preview`.
- CI may install a minimal fixture-exercised subset; the SPEC matrix stays the user-facing install;
  an uncached FreeBSD TeX install is acceptable.
- R6.3.3 link focus/follow/back stack (R6.3.3a, M4) and R1.8.7 sync jump-list recording
  (R1.8.7a, M5) are deferred rows, already decided in commit 42e6aea.
- FEATURES M2 rows may be grouped per slice with their names listed, each appearing exactly once.

## Open questions
None.

## Data, routes and components touched
- **Data:** new static fixture vault under `tests/fixtures/` (note(s), `.cwiki/` marker, vault
  preamble, optional README); no vault format, cache or state-directory data.
- **Routes:** none (no CLI flags or product commands change). New `make` target(s) for the probe
  and the M2 demo, wired into `check` and `demo`.
- **Components:** `docs/ACCEPTANCE.md`; `docs/DEVELOPING.md`; root `Makefile`; new script(s) under
  `scripts/` for the probe and demo skeleton; `tests/support/fixture_vault.c` (reused, unchanged
  unless copy-in support is missing); `ai-factory/adr/0003-*.md`; `.github/workflows/ci.yml`;
  `docs/ROADMAP.md` S1 row (delivery ID and status only).
