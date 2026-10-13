# 0002 — Plan: Milestone 2 block extraction and TeX source generation

**Goal:** From a saved note's bytes, its zone data, its frontmatter and the vault preamble, produce
the ordered list of renderable LaTeX blocks with exact note line ranges, decide which are
compilable against the packages actually loaded, and generate one note-level TeX source with a
page → block map and a generated-line → note-line/origin table — pure computation, no subprocess.

**Spec:** [ai-factory/specs/0002-m2-block-extraction-tex-source.md](../specs/0002-m2-block-extraction-tex-source.md)
(delivery `d-20261012-251ff8`)

## Files

Create:

| Path | Why |
|---|---|
| `src/tex_env.c`, `src/tex_env.h` | Shipped per-package environment tables (core, amsmath, mhchem, tikz, pgf, pgfplots, tikz-cd), preamble scan for the six environment-declaration commands and `\usepackage`/`\RequirePackage`, implied loads, and the resulting availability set (AC9, AC12, AC32, AC33). |
| `src/tex_block.c`, `src/tex_block.h` | Block extraction from zone data over the note body, `% cwiki:` directives, availability gating, nested-environment check, unterminated/degraded detection and the report list (AC1–AC8, AC10–AC15, AC29–AC31). |
| `src/tex_source.c`, `src/tex_source.h` | Reads `<vault>/.cwiki/preamble.tex`, composes effective preamble inputs, generates the TeX document, page → block map and line table (AC16, AC17, AC20–AC23, AC34, AC35). Named `tex_*` to stay clear of the display `render` module. |
| `tests/render/test_tex_env.c` | Tables, scan and implied loads (AC9, AC12, AC32, AC33). |
| `tests/render/test_tex_block.c` | Extraction, gating, directives, reports, fixture extraction (AC1–AC8, AC10–AC15, AC26, AC29–AC31). |
| `tests/render/test_tex_source.c` | Inputs, generation, line table, determinism (AC16–AC23, AC28, AC34, AC35). |
| `tests/render/fuzz_tex.c` | Arbitrary-bytes fuzz through frontmatter parse, extraction and generation with invariant asserts; standalone mode like `fuzz_zone.c` (AC25). |
| `tests/render/tex_compile_driver.c` | Test-only driver: fixture root via `fixture_vault_create` + `fixture_vault_copy_in`, generate `class-note.tex` and `class-note.lines` into the root, exec a check script on the root, destroy the root (AC24, AC26). |
| `tests/render/tex_compile_check.sh` | Runs the S1 probe; skips/fails per `CWIKI_REQUIRE_TEX`; runs `lualatex -no-shell-escape -interaction=nonstopmode -file-line-error` in the root and asserts pages and the error line mapping to note line 23 (AC24). |
| `tests/render/check_s2_boundaries.sh` | Greps the three new `src/tex_*.c` for `fork`, `exec`, `popen`, `system(`, socket and stdout/terminal writes; asserts nothing found (AC27). |
| `tests/fixtures/vaults/m2-render/frontmatter-note.md` | `packages: [tikz-cd]`, `preamble:` with `\newenvironment{foo}`, `engine: pdflatex`, and `tikzcd`/`foo` blocks (AC11, AC12, AC16, AC18, AC26). |
| `tests/fixtures/vaults/m2-render/directive-note.md` | `% cwiki: norender` in an environment and in multi-line `$$…$$`/`\[…\]`; an unknown `% cwiki:` word (AC15, AC29, AC26). |
| `tests/fixtures/vaults/m2-render/fence-note.md` | Delimiters inside a `latex` fence, a plain fence, `%%`, `<!-- -->`, SynIgnore, and `\$` in prose (AC5, AC6, AC26). |
| `tests/fixtures/vaults/m2-render/unknown-env-note.md` | One unrecognised name in several blocks, a nested unknown `\begin{bar}`, an unterminated `\begin` (AC14, AC30, AC31, AC26). |

Modify:

