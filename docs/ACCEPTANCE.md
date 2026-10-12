# Milestone acceptance

This document is the release traceability source of truth. A milestone is not
complete because its component branches merged or its unit tests pass. It is
complete only when every row in its closure table is either:

- **verified**, with implementation, automated evidence, and user-observable
  acceptance evidence; or
- **deferred**, with the approved requirement and owning milestone named.

`docs/SPEC.md` remains authoritative for requirements. `docs/FEATURES.md`
records product decisions and provenance. This file records delivery evidence;
it does not create or change requirements.

## Release gate

Before a milestone may be reported complete or work on the next milestone may
start:

1. Reconcile every feature assigned to the milestone with the approved spec.
2. Exercise the production executable, not only component or test-only demo
   entry points.
3. Run strict, sanitizer, static-analysis, fuzz/model, and fault-injection checks
   applicable to the milestone.
4. Run the recorded acceptance scenarios in real kitty and inspect the rendered
   output. Synthetic ANSI rasterization may help debugging but cannot replace
   this check.
5. Have the user perform the milestone's manual acceptance checklist and record
   the result here.
6. Keep later milestones paused until all rows are verified or explicitly
   deferred and the user accepts the milestone.

Each future milestone gets its closure table before implementation is delegated.
Every worker receives the exact rows it owns, and integration verifies the
combined product against this table rather than trusting worker summaries.

## Milestone 1 closure

Status: **verified and accepted on 2026-10-01**. Milestone 2 is eligible to
proceed but awaits the user's explicit start command.

| Requirement slice | Sources | Current evidence | Disposition |
|---|---|---|---|
| Kitty capability gate, raw mode, keyboard protocol, synchronized updates, ordered crash restoration | R1.5; M1 terminal rows | PTY protocol/fault/signal tests; production CLI PTY lifecycle and exact reset test; real-kitty startup and Unicode-width conformance exercised | **Verified** |
| Raw key recording and replay-safe crash flush | R1.5.9 | Unit/fault tests, terminal crash integration, rejected-input PTY coverage, and decoded sidecar | **Verified** |
| UTF-8 buffer, grapheme edits, positions, in-session undo | R1.1–R1.3; M1 scope | Model, sanitizer, editor, and PTY tests; searchable in-session undo picker | **Verified**; persistent undo remains M3 |
| Plain Markdown load/save and durable failure semantics | R1.12; M1 acceptance | Production CLI save/reopen; round-trip and injected app-level pre-rename/post-rename tests with distinct visible diagnostics | **Verified** |
| Normal/Insert/Replace/Command-line and bounded M1 edits | R1.3; M1 scope | Editor/input/PTy tests include `I A o O x r`, `d c y`, put, text objects; every production command dispatches through named actions | **Verified** |
| Approved M1 motions and display/source-row behavior | R1.3, R1.7; M1 scope | Motion/layout/editor tests | **Verified** |
| Zone stack, parser spans, source highlighting, degraded fallback | R1.4, R1.9 | Parser/highlight tests and fuzzing; `% VimTeX: SynIgnore on/off` integrated | **Verified** |
| Conceal categories, mappings, reveal lifecycle | R1.6 | Conceal/layout/editor/PTy tests; runtime toggle and syntax-module disable path | **Verified** |
| Soft wrap, code no-wrap, continuation rows | R1.7 | Layout/render tests; per-window `\w` action and visible state | **Verified** |
| Relative numbers and smart cursorline | M1 display rows | Renderer/app tests; configurable and runtime-toggleable | **Verified** |
| Three-space indentation defaults | M1 indentation row | `shiftwidth`, `tabstop`, and `expandtab` config tests; production Tab defaults to three displayed cells | **Verified** |
| Snippet engine, contexts, mirrors, transforms, nesting, subject layers | R1.10 | Engine/catalog/config tests and app PTY coverage | **Verified** |
| `<C-l>` expansion/forward and `<C-h>` backward snippet navigation | R1.10.2; user acceptance | Editor-input and PTY regression tests | **Verified** |
| List snippets active at cursor | R1.10.10c | Named action and context-filtered overlay integrated and PTY-tested | **Verified** |
| Named actions, physical-key maps, rebinding, removal, clue popup | R1.14.1 | Registry/keymap/config/clue/app tests; all production controls audited through actions | **Verified** |
| Runtime option toggles | R1.7.2; M1 toggle row | Named actions for wrap, numbers, cursorline, conceal, snippets, and undo picker with visible state | **Verified** |
| Per-subsystem disable switches | R1.14.3 | Strict layered `modules` config for syntax, snippets, clues, and picker; app PTY test disables all four; conceal also has its own display switch | **Verified**; later subsystem switches ship with their owner |
| Declarative configuration and provenance inspection | R7.2 | Strict schema/layering tests; `--inspect-config` | **Verified** |
| Reusable line editor and M1 prompt surface | M1 input-prompt rows | Shared line editor integrated into command and picker queries with control-key tests | **Verified** |
| Reusable picker/float model | M1 scope; R9.1 | Model/renderer tests and searchable undo-history provider | **Verified**; Notes provider remains M4 and other providers M5 |
| Vault recognition | R2.1.2, R9.1.2; M1 fixture scope | `.cwiki` ancestor discovery for layered config and explicit CLI note path | **Verified**; Notes picker remains M4 |
| Persistent lightweight status | M1 scope | Mode, filename, dirty state, source position rendered | **Verified**; render status arrives M2 and Git/sync status M3 |
| Structural search and Vim-pattern translation | R1.9.4, R1.11 | Strict tests, deterministic fuzzing, analyzer | **Verified**; consumers follow owning commands |
| OSC 52 clipboard | R1.16 register scope; clipboard feature row | No M1 clipboard action exists | Deferred to M5 system-clipboard actions; terminal transport ships with that user path |
| Production acceptance and demo | M1 outcome/acceptance/demo | Requirement-traceable production CLI test, real-kitty inspection, real-kitty width probe, test-only fixture replay, and user acceptance | **Verified** |

