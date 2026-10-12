# 0002 — Milestone 2 block extraction and TeX source generation

Milestone 2 slice S2: from a note's existing per-line zone data, find its renderable LaTeX blocks
with exact source line ranges, decide which may be compiled, read the note's and vault's
preamble/engine inputs, and generate the TeX source for one note-level run together with a
temp-line → note-line table. Pure computation: no subprocess, cache or terminal output.

## User story
As the user writing chemistry, physics and calculus notes, I want cwiki to know exactly which parts
of a saved note are formulas, chemistry and diagrams, which of them TeX can compile with the
packages I actually load, and how every line of the generated TeX maps back to my note, so that the
render pipeline (S3) and diagnostics (S4) compile only what can succeed and point errors and sync
jumps at the right note line.

## Sources
`docs/ROADMAP.md` Milestone 2 Slices row S2 (R2.3, R2.8, R2.9, R6.1.1, R1.8.14);
`docs/ACCEPTANCE.md` "Milestone 2 closure" rows owned by S2 — scope items "LuaLaTeX with explicit
pdfLaTeX override" and "Vault, note and block preambles", and FEATURES rows "Per-note engine and
preamble directives", "Inline math and display math delimiters", "mhchem chemistry notation", "TikZ
diagrams", "Vault-wide preamble plus per-note/per-block additions", "Compile one block against the
note's preamble"; `docs/SPEC.md` R2.3.1–R2.3.3, R2.8.1–R2.8.4, R2.9.1–R2.9.6, R6.1.1, R6.1.2,
R1.8.14, R1.9.5, R1.9.6b, and the 2026-10-12 decisions R6.1.1a, R2.8.5–R2.8.7, R2.9.2a,
R2.9.4a, R2.9.5 (corrected), R2.9.5a, R2.9.6a, R2.9.7; R1.9.6c, R1.9.8, R1.3.4, R7.1.1, R7.1.2, R7.2.1, R7.3.1;
spec 0001 (render fixture vault `tests/fixtures/vaults/m2-render`, copied through
`fixture_vault_copy_in`).

## Acceptance criteria

Line numbers below are 1-based note lines. "Extraction" is the S2 operation that turns a note's
bytes plus its zone data into an ordered block list and a report list; "generation" turns a chosen
set of compilable blocks plus the effective preamble inputs into one TeX source and its line table.

**Block extraction from zone data**

- **AC1** — Given a note line containing `$…$` or `\(…\)` in prose, when extraction runs, then it
  yields one inline-math block whose source bytes are exactly the delimited text including its
  delimiters and whose line range is that line, identified from `src/zone.c` zone data with no
  second parser (R1.9.7, R2.3.1, R2.3.2).
- **AC2** — Given `$$…$$` or `\[…\]`, on one line or spanning several, when extraction runs, then
  it yields one display-math block whose line range runs from the opening delimiter's line to the
  closing delimiter's line (R2.3.1, R2.3.2).
- **AC3** — Given `\ce{…}` in prose, when extraction runs, then it yields one chemistry block; and
  given `\ce{…}`, `\text{…}` or a nested `\begin{…}` inside a math zone or environment, then no
  separate block is yielded for it — it is part of the enclosing block's bytes (blocks are the
  outermost renderable zones; R1.9.5).
- **AC4** — Given a bare `\begin{name}` … `\end{name}` environment, including `tikzpicture` and a
  multi-line `align`, when extraction runs, then it yields one environment block carrying the
  environment name, its exact bytes including any `[options]` argument (R2.9.1, R2.9.2, R2.8.3), and
  its line range from the `\begin` line to the matching `\end` line.
- **AC5** — Given `$`, `\(`, `\[`, `\ce{` or `\begin{` inside any fenced code block — including a fence
  whose info string is `latex` — or inside a `%%` comment, an `<!-- -->` comment, or a SynIgnore region
  (R1.9.6b), when extraction runs, then no block is yielded from that text (R2.9.3, R2.3.3).
- **AC6** — Given an escaped `\$` in prose, when extraction runs, then it does not open a math block
  (R2.3.1).