| Path | Why |
|---|---|
| `src/config.c`, `src/config.h` | Note TeX frontmatter (`engine:`, `packages:`, `preamble:`) parsed alongside `cwiki_config_parse_note_subjects`, rejecting the user environment-table key in frontmatter; the user environment table as a validated config key in global/vault/machine-local scopes with a scope-composing builder (AC13, AC16, AC18, AC19, AC28, AC33). |
| `tests/config/test_config.c` | Tests for both config additions (AC13, AC28, AC33). |
| `Makefile` | Targets for the three tests, fuzz target and compile driver; `tex-compile-test` and `s2-boundary-test` in `test`; new sources in `APP_SOURCES`, tests in `PRODUCT_TESTS`, all in `ANALYZE_SOURCES` (AC25, AC27). |
| `tests/render/check_render_fixture.sh` | Only if it assumes `class-note.md` is the vault's sole note; otherwise untouched. |
| `ai-factory/docs/architecture.md` | Module map gains `tex_env`, `tex_block`, `tex_source` (spec "Components touched"). |

Not touched by this delivery: `src/zone.c` (read-only use; AC "Out of scope"). Two separate
commits outside this delivery changed it on 2026-10-12 at the developer's direction: `0b2c337`
(bounded scan cost on long lines) and `3b936ee` (prose `\ce{}` is a top-level chemistry zone,
replacing Step 4's extraction-only engine). `d7b3dc9` (2026-10-13, review fix, developer-authorised)
balances nested braces in `\ce{}`, `\text{}`/`\intertext{}` and reference arguments. Also not touched: `tests/snapshots/`,
`tests/fixtures/keys/`, `tests/fixtures/vaults/m2-render/class-note.md`,
`tests/fixtures/vaults/m2-render/.cwiki/preamble.tex`, `docs/SPEC.md`. The ROADMAP S2 row's delivery
ID and status are the session's slice-end action under the milestone autopilot, not a plan step.

## Server vs client components

Not applicable: cwiki is one local C process. All S2 product code is pure in-process computation
plus one read-only file read (`preamble.tex`); the only subprocess is in the test-only AC24 script.

## Design choices the spec leaves to planning

- **Frontmatter and zone state.** The zone engine does not know YAML frontmatter, so an unbalanced
  `$` or `\begin{` inside `preamble:` would leak zone state into the body. Extraction therefore runs
  the zone engine over a body-only `cwiki_buffer` copy (lines after the closing `---`) starting from
  an empty stack, and adds the body offset to every reported line (AC7). No change to `zone.c`.
- **Block = outermost renderable zone.** A block opens where a body line's zone stack goes from
  depth 0 (prose) to a `MATH_INLINE`, `MATH_DISPLAY`, `CHEMISTRY`, `TIKZ` or `LATEX` zone, and
  closes where it returns to depth 0. Environment names come from `cwiki_zone_detail`. Nested
  `\begin` names inside the block are collected from the block's inner `OPEN` spans (AC3, AC30).
- **Unterminated** = the body's last line ends with that block still on the stack, for every block
  kind including inline `$`, `\(` and `\ce{` (R2.9.7, settled 2026-10-12). **Untrusted** = any line
  in the block's range has `degraded` set by `cwiki_zone_build_line` (AC31).
- **Availability is required for environment blocks and nested environments only.** Commands such
  as `\ce` are not gated (R2.9.4 speaks of environments).
- **mhchem table is empty** (mhchem defines no environments); it exists so the table key is uniform.
- **User environment table key:** `environments:`, a mapping `package-name: [env, …]`, names
  validated like `packages:` entries. A later scope replaces a package's whole entry and `null`
  removes it (R2.9.5a, settled 2026-10-12).
- **Template.** `\documentclass[varwidth,multi=cwikiblock]{standalone}`, then the vault preamble,
  `\usepackage` lines, note `preamble:`, `\begin{document}`, and per block a `\begin{cwikiblock}`
  line, the block's bytes on their own lines, an `\end{cwikiblock}` line. Inline-math and chemistry
  bytes are inserted as written (text mode, so `$…$`/`\ce{}` stay inline); display-math and
  environment bytes as written inside the `varwidth` box (display context). This extends the S1
  hand-compile wrapper recorded in `tests/fixtures/vaults/README.md`; AC24 proves it. If
  `multi`+`varwidth` misbehaves, the implementer may switch to `preview` (`tightpage`) per block —
  `preview.sty` is already probed — and must record the change in the step report.
- **Line table.** One record per generated line: origin ∈ {template, vault-preamble, packages,
  note-preamble, block} plus the source line (preamble.tex line, `packages:` list index, `preamble:`
  line, or note line). A block's first generated line holds the bytes from the opening delimiter
  onward, so it still maps to exactly one note line.
- **Vault preamble size.** Read with a 1 MiB cap matching the frontmatter cap; over the cap is
  treated as unreadable (R2.8.6, settled 2026-10-12).

## Steps

- [x] Step 1 — Environment tables and preamble scan (`tex_env`). AC9, AC12, AC32, AC33. API: build
  an availability set from (vault preamble bytes, note `preamble:` bytes, note `packages:` list,
  composed user table). Shipped tables keyed by package; `latex` core always in force; implied
  loads pgfplots→tikz, tikz-cd→tikz, tikz→pgf; `\usepackage[opt]{a,b}` and `\RequirePackage`
  recognised through the escape/comment-aware idiom using the shared `regex` runtime with limits
  (a limit hit is returned as an error, never a silent no-match); the six declaration commands
  register names; user entries in force only when their package is loaded. Query: "is `name`
  available" and "which package provides it". Tests in `test_tex_env.c`: each shipped package's
  table inactive until loaded and core always active; each of the six commands; `\usepackage` with
  options and a comma list; `\RequirePackage`; implied loads; a commented-out `% \usepackage{x}`
  does not load; user entry inactive when its package is not loaded.
  Expected red: `build/test-tex-env` fails to build, `tex_env.h` not found.
  Verify (red): `make build/test-tex-env`
  Verify: `make build/test-tex-env` Verify: `build/test-tex-env`
- [x] Step 2 — Note TeX frontmatter in `config`. AC16, AC18, AC19, AC28, AC33. Add
  `cwiki_config_parse_note_tex(buffer, struct cwiki_config_note_tex *, error)` next to
  `cwiki_config_parse_note_subjects`, reusing its frontmatter delimiting and 1 MiB cap: returns
  engine (default LuaLaTeX), `packages:` names in order, `preamble:` bytes verbatim (literal block
  scalar preserved byte-for-byte), and the body's first line index. Invalid `engine:` value,
  `packages:` not a sequence of `[A-Za-z0-9._-]+` scalars, non-string `preamble:`, or an
  `environments:` key present → `CWIKI_CONFIG_SCHEMA_ERROR` with line/column. Other keys ignored
  (M4 owns typed properties). Tests in `test_config.c`: absent frontmatter and absent keys → no
  packages, no preamble, LuaLaTeX; `engine: pdflatex`/`lualatex`; `engine: LuaTeX` rejected with
  location; `packages: [tikz-cd, siunitx]` order kept; `packages: "x"` and `packages: ["a b"]`
  rejected; `preamble: [x]` rejected; `preamble: |` block bytes exact; `environments:` in
  frontmatter rejected with location.
  Verify: `make build/test-config` Verify: `build/test-config`
- [x] Step 3 — User environment table in configuration. AC13, AC33. `cwiki_config_parse` accepts
  `environments:` (mapping package → sequence of environment names; strict schema, duplicate keys
  and wrong types rejected with source locations per R7.1.2) in global, vault and machine-local
  files; `cwiki_config_build_environments(configs, count, …)` composes scopes, later scope replacing
  an entry by package name and `null` removing it, mirroring `cwiki_config_build_zones`. Tests in
  `test_config.c`: valid table parses; non-mapping, non-list, non-string, bad name, duplicate key
  each rejected with line/column; two scopes compose with replacement. `cwiki --inspect-config`
  output is not extended (not required by the spec).
  Verify: `make build/test-config` Verify: `build/test-config`
- [x] Step 4 — Block extraction (`tex_block`). AC1, AC2, AC3, AC4, AC5, AC6, AC7, AC8, AC26, AC31.
  `cwiki_tex_extract(engine, note buffer, body first line, availability set, result)` → ordered
  blocks {kind inline/display/chemistry/environment, env name, byte range per line, note line range,
  compilable, suppressed} and reports {kind, name, note line}. Body-only buffer copy as above;
  `cwiki_zone_recompute` then `cwiki_zone_build_line` per line. Unterminated and degraded blocks
  are kept, marked not compilable, and reported with start line and reason (every block kind,
  R2.9.7). Add fixture notes
  `fence-note.md` and `unknown-env-note.md` (unterminated case used here; unknown-name cases in
  Step 5). Tests in `test_tex_block.c`, each note copied into a `fixture_vault_create` root via
  `fixture_vault_copy_in` and read from there: `$…$` and `\(…\)` single-line exact bytes; `$$…$$`
  and `\[…\]` single- and multi-line ranges; `\ce{}` in prose is a block, `\ce{}`/`\text{}`/nested
  `\begin` inside math or an environment are not separate blocks; `tikzpicture` and multi-line
  `align` with `[options]` bytes; nothing from fences (incl. `latex`), `%%`, `<!-- -->`, SynIgnore;
  `\$` opens nothing; frontmatter with a stray `$` and `\begin{` yields nothing from frontmatter and
  body lines keep note numbering; unterminated `$$`, `\[`, `\begin{x}`, `$`, `\(` and `\ce{` each
  reported "unterminated" with start line; a degraded line (engine built with a tiny match limit, as `test_zone.c` does) reported
  "untrusted"; `class-note.md` yields exactly the six AC8 blocks, all compilable, no reports; the
  committed fixture directory's bytes are unchanged after the test.
  Expected red: `build/test-tex-block` fails to build, `tex_block.h` not found.
  Verify: `make build/test-tex-block` Verify: `build/test-tex-block`
- [x] Step 5 — Availability gating, directives and reports. AC10, AC11, AC12, AC13, AC14, AC15,
  AC29, AC30, AC33. Environment blocks and every nested `\begin` name are checked against the
  availability set; any unknown name makes the whole block not compilable and is reported once per
  name per extraction with its first line. `% cwiki: norender` on a line of its own inside an
  environment or multi-line `$$`/`\[` block → suppressed, not compilable, not reported; any other
  `% cwiki:` word → reported by name, block stays compilable. Add `frontmatter-note.md` and
  `directive-note.md`; extend `unknown-env-note.md`. Tests in `test_tex_block.c`: `align` with
  amsmath in vault preamble compilable, without it not compilable and `align` reported; `tikzcd`
  with/without `packages: [tikz-cd]`; `\newenvironment{foo}` in vault preamble and in note
  `preamble:`; user table entry with package loaded compilable, unloaded not; one unknown name in
  three blocks reported once, all three not compilable; nested unknown `\begin{bar}` inside `align`
  and inside `$$…$$`; norender in environment, `$$…$$` and `\[…\]`; unknown directive word.
  Verify: `make build/test-tex-block` Verify: `build/test-tex-block`
- [x] Step 6 — TeX source generation (`tex_source`). AC16, AC17, AC20, AC21, AC22, AC23, AC28,
  AC34, AC35. Read `<vault>/.cwiki/preamble.tex` only (missing → nothing; unreadable or over the 1 MiB
  cap → report naming the path, no source; R2.8.6). Generate from (note bytes, blocks, chosen compilable subset
  in caller order, note TeX inputs, vault preamble) the document described above, the page → block
  map and the line table; refuse (no source, report) when frontmatter or vault preamble reported
  an error; refuse a block that is not compilable. Tests in `test_tex_source.c`: vault preamble
  bytes present verbatim and each line tagged with its `preamble.tex` line; one `\usepackage{name}`
  per `packages:` entry in order; `preamble:` verbatim; order vault → packages → note preamble;
  missing vault preamble contributes nothing; a preamble one byte over 1 MiB reported as
  unreadable with path and no source; unreadable one (mode 000 inside the fixture root;
  skipped with a message when running as root) reported with path and no source; invalid
  frontmatter → no source; subset `{display 23, inline 3}` emitted in that order, one page each,
  page map matches; every block byte present unmodified; every block line maps to its note line,
  no template/preamble line maps to a note line; inline and chemistry blocks not inside a display
  construct, display/environment blocks inside the varwidth box; two runs byte-identical.
  Expected red: `build/test-tex-source` fails to build, `tex_source.h` not found.
  Verify: `make build/test-tex-source` Verify: `build/test-tex-source`
- [x] Step 7 — Real TeX compile check. AC24, AC26. `tex_compile_driver.c` →
  `build/cwiki-tex-compile ROOT_FIXTURE SCRIPT` (built like `m2_demo.c`, `-Itests/support`, in
  `ANALYZE_SOURCES`): copy fixture in, extract and generate all six blocks, write `class-note.tex`
  and `class-note.lines` (`genline origin noteline` per line) into the root, `fork`/`execl` `/bin/sh
  SCRIPT ROOT`, destroy the root, exit with the script status. `tex_compile_check.sh ROOT`: run
  `scripts/tex-probe.sh`; if it does not print `found: lualatex`, print a skip naming the probe
  result and exit 0, or exit 1 when `CWIKI_REQUIRE_TEX=1`; else run `lualatex -no-shell-escape
  -interaction=nonstopmode -file-line-error class-note.tex` in ROOT, assert the log reports at
  least 5 pages written (the five good blocks; no dependence on a recovered page for the failing
  block, per the 2026-10-12 decision), and that the
  `class-note.tex:N: Undefined control sequence` line N maps to note line 23 in
  `class-note.lines`. Makefile target `tex-compile-test` added to `test`.
  Verify: `make tex-compile-test` Verify: `env CWIKI_REQUIRE_TEX=1 make tex-compile-test`
- [x] Step 8 — Fuzz target. AC25. `fuzz_tex.c` with `CWIKI_TEX_FUZZ_STANDALONE`, mirroring
  `fuzz_zone.c`: deterministic corpus plus seeded random bytes biased toward `---`, `$`, `\[`,
  `\begin{`, `\end{`, `%%`, `% cwiki:`, fences, `\newenvironment`, `\usepackage`; runs frontmatter
  parse, availability build, extraction and generation; asserts every block range lies in the
  note, blocks do not overlap and are ordered, every line-table entry names an existing generated
  line and an existing note line or origin line; bounded iteration count so `make check` stays fast.
  `$(TEX_FUZZ)` in `PRODUCT_TESTS`, `product-test` and `ANALYZE_SOURCES`; runs under `make sanitize`.
  Verify: `make build/fuzz-tex` Verify: `build/fuzz-tex`
- [x] Step 9 — Boundary check and docs. AC27. `check_s2_boundaries.sh` (target `s2-boundary-test`
  in `test`) fails if `src/tex_env.c`, `src/tex_block.c` or `src/tex_source.c` mention `fork`,
  `exec`, `popen`, `system(`, `socket`, `connect(`, `STDOUT_FILENO`, `printf(` or `fputs(`.
  Architecture module map: add `tex_env`, `tex_block`, `tex_source` under a "Render input" row
  citing R2.3, R2.8, R2.9, R6.1.1, R1.8.14.
  Verify: `sh tests/render/check_s2_boundaries.sh`
- [x] Step 10 — Final verification. All ACs. Approved snapshots, key recordings and the committed
  S1 fixture files are unchanged since the spec commit; the full suite passes, with TeX required.
  Verify (final): `git diff --exit-code 135a625 -- tests/snapshots tests/fixtures/keys tests/fixtures/vaults/m2-render/class-note.md tests/fixtures/vaults/m2-render/.cwiki/preamble.tex`
  Verify (final): `git diff --exit-code d7b3dc9 -- src/zone.c`
  Verify (final): `make verify` Verify (final): `env CWIKI_REQUIRE_TEX=1 make check`
  Verify (final): `sh tests/render/check_s2_boundaries.sh` Verify: `make check`

## Risks and how each is checked

| Risk | Check |
|---|---|
| Frontmatter text leaks zone state into the body (stray `$`, `\begin{` in `preamble:`). | Body-only zone scan; Step 4 frontmatter test; fuzz corpus starts with `---`. |
| Extraction becomes a second parser (R1.9.7). | Blocks derived only from zone stacks/spans; the only extra scanning is the preamble scan (preamble text, not note body) and the `% cwiki:` line match inside a known block; reviewed at `/t4:check`. |
| Preamble-scan regex hits a PCRE2 limit and silently drops a declaration. | Step 1 returns the limit as an error; Step 5/6 turn it into a report and no source. |
| Line table off by one at block edges or frontmatter offset. | Step 6 per-line mapping assertions; Step 7 maps a real TeX error to note line 23; fuzz invariants. |
| `standalone` `multi`+`varwidth` paging or display context differs from the S1 hand compile. | Step 7 real compile under `CWIKI_REQUIRE_TEX=1`; fallback to `preview` recorded if needed. |
| Failing block halts output so the good blocks' pages are missing. | `nonstopmode`; Step 7 asserts at least 5 pages and the line-23 error mapping. |
| Tests write into the committed fixture. | All tests use `fixture_vault_copy_in`; Step 10 `git diff --exit-code` on the S1 files; Step 4 byte comparison. |
| Unreadable-file test passes vacuously when run as root (CI containers). | Test checks `geteuid()==0` and prints an explicit skip for that case only. |
| Fuzz target slow or leaks under sanitizers. | Bounded iterations; `make sanitize` in `make verify`. |
| A new file missing from `APP_SOURCES`/`PRODUCT_TESTS`/`ANALYZE_SOURCES`. | `make analyze` on Linux CI (not installed locally); `/t4:check` review of the Makefile diff. |
| POSIX-make or shell portability of new targets/scripts. | Scripts run under `/bin/sh`; FreeBSD (bmake) CI after push approval. |

## Verification

| Phase | Command | Covers |
|---|---|---|
| red | `make build/test-tex-env` — expected to fail: `tex_env.h` not found | AC9, AC12, AC32, AC33 |
| step | `make build/test-tex-env`; `build/test-tex-env` | AC9, AC12, AC32, AC33 |
| step | `make build/test-config`; `build/test-config` | AC13, AC16, AC18, AC19, AC28, AC33 |
| step | `make build/test-tex-block`; `build/test-tex-block` | AC1–AC8, AC10–AC15, AC26, AC29–AC31 |
| step | `make build/test-tex-source`; `build/test-tex-source` | AC16, AC17, AC20–AC23, AC28, AC34, AC35 |
| step | `make tex-compile-test`; `env CWIKI_REQUIRE_TEX=1 make tex-compile-test` | AC24, AC26 |
| step | `make build/fuzz-tex`; `build/fuzz-tex` | AC25 |
| step | `sh tests/render/check_s2_boundaries.sh` | AC27 |
| final | `git diff --exit-code 135a625 -- tests/snapshots tests/fixtures/keys tests/fixtures/vaults/m2-render/class-note.md tests/fixtures/vaults/m2-render/.cwiki/preamble.tex` and `git diff --exit-code d7b3dc9 -- src/zone.c`; `make verify`; `env CWIKI_REQUIRE_TEX=1 make check`; `sh tests/render/check_s2_boundaries.sh` | all |

Red tests are scoped to Step 1; Steps 4 and 6 note their own expected build failure for the tester's
next red run. Final completion requires every final command to pass. `make analyze` needs
`clang-tidy`, which is not installed locally; its evidence comes from Linux CI.

## Ambiguities resolved (developer, 2026-10-12; `docs/SPEC.md` commit 81cad8a)

1. User environment table key is `environments:` (package → environment names); a later scope
   replaces a package's whole entry, `null` removes it (R2.9.5a).
2. Vault `preamble.tex` is capped at 1 MiB; over the cap is treated as unreadable (R2.8.6).
3. R2.9.7 covers every block kind, including inline `$`, `\(` and `\ce{` (R2.9.7).
4. AC24 asserts the error maps to note line 23 and at least five pages; it does not depend on a
   recovered page for the failing block.

No open ambiguities remain.