### Current integration evidence

On 2026-09-30, after the final M1 module-control integration, the release branch
passed full strict `make check`, full ASan+UBSan `make sanitize`, direct Clang
static analysis of the changed application/configuration/input units, and five
consecutive sanitizer runs of the complete PTY application suite. This closes
the implementation-test gate, not the production or user-acceptance gates.

A fresh non-sanitized production executable then passed these isolated
fixture-vault scenarios:

- Kitty 0.48.1 started through its native keyboard protocol. `<C-l>` expanded
  `mk`, `<C-l>` advanced, and `<C-h>` returned to the prior stop; the edited
  `$y$` survived `:wq` and a new process reopening the file.
- The reopened frame showed source highlighting, width-safe `𝛼` concealment,
  relative line numbers, cursorline, status, and the generated `g` clue popup.
  The shared undo-history picker opened from its configured action. The real
  Kitty capture was inspected at `.amp/in/artifacts/m1-production-kitty.png`;
  no application clipping, overlap, or unreadable text was found.
- `I`, `A`, `o`, `O`, `r`, `x`, undo, save, and a word-object deletion were
  exercised through Kitty key events against a second disposable note.
- A synchronized vault config with all four M1 modules disabled left `\alpha`
  and `mk` literal and opened neither clues nor the picker.
- The production `--inspect-config` output reported synchronized-vault settings
  and source locations; an unknown key failed before terminal startup.
- `--reset-terminal` emitted the exact fixed restoration sequence, including
  the cursor-color stack pop added for theme-safe cursor contrast. A real
  pseudo-terminal that did not answer capability queries exited after the
  bounded timeout with the documented diagnostic.

On 2026-10-01, a clean-room audit reconciled the implementation with all 52
Milestone 1 rows in `docs/FEATURES.md` and the approved requirements in
`docs/SPEC.md`. It found no further missing M1 feature, but found three release
evidence gaps and one save-error presentation defect. The release candidate now:

- runs the real `build/cwiki` from `make check`, covering reset, layered config
  discovery and provenance, pre-terminal config rejection, capability startup,
  edit/save/quit, terminal restoration, exact Markdown, and temporary recorder
  cleanup;
- compares cwiki's grapheme-width predictions with cursor-position reports from
  a real Kitty for combining, wide, emoji/VS16/ZWJ/flag, and default-ignorable
  sequences; the probe passes in Kitty 0.48.1 and is a required Linux CI step;