- **AC7** — Given a note with an opening YAML frontmatter block, when extraction runs, then no block
  is yielded from frontmatter lines, and body line numbers are still reported as note line numbers.
- **AC8** — Given a copy of the render fixture `class-note.md` and its vault preamble, when
  extraction runs, then it yields exactly six blocks in document order: inline math on line 3,
  display math on line 5, chemistry on line 7, `tikzpicture` on lines 9–11, `align` on lines 13–19,
  and display math on line 23; all six are compilable; and the report list is empty.

**Environment availability (R2.9.4–R2.9.6)**

- **AC9** — Given shipped per-package environment tables for LaTeX core, amsmath, mhchem, TikZ/PGF,
  pgfplots and tikz-cd, when they are inspected, then each environment is keyed by its package and
  LaTeX-core environments are always in force while every other table is in force only when its
  package is loaded by the vault preamble or the note's `packages:` (R2.9.5).
- **AC10** — Given a note using `align` whose vault preamble loads `amsmath`, when extraction runs,
  then the `align` block is compilable; and given the same note with a vault preamble and
  `packages:` that load no package providing `align`, then the block is not compilable and the
  report list names `align` (R2.9.4, R2.9.6).
- **AC11** — Given a note whose frontmatter has `packages: [tikz-cd]` and a `\begin{tikzcd}` block,
  when extraction runs, then the block is compilable; without that package it is reported by name
  and not compilable.
- **AC12** — Given `\newenvironment{foo}…` in the vault preamble, or in the note's `preamble:`, when
  extraction runs on a note with a `\begin{foo}` block, then the block is compilable without any
  table entry (R2.9.5 preamble scan).
- **AC13** — Given a user environment-table entry declared as configuration data for an environment
  no shipped table or preamble declares, when extraction runs, then a block using it is compilable
  (R2.9.5, R1.14.2, R7.3.1); the entry is validated strictly with source-located errors (R7.1.2).
- **AC14** — Given a note in which the same unrecognised environment name occurs in several blocks,
  when extraction runs, then the report list names that environment exactly once, every such block
  is marked not compilable, and none of their bytes appear in any TeX source generated for the note
  (R2.9.4, R2.9.6, R2.9.6a).

**`% cwiki:` directives**

- **AC15** — Given `% cwiki: norender` on its own line inside an environment block, when extraction
  runs, then the block is yielded with its line range but marked suppressed, it is not compilable, it is not reported as an error, and its bytes never appear in
  generated TeX (R2.9.2, R2.9.2a).

**Frontmatter, vault preamble and engine**

- **AC16** — Given a note frontmatter with `packages:` as a list of package names and `preamble:` as
  a literal block, when the effective preamble inputs are read, then they expose the package names
  in order and the `preamble:` text byte-for-byte, and generated TeX contains one `\usepackage{name}`
  line per listed package and the `preamble:` text unmodified (R2.8.1, R2.8.3).
- **AC17** — Given a vault with `<vault>/.cwiki/preamble.tex`, when the effective preamble inputs
  are read, then its bytes are included unmodified in generated TeX, read from that fixed path only
  (R2.8.4).
- **AC18** — Given a note whose frontmatter makes no engine selection, when its inputs are read,
  then the engine is LuaLaTeX; given an explicit pdfLaTeX selection, then the engine is pdfLaTeX;
  and the engine is never inferred from packages, preamble or content (R6.1.1).
- **AC19** — Given a note whose frontmatter has no `packages:`, `preamble:` or engine key, or has no
  frontmatter at all, when inputs are read, then the note contributes no packages and no preamble
  text and the engine is LuaLaTeX.

**TeX source generation**

- **AC20** — Given a note and any chosen subset of its compilable blocks, when generation runs, then
  it produces one TeX document for one note-level run containing exactly those blocks, in the given
  order, each on its own output page, together with a page → block mapping (R6.1.2 one run per
  note, R1.8.14).
- **AC21** — Given generated TeX, when it is compared to the note, then every block's bytes appear
  unmodified (R1.3.4); inline-math and chemistry blocks are set in inline (text) math style and
  display-math and environment blocks in display context, so no block is silently promoted or
  demoted.
- **AC22** — Given generated TeX, when its line table is inspected, then every line that came from a
  block maps to exactly the note line it was copied from, and every other line is tagged with its
  origin — template, vault preamble (with its `preamble.tex` line), note `packages:`, or note
  `preamble:` — so no template line maps to a note body line (R1.8.14).
- **AC23** — Given identical note bytes, vault preamble, user tables and block subset, when
  generation runs twice, then the TeX source and line table are byte-identical.
- **AC24** — Given the render fixture copy, all six blocks selected, and the S1 toolchain probe
  reporting `lualatex` found, when a test compiles the generated source with
  `lualatex -no-shell-escape`, then the pages for the five good blocks are produced and the error
  TeX reports for `\undefinedfixturemacro` is on a generated line that the line table maps to note
  line 23. When the probe reports TeX missing, this test is skipped with a message naming the probe
  result, and it fails under `CWIKI_REQUIRE_TEX=1` (spec 0001 AC13/AC13a policy). This test is the
  only S2 check that runs a subprocess; product S2 code runs none.

**Robustness and verification**

- **AC25** — Given a fuzz target that feeds arbitrary bytes as a note (including frontmatter and
  preamble text) through extraction and generation, when it runs under `make check` and under ASan
  and UBSan in `make sanitize`, then it never crashes, over-reads, leaks or loops, and asserts that
  every block line range lies inside the note, blocks do not overlap, and every line-table entry
  refers to an existing generated line and an existing note line or origin (R1.9.8). It is wired
  into the root `Makefile` like the existing zone and snippet fuzz targets and into
  `ANALYZE_SOURCES`.
- **AC26** — Given any S2 test or fuzz run, when it uses the render fixture, then it copies the
  fixture into a root created by `tests/support/fixture_vault.c` and never writes into the committed
  fixture directory; added fixture notes for frontmatter, directive, fence and unknown-environment
  cases leave the existing `class-note.md` and `.cwiki/preamble.tex` unchanged.
- **AC27** — Given the diff of this slice, when it is reviewed, then S2 product code performs no
  subprocess, cache, network or terminal output; existing approved snapshots under
  `tests/snapshots/` and key recordings under `tests/fixtures/keys/` are unchanged; every new source
  and test file is listed in `APP_SOURCES`, `PRODUCT_TESTS` and `ANALYZE_SOURCES` as applicable;
  and `make verify` passes.

- **AC28** — Given frontmatter `engine: pdflatex`, `engine: lualatex` or no engine key, when inputs
  are read, then the engine is pdfLaTeX, LuaLaTeX or LuaLaTeX respectively. Given any other engine
  value, a `packages:` that is not a list of names matching `[A-Za-z0-9._-]+`, or a non-string
  `preamble:`, then a report gives the frontmatter location and no TeX source is generated for the
  note (R6.1.1a, R2.8.5).
- **AC29** — Given `% cwiki: norender` on its own line inside a multi-line `$$…$$` or `\[…\]` block,
  when extraction runs, then that block is suppressed as in AC15. Given an unknown `% cwiki:` word in
  a block, then the report names that word and the block stays compilable (R2.9.2a).
- **AC30** — Given an otherwise compilable block containing a nested unrecognised `\begin{bar}`,
  inside an environment or a math zone, when extraction runs, then the whole block is not compilable
  and `bar` is reported (R2.9.4a).
- **AC31** — Given a `$$`, `\[` or `\begin{…}` with no closing delimiter before the end of the
  note, or a block touching a line the zone engine marks degraded, when extraction runs, then that
  block is not compilable and is reported with its start line and the reason (unterminated or
  untrusted) (R2.9.7).
- **AC32** — Given environment declarations made by each of `\newenvironment`, `\renewenvironment`,
  `\NewDocumentEnvironment`, `\RenewDocumentEnvironment`, `\ProvideDocumentEnvironment` and
  `\DeclareDocumentEnvironment`, when the scan runs, then each declared name is available. Given a
  package loaded only by `\usepackage[opt]{a,b}` or `\RequirePackage` in the vault preamble or the
  note's `preamble:`, then its table is in force. Given only pgfplots or tikz-cd loaded, then the
  TikZ and PGF tables are also in force (R2.9.5).