- distinguishes a pre-rename save failure from post-rename durability
  uncertainty, exits Command-line mode so the diagnostic is visible, and keeps
  the buffer dirty in both cases; app-level PTY fault tests verify old bytes for
  the former and complete replacement bytes for the latter; a real-Kitty
  pre-rename failure capture at `.amp/in/artifacts/m1-save-error.png` was
  inspected for mode, dirty marker, diagnostic visibility, and contrast; and
- passes full strict `make check`, full ASan+UBSan `make sanitize`, and direct
  Clang static analysis of every changed translation unit. The configured
  `make analyze` wrapper could not run locally because `clang-tidy` is not
  installed; hosted Linux CI owns that tool-backed check.

These checks closed the automated evidence gaps. The user acceptance recorded
below closes the remaining manual gate.

### User acceptance checklist

The final user pass uses a disposable fixture, never the real vault:

1. Confirm `i`, `I`, `a`, `A`, `o`, `O`, `x`, `r`, Insert/Replace leave,
   `:w`, `:q`, and `:wq` feel usable.
2. Confirm `d`/`c`/`y` with `iw`, `aw`, `iW`, and `aW`, then `p`/`P` and undo.
3. Enter prose, `$…$`, `\ce{…}`, TikZ, a comment, and a fenced code block;
   verify highlighting/conceal stay in their zones after multiline edits.
4. Expand representative automatic, explicit, postfix, mirrored, nested,
   chemistry, and TikZ snippets. Confirm `<C-l>` moves forward and `<C-h>`
   backward while Tab remains indentation.
5. Exercise wrapped display-row and source-line motion, then toggle wrap,
   numbers, cursorline, and conceal from the `\` prefix.
6. Open the active-snippet and undo-history overlays, edit a picker query, and
   confirm `g` shows only currently available continuations.
7. Save, quit, reopen, and compare the plain Markdown bytes with what was
   entered. Confirm the mode/file/dirty/position status remains readable.

No row may be changed from **open** to **verified** from a worker report alone.
The integration branch must contain the implementation, the named checks must be
rerun there, and user-visible rows must be exercised through the production
binary.

**Acceptance result:** The user accepted Milestone 1 on 2026-10-01 after the
production demo, feedback fixes, clean-room 52-row audit, and final release-gate
verification. This closes Milestone 1; Milestone 2 remains unstarted until the
user explicitly directs it to begin.

## Milestone 2 closure

Status: **open.** Slice S1 (this harness) records the table; no row is verified
until its owning slice lands and its named checks run on the integration branch.
Slice numbers and `R` ids come from the Slices table in `docs/ROADMAP.md`.

### Acceptance criteria

| Criterion (ROADMAP Milestone 2) | Owning slice | Disposition |
|---|---|---|
| Switching an uncached note to rendered mode returns without waiting | S6 | **Open** |
| Successful formula, chemistry and TikZ blocks replace their placeholders | S6 (pipeline from S3) | **Open** |
| One failing block shows its mapped diagnostic while successful blocks remain visible | S6 (diagnostics from S4) | **Open** |
| Changing source, preamble, colors, cell metrics or engine invalidates only incompatible artifacts | S3 | **Open** |
| No partial artifact is displayed | S3 | **Open** |
| Source and inverse jumps land on the specified line/fallback, at line granularity inside multi-line blocks | S9 (reader from S8) | **Open** |
| A missing or unparseable sidecar degrades to scrolling and says so | S9 | **Open** |
| Render and SyncTeX fuzz targets pass | S4 (TeX-log parser) and S8 (SyncTeX reader) | **Open** |

### Scope items

| Scope item (ROADMAP Milestone 2) | Slice | R ids | Disposition |
|---|---|---|---|
| Acceptance harness, closure table, fixture vault, demo skeleton, dependency ADR, CI TeX subset | S1 | R6.1.1-R6.1.3 dependency gate | **Open** |
| LuaLaTeX with explicit pdfLaTeX override | S2 | R6.1.1 | **Open** |
| Vault, note and block preambles | S2 | R2.3, R2.8, R2.9, R1.8.14 | **Open** |
| One changed-block TeX run per note | S3 | R6.1.2 | **Open** |
| Poppler rasterization | S3 | R6.1.3 | **Open** |
| Color/cell-aware render keys | S3 | R6.1.4, R1.12.8 | **Open** |
| Precompiled preambles and cache warming (on open and after cwiki's own writes) | S3 | R6.1.4, R1.14.3 | **Open** |
| Atomic artifact swaps | S3 | R1.8.19 | **Open** |
| Structured TeX diagnostics and raw logs | S4 | R6.5 | **Open** |
| Kitty shared-memory images, placeholders, z-index | S5 | R6.1.3, R9.3.1 | **Open** |
| OSC 66 headings | S6 | R6.3.2 | **Open** |
| Editing/rendered mode separation | S6 | R1.8.1-R1.8.3, R1.12.6, R1.12.9, R6.2 | **Open** |
| Markdown layout | S6 | R6.3.1, R1.8.10 | **Open** |
| Partial failure | S6 | R6.5.3 | **Open** |
| Multiple windows for a source/rendered split | S7 | R1.8.2, R1.12.2-R1.12.5 (M2 subset) | **Open** |
| SyncTeX sidecars for multi-line blocks (reader) | S8 | R1.8.11a, R1.8.12-R1.8.15 | **Open** |
| Line-granular source/render synchronization | S9 | R1.8.4-R1.8.6, R1.8.8, R1.8.9, R1.8.16-R1.8.18 | **Open** |
| Render search navigation (`/`, `n`/`N`) | S10 | R6.3.3 (M2 part), R6.3.3a | **Open** |

### FEATURES rows assigned to M2

Each of the 28 `M2` rows in `docs/FEATURES.md` is named in exactly one row.

| Slice | FEATURES rows (by feature name) | Disposition |
|---|---|---|
| S2 | Per-note engine and preamble directives; Inline math and display math delimiters; mhchem chemistry notation; TikZ diagrams; Vault-wide preamble plus per-note/per-block additions; Compile one block against the note's preamble | **Open** |
| S3 | One TeX run per note for changed blocks only; Render cache plus precompiled preamble, warmed in background; Never display an artefact that is being written | **Open** |
| S4 | Structured TeX-log to diagnostics; Diagnostic filtering; Detect which packages a note actually uses | **Open** |
| S5 | Terminal-native image display via kitty graphics protocol; Images rendered at terminal's font size/colors; Full kitty graphics protocol; Terminal colour queries; Terminal-background sync | **Open** |
| S6 | Separate editing and rendered modes; Sign column always reserved; LaTeX errors shown in place of the formula; OSC 66 text sizing; Source position to rendered position mapping | **Open** |
| S9 | Rendered position to source position mapping; Treat position mapping as an optional capability | **Open** |
| S10 | Quickfix-list toggle (diagnostics pane); Notification history; Timestamped internal message log; State dump | **Open** |

### Deferred

| Id | Item | Owning milestone | Decision source | Disposition |
|---|---|---|---|---|
| R1.12.7 | Pull-triggered cache warming and external-write refresh | M3 | ROADMAP Slices note (depends on M3 change detection) | **Deferred** to M3 |
| R6.4.1 | Trusted-vault shell-escape override | M3 | ROADMAP Slices note; M2 always passes `-no-shell-escape` | **Deferred** to M3 |
| R1.12.9a | Per-row stale render markers in the sync sidebar | M5 | SPEC R1.12.9a; ADR 0002 | **Deferred** to M5 |
| R6.3.3a | Rendered link focus, Enter to follow and back stack | M4 | SPEC R6.3.3a (2026-10-11), commit 42e6aea | **Deferred** to M4 |
| R1.8.7 / R1.8.7a | Sync jump-list recording and its toggle | M5 | SPEC R1.8.7a (2026-10-11), commit 42e6aea | **Deferred** to M5 |

### Fixture hand-compile record

The render fixture's hand-compile commands and result are recorded in
`tests/fixtures/vaults/README.md`.

### User acceptance checklist

Mirrors `scripts/m2-demo-steps.txt`; each step becomes runnable when its slice lands.
Run on a disposable fixture, never the real vault (`make m2-demo`).

1. Open the fixture note in source and rendered windows. (S7)
2. Save an edit; the rendered view refreshes from disk. (S6)
3. Observe non-blocking stale/placeholder behaviour and the eventual image. (S6)
4. Trigger a TeX error and see its diagnostic in place. (S6)
5. Inspect the failing block's raw TeX log. (S10)
6. Jump source to rendered and rendered to source. (S9)