- **AC33** — Given a user-table entry, when its package is not loaded, then the entry's environments
  are not available. Given the table key in note frontmatter, then the frontmatter is rejected with
  a source-located report, while the key is accepted in global, synchronized-vault and machine-local
  configuration (R2.9.5a, R7.2.2).
- **AC34** — Given no `<vault>/.cwiki/preamble.tex`, when inputs are read, then the vault contributes
  no preamble text. Given an unreadable one, then a report names its path and no TeX source is
  generated (R2.8.6).
- **AC35** — Given a vault preamble, note `packages:` and note `preamble:`, when generation runs, then
  they appear in that order — vault preamble, then the `\usepackage` lines in list order, then the
  `preamble:` text (R2.8.7).

## Out of scope
- Running TeX or Poppler from product code, `-no-shell-escape` invocation, render-cache keys
  (including R2.8.2's per-note cache-key effect, which S3 computes from the inputs S2 exposes),
  OS cache location, precompiled preambles, cancellation and SyncTeX sidecar generation (S3).
- TeX-log parsing, mapping errors through the line table, raw logs and `.fls` package detection
  (S4); S2 only produces the table.
- Showing reports or blocks to the user: message log, notifications, state dump (S10), in-place
  errors and placeholders (S6).
- The command arity table and its preamble scan for `\newcommand` and relatives (R1.15.3, M5);
  only environment declarations are scanned here.
- Typed frontmatter properties beyond `packages:`, `preamble:` and the engine key (M4).
- The trusted-vault shell-escape override (R6.4.1, M3).
- Changing zone recognition in `src/zone.c` beyond what block extraction strictly needs to read.

## Decisions (developer, 2026-10-12)
All nine questions from the draft were answered as proposed and recorded in `docs/SPEC.md`:
1. `engine: lualatex|pdflatex`. An invalid engine, a malformed `packages:` or a non-string
   `preamble:` is reported and the note gets no TeX (R6.1.1a, R2.8.5).
2. `% cwiki: norender` on a line of its own inside environment blocks and multi-line `$$…$$`/`\[…\]`
   blocks. Unknown words are reported and the block still compiles (R2.9.2a).
3. The user environment table maps a package to environment names. An entry is active only when its
   package is loaded, and the table is set in global, vault or machine-local config, not
   frontmatter (R2.9.5a).
4. The scan covers six environment-declaration commands; the R2.9.5 wording was corrected. Packages
   count as loaded through `\usepackage`/`\RequirePackage` and `packages:`, plus implied loads
   (R2.9.5).
5. An unknown nested environment withholds the whole block (R2.9.4a).
6. "Reported once" means once per name per extraction of a note (R2.9.6a).
7. Unterminated or degraded blocks are not compiled and are reported (R2.9.7).
8. A missing vault preamble contributes nothing; an unreadable one is reported and no TeX is
   generated (R2.8.6).
9. Preamble order: vault preamble, then `packages:` lines, then `preamble:` (R2.8.7).

## Open questions
None.

## Data, routes and components touched
- **Data:** note frontmatter keys `packages:`, `preamble:` and the engine key (read only); vault file
  `<vault>/.cwiki/preamble.tex` (read only); a new user environment table in the configuration
  schema (R7.3.1); in-memory block list, report list, generated TeX source, page → block map and
  temp-line → note-line/origin table. No vault writes, cache or state-directory data.
- **Routes:** none — no CLI flag, action or keybinding changes.
- **Components:** new flat module(s) in `src/` for block extraction, environment tables/preamble
  scan and TeX source generation (named to avoid the existing display `render` module); read-only
  use of `zone`, `buffer` and `regex`; `config` for the frontmatter keys and user environment table
  (alongside `cwiki_config_parse_note_subjects`); new tests under `tests/` mirroring the module(s),
  a new fuzz target, added fixture notes under `tests/fixtures/vaults/m2-render/`; root `Makefile`;
  `ai-factory/docs/architecture.md` module map; `docs/ROADMAP.md` S2 row (delivery ID and status
  only).
