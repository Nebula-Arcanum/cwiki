# cwiki roadmap

Status: **specification approved; Milestone 1 complete and accepted; Milestone
2 in progress (started 2026-10-11).**

The approved specification is the implementation authority. `docs/FEATURES.md`
assigns every kept or changed inventory row to exactly one milestone; dropped
rows remain unassigned. Milestones are ordered first by dependency and data
safety, then by the workflow priorities in `docs/PRODUCT.md`. Milestone 1 passed
its user ship/test gate on 2026-10-01. Milestone 2 is eligible to proceed but
remains unstarted until the user explicitly directs it to begin.

Effort uses focused implementation tasks: **S** = under one task, **M** = 1–3,
**L** = 4–10, and **XL** = more than 10. A milestone total is a planning range,
not a calendar or model-session estimate. Before implementation first introduces
a packaged dependency, revalidate its Arch Linux, Homebrew, and FreeBSD package.

## Milestone 0 — verification harness

Status: **complete.** Local verification and the replay demo pass; hosted
[CI run 36490600390](https://github.com/Nebula-Arcanum/cwiki/actions/runs/36490600390)
passed on Arch Linux, macOS with Homebrew, and FreeBSD 14.5.

**Outcome.** Establish the build, test, replay, and review system before product
behavior exists. This milestone contains the harness, not product features.

**Scope.** Portable non-GNU build entry points; strict-warning builds on Arch
Linux, macOS with Homebrew, and FreeBSD; ASan and UBSan everywhere; leak
detection and clang-tidy on Linux; GitHub Actions with cancellation and package
caching; key recording/replay; screen snapshots; fixture vaults; automated
change review; and the simple reference buffer used by later model-based tests.
Parser, SyncTeX, and Vim-regex-translation fuzz targets are wired into the
harness when those components arrive rather than stubbed here.

**Acceptance criteria.** A no-product-code smoke target builds cleanly on all
three systems; CI runs the same checks documented for local use; one recorded
key fixture replays deterministically into an approved screen snapshot; sanitizer
and static-analysis failures make the run fail; fixture-vault tests cannot resolve
to a path outside their temporary root.

**Demo.** Run the documented local verification command, replay the seed key
fixture, and show the matching snapshot and a CI matrix run.

**Dependencies.** None.

**Risks.** Cross-platform compiler and sanitizer differences; flaky terminal
snapshots; accidentally coupling the harness to a product architecture not yet
implemented.

**Effort.** L, 6–9 focused tasks.

## Milestone 1 — daily class-note skeleton

Status: **complete and accepted on 2026-10-01.** The requirement-to-evidence
gate in `docs/ACCEPTANCE.md` is closed. Full strict and sanitizer integration
checks, recorded production/real-Kitty scenarios, a clean-room reconciliation
against all 52 M1 feature rows, and the user's manual acceptance pass completed
the gate. Milestone 2 is unblocked but awaits the user's start command.
Production-CLI lifecycle coverage, real-Kitty Unicode-width conformance, and
visible pre-/post-rename save-failure coverage guard the closed release gate.
The UTF-8 line buffer and position fix-up, in-session
undo tree, kitty key/bracketed-paste and startup-capability parsers, and
durable-write primitive are integrated. The terminal lifecycle now provides raw
mode, capability probing, synchronized updates, and signal-safe restoration.
Its always-on bounded raw-input recorder captures bytes before parsing and can
flush a valid replay artifact through the terminal crash path.
The incremental zone-stack foundation covers the first Markdown/LaTeX contexts,
uses the shared bounded PCRE2 runtime, and visibly marks lines where a regex
resource limit makes zone state untrustworthy. Typed Vim patterns translate
across all four magic modes with Unicode smartcase, while unsupported constructs
are rejected rather than guessed. The conceal foundation supplies the accepted
category defaults, width-safe substitutions, source↔display mappings, and
per-run reveal behavior. Per-window layout adds prose soft wrap, unwrapped code,
degraded raw rows, and source↔display movement mappings. The snippet-engine
foundation supplies layered zone-aware matching, the approved body syntax,
mirrors, transforms, nested tab stops navigated with `<C-l>`/`<C-h>`, and
transactional undo. Strict, sanitizer,
model, protocol, fault-injection, and parser/translator/snippet fuzz tests cover
these slices. A bounded shared structural-search primitive and named-action
registry establish the common paths for later motions, clues, palette, macros,
replay, and command dispatch. The keymap foundation adds mode-scoped physical
key sequences, rebinding, prefix matching, and deterministic clue metadata. Plain
Markdown document ownership composes buffer encoding with the durable-write
outcomes while preserving dirty state unless durability is fully confirmed. The
motion engine covers the approved Milestone 1 navigation set and core
word/WORD text objects with Unicode word/WORD and grapheme semantics, distinct
wrapped-row and source-line movement,
viewport and structural motions, conceal reveal continuity, and normalized
operator ranges. The editor model composes those ranges with `I`/`A`/`o`/`O`,
grapheme-safe `x`/`r`, Insert/Replace and Command-line modes, deterministic undo
boundaries, `d`/`c`/`y`, the unnamed
yank and `p`/`P`, dirty-state-preserving saves, and the bounded `:w`/`:q`/`:wq`
surface. Parser events now route through the shared physical-key keymap and named
action registry, including prefix sequences, associated text, and literal paste.
Source highlighting now maps every cached zone kind to immutable, grapheme-safe
semantic runs with raw fallback for untrusted lines. Parser-owned spans now give
complete Markdown and LaTeX delimiters distinct roles without a second parser.
The single-window source renderer composes those roles with conceal, wrap,
viewport clipping, cursor placement, and the persistent mode/file/position status
line into deterministic terminal frames. The executable application loop now
composes terminal capability setup and restoration, raw recording, event dispatch,
derived-state refresh, synchronized redraw, viewport following, insert-leave
saves, and plain-file reopen through fixture-tested PTYs. Horizontal motion now
carries per-run conceal reveal state through the editor into rendered frames.
The builtin catalog supplies named, overridable VimTeX-compatible symbols and
math, mhchem, and TikZ templates. Insert-mode dispatch now performs automatic
and `<C-l>`-requested expansion, nested `<C-l>`/`<C-h>` tab-stop navigation,
transactional mirror updates, paste suppression, and the specified separate
undo steps for trigger text, expansion, and subsequent stop fills. Prompt text
input now has a reusable Unicode/grapheme-safe line editor with the
approved Emacs-style controls. The shared picker model owns stable item metadata,
live-filters and deterministically ranks Unicode case-insensitive fuzzy matches,
and preserves selection across query changes. A bounded float-placement primitive
and picker overlay add configurable anchoring, borders, scrolling, clipping, and
empty-result rendering. A live clue overlay now derives every multi-key-prefix
continuation from shared keymap and named-action metadata, including action
availability, and is composed into application redraws. The bounded libyaml
configuration foundation strictly validates physical-key bindings, unbindings,
clue-group labels, conceal/display options, and save policy with source
locations, then builds a complete candidate keymap without mutating its base.
Validated configuration bytes now replace the live keymap transactionally
before terminal startup; configured clue-group labels title the live popup;
layered display settings reach layout; and manual, insert-leave, and idle save
policies drive writes. Relative line numbers, smart cursorline, three-cell Tab
indentation, runtime display toggles, active-snippet listing, and searchable
in-session undo history are integrated through named actions. All production
controls now dispatch through the action/keymap path. Layered module switches
independently disable the M1 syntax presentation, snippet, clue, and picker
subsystems, while conceal retains its per-window display switch. Name-keyed
snippet configuration transactionally composes
builtin replacement/disabling and custom literal or bounded-regex definitions,
zone-dependent bodies, trigger flags, priorities, and subject layers. Read-only
startup discovery composes existing global, synchronized-vault, and UUID-keyed
machine-local files without creating vault metadata. The custom-zone schema adds
bounded named regions, parent/child containment, line-ending regions, detail
captures for hosted languages, and layered replacement/disabling without
allowing builtin grammar removal. Opening YAML frontmatter selects all declared
subject snippet layers without pulling Milestone 4's typed-property system into
the editor foundation. A noninteractive inspection command reports the effective
M1 scalar settings, subject layers, keybindings, clue groups, snippets, and zones
with source locations. The real-application fixture-vault demo now replays a
chemistry/calculus note through snippets, tab stops, undo, save, quit, and reopen,
and checks both the rendered screen and authored Markdown. The M1 picker model
and floating surface are complete; concrete providers follow their owning
workflows: indexed notes and full-text search in M4, then buffers, help, resume,
and the command palette in M5.

**Outcome.** A fixture vault can be opened in kitty and used to take a source-mode
class note quickly, with snippets and source highlighting. Rendered mode is not
part of this milestone.

**Scope.** The C application skeleton; kitty capability and keyboard protocols;
synchronized redraw; the UTF-8 line buffer and position fix-up; basic
Normal/Insert/Replace/Command-line operation; the first `d`/`c`/`y` operator
slice over the approved M1 grapheme, word/WORD, display/source-line, document,
viewport, line, paragraph, and sentence motions plus core
`iw`/`aw`/`iW`/`aW` text objects; `I`/`A`/`o`/`O` and grapheme-safe `x`/`r`;
one unnamed yank slot with `p`/`P`; in-session undo; source highlighting
from the incremental zone stack; conceal; prose soft-wrap; the PCRE2 policy and
Vim-pattern translator needed by regex snippets; snippet tab stops, mirrors,
contexts, named transforms, subject layering, and the curated math/mhchem/TikZ
defaults; named actions, keymaps, status line, prompts, and the first reusable
picker/clue-popup surfaces; declarative configuration foundations; fixture-vault
selection; and plain Markdown load/save through the durable-write path.

**Acceptance criteria.** In a fixture vault, a recorded chemistry/calculus note
can be entered using context-gated snippets without expansions crossing prose,
math, `\ce{}`, TikZ, comment, or code zones; source highlighting and conceal
remain correct after multiline edits; grapheme motion and editing preserve valid
UTF-8; undo restores the exact prior bytes; injected failures before rename
leave both the original file and dirty buffer intact, while a post-rename
directory-sync failure reports durability uncertain, leaves the buffer dirty,
and leaves either complete old or complete new bytes on disk; missing kitty
capabilities produce the specified exit message; model-based buffer tests and
parser/regex fuzz targets pass under sanitizers.

**Demo.** Replay a short class-note recording into a fixture vault, show source
highlighting and conceal, exercise snippet tab stops and undo, save, reopen, and
show the authored Markdown. The release gate additionally runs the production
executable through the M1 closure scenarios in `docs/ACCEPTANCE.md` and requires
an inspected real-kitty run plus the user's manual acceptance. A test-only demo
entry point or synthetic raster is supporting evidence, not acceptance.

**Dependencies.** Milestone 0.

**Risks.** The zone stack, display/source mapping, operator grammar, and undo are
the first schedule-dominating components. Keep the operator slice narrow and
defer counts, broader text objects, block Visual mode, additional operators,
the full register model, and broad Vim parity.

**Effort.** XL, 24–32 focused tasks.

**Decision resolved.** R1.6.8 uses the balanced default conceal profile:
accents, Greek, math symbols, ligatures, fractions, size-modified delimiters
and simple sub/superscripts are enabled; structural or invisible categories are
opt-in. Accepted and rejected alternatives are recorded in `docs/SPEC.md`.

## Milestone 2 — rendered notes

**Outcome.** A saved note opens immediately in a viewer-style rendered window;
math, mhchem, and TikZ fill in asynchronously through the selected TeX/PDF/PNG
pipeline without blocking editing.

**Scope.** Editing/rendered mode separation; multiple windows needed for a
source/rendered split; Markdown layout; LuaLaTeX with explicit pdfLaTeX override;
vault, note, and block preambles; one changed-block TeX run per note; Poppler
rasterization; kitty shared-memory images, placeholders, z-index, and OSC 66
headings; color/cell-aware render keys; precompiled preambles and cache warming;
atomic artifact swaps; partial failure; structured TeX diagnostics and raw logs;
rendered scrolling and search (link focus/follow ships in M4, R6.3.3a); and line-granular source↔render synchronization,
including SyncTeX sidecars for multi-line blocks (R1.8.11a).

**Acceptance criteria.** Switching an uncached note to rendered mode returns
without waiting; successful formula, chemistry, and TikZ blocks replace their
placeholders; one failing block shows its mapped diagnostic while successful
blocks remain visible; changing source, preamble, colors, cell metrics, or engine
invalidates only incompatible artifacts; no partial artifact is displayed; source
and inverse jumps land on the specified line/fallback, at line granularity
inside multi-line blocks; a missing or unparseable sidecar degrades to scrolling
and says so; render and SyncTeX fuzz targets pass.

**Demo.** Open one fixture note in source and rendered windows, save an edit,
observe non-blocking stale/placeholder behavior and the eventual image, trigger a
TeX error, inspect its raw log, and jump in both directions.

**Dependencies.** Milestone 1.

**Risks.** Asynchronous ownership and cancellation, TeX log attribution,
placeholder/image layout, cross-platform TeX package subsets, and SyncTeX
precision. CI installs only packages exercised by fixtures and caches them.

**Effort.** XL, 20–28 focused tasks, including the SyncTeX sidecar reader, its
parse cache and fuzz target.

**Decisions resolved.** SyncTeX sidecars ship in this milestone (R1.8.11a,
accepted 2026-10-11). Per-row stale render markers are not part of this
milestone; they ship in Milestone 5, optional and off by default (R1.12.9a,
decided 2026-10-11). The status-line stale indicator remains in scope. Sync
jump-list recording moves to Milestone 5 (R1.8.7a) and rendered link navigation
to Milestone 4 (R6.3.3a), both decided 2026-10-11. No Milestone 2 decision
remains open.

### Slices

Ordered vertical slices. Each is independently testable; every M2 scope item
and acceptance criterion lands in exactly one. Pull-triggered cache warming and
external-write refresh (R1.12.7) depend on M3 change detection; M2 warms on
open and after cwiki's own writes. The trusted-vault shell-escape override
(R6.4.1) ships with M3's machine-local trust policy; M2 always passes
`-no-shell-escape`.

| # | Slice | Covers | Depends on | Delivery | Status |
|---|---|---|---|---|---|
| S1 | Acceptance harness: M2 closure table, render fixture vault (math, mhchem, TikZ, failing block, multi-line derivation), demo skeleton, TeX Live/Poppler dependency ADR with package revalidation, CI TeX package subset | M2 acceptance criteria and demo; R6.1.1–R6.1.3 dependency gate | — | d-20261011-ff71bd | in progress |
| S2 | Block extraction and TeX source generation: renderable blocks from zone data, environment availability tables and preamble scan, frontmatter `packages:`/`preamble:`/engine, vault preamble, `% cwiki:` directives, `latex` fences as code, standalone template with temp-line→note-line table | R2.3, R2.8, R2.9, R6.1.1, R1.8.14 | S1 | — | pending |
| S3 | Render pipeline: asynchronous one-run-per-note TeX job for changed blocks, `-no-shell-escape`, Poppler rasterization, cache keys and OS cache location, atomic artifact swap, SyncTeX `-synctex=-1` sidecars for multi-line blocks, precompiled preamble and warm-on-open, cancellation, background-rendering switch | R6.1.2–R6.1.4, R1.8.11, R1.8.19, R1.12.8, R6.4.2, R1.14.3 | S2 | — | pending |
| S4 | TeX diagnostics: stateful log parser with file stack, context and continuation lines, regex filters and error/warning policy, mapping to note lines, raw log, package detection from `.fls`, log-parser fuzz target | R6.5, FEATURES TeX-diagnostic rows | S3 | — | pending |
| S5 | Terminal graphics and metrics: cell pixel size, foreground/background colour queries with focus-return refresh and light/dark theme variants, kitty shared-memory transfer, Unicode placeholders, z-index | R6.1.3, R6.1.4 inputs, R9.3.1, kitty graphics rows | S1 | — | pending |
| S6 | Rendered mode in one window: per-window mode switch that never waits, Markdown prose layout, OSC 66 headings, inline formula row expansion and baseline, placeholders and newest valid artifact, in-place errors, render on write only, status-line stale indicator, row↔source tables, editing-mode diagnostic gutter with reserved sign column, rendered scrolling | R1.8.1–R1.8.3, R1.8.10, R1.12.6, R1.12.9, R6.2, R6.3.1, R6.3.2, R6.5.3 | S3, S4, S5 | — | pending |
| S7 | Source/rendered split: two windows on one buffer with per-window state, cross-window position fix-up, split/close/focus actions | R1.8.2, R1.12.2–R1.12.5 (M2 subset) | S6 | — | pending |
| S8 | SyncTeX reader: one-page record tree, innermost-box lookup, bounded parse cache, temp-line mapping, fuzz target | R1.8.11a, R1.8.12–R1.8.15 | S3 | — | pending |
| S9 | View sync: forward/inverse continuous or explicit per direction, global and per-window toggles, minimal forward scroll, sidebar reticle, no-output-line fallback, post-jump cue, degrade to scrolling when mapping is unavailable | R1.8.4–R1.8.6, R1.8.8, R1.8.9, R1.8.16–R1.8.18 | S7, S8 | — | pending |
| S10 | Rendered search and observability: `/` search with `n`/`N` over rendered text, internal message log, notification history, diagnostics pane toggle, state dump | R6.3.3 (M2 part), R1.14.5, R1.14.6, FEATURES M2 log/notification/pane rows | S6 | — | pending |

## Milestone 3 — data-safe vault and Git sync

**Outcome.** The data-safety gate passes: cwiki can be demonstrated against a
copy of a real vault without risking authored content, and explicit Git Sync can
integrate concurrent changes without discarding dirty buffers.

**Scope.** Recovery journals and persistent undo; external file watching; clean
reload and undo-tree rollover; dirty-buffer three-way merge; conflict view and
all-hunks-resolved gate; trash/restore; explicit save/commit/fetch/rebase/push;
Git status, diff, history, retry, and conflict surfaces; shell-escape trust policy;
and global, synchronized-vault, and machine-local configuration boundaries.

**Acceptance criteria.** Fault-injection at every durable-save step preserves the
old or complete new file; crash recovery is offered only for the matching content
hash; clean external edits reload with views preserved; dirty edits enter the
three-way merge without overwriting either side; conflict markers are never parsed
or partially saved; Sync stops visibly at every specified failure and never
rewrites a pushed commit; shell escape cannot be enabled by synchronized content.

**Demo.** Use two disposable clones of a fixture vault to show clean reload,
dirty-buffer conflict resolution, interrupted recovery, successful Sync, and a
stopped/retryable Sync. After this demo passes, the user may separately authorize
testing against a copy of the real vault; the real vault itself remains off limits
without explicit approval.

**Dependencies.** Milestones 0–2.

**Risks.** Filesystem durability varies by OS; watcher event coalescing; Git
failure-state recovery; preserving the base version for a correct three-way merge.

**Effort.** XL, 14–20 focused tasks.

## Milestone 4 — linked knowledge base

**Outcome.** Notes form a searchable wiki with deterministic cross-platform link
resolution and reusable navigation/query surfaces.

**Scope.** SQLite lifecycle and rebuilds; transactional watching/index updates;
frontmatter and typed properties required by approved workflows; indexed titles,
aliases, headings, links, backlinks, properties, TODO comments, and attachment
metadata; exact normalized link resolution and collision guard; wikilinks,
heading links, completion, backlinks, and outgoing links; the Notes browser and
outline; rendered-mode link focus, follow and back stack (R6.3.3a); full-text, phrase, Boolean, typed, property, and bounded regex queries;
saved searches; multiple-vault selection; attachments folder; and explicit
rename-and-fix-links.

**Acceptance criteria.** A cold rebuild and incremental update produce identical
query results; corruption triggers rebuild; queries never observe a partial
transaction; NFC/NFD equivalents resolve while case mismatches do not; collisions
are refused/reported; code and LaTeX remain text-searchable but cannot create false
structured records; all query forms return independently derived expected rows;
the Notes browser, outline, completion, and backlinks reuse the shared index.

**Demo.** Browse and search a multilingual fixture vault, complete and follow a
heading link, inspect backlinks/unlinked mentions, demonstrate normalization and
collision behavior, modify a file externally, and show the transactional update.

**Dependencies.** Milestones 1 and 3; rendered-link navigation also uses 2.

**Risks.** Unicode normalization, stale watcher events, query grammar ambiguity,
and index/schema drift. Authored files remain authoritative.

**Effort.** XL, 18–24 focused tasks.

## Milestone 5 — complete authoring workspace

**Outcome.** The daily editor grows from the Milestone 1 slice into the approved
Vim/vimtex-style authoring workspace without delaying the first usable build.

**Scope.** Full operator/count grammar; complete text objects; character/line and
block Visual modes; registers, macros, marks, jump list, and conditional change
list, including sync-jump recording (R1.8.7a); search, substitute, formatting, filters, dot repeat, surround, LaTeX motions
and manipulations; folding and indentation; tabs, split layout, sidebars, buffer
reuse/removal, and session policy; command/environment/file completion; command
palette, contextual help, themes, and the remaining accepted editor polish; and
the optional, off-by-default per-row stale-render markers in the sync sidebar
(R1.12.9a).

**Acceptance criteria.** The operator×motion/text-object matrix passes generated
tests over wrapped, concealed, Unicode, empty, and ragged lines; dot-repeat and
macro replay produce one coherent undo history; block Visual operations obey
virtual columns; LaTeX searches remain bounded and comment-safe; folds and split
views are per-window; key conflicts are rejected; editor state restores only when
its validation guards match.

**Demo.** Replay an advanced authoring fixture using operators, macros, block
Visual mode, LaTeX objects/toggles, folds, command completion, multiple tabs and
splits, then close/reopen the workspace.

**Dependencies.** Milestones 1–4.

**Risks.** Combinatorial editor semantics and regressions in the class-note path.
Ship as small vertical tasks behind the established replay/model harness.

**Effort.** XL, 30–42 focused tasks.

**Decisions due before coding.** Once the jump list exists, retain `g;`/`g,` only
if the change list is still cheap under the implemented position model. Decide
whether named sessions beyond the required implicit session justify inclusion.
Record both outcomes in `docs/SPEC.md`.

## Milestone 6 — flashcard study

**Outcome.** Cards can be authored in a deck browser and reviewed daily with
Anki-like scheduling, rich rendered content, and merge-safe history.

**Scope.** Card-note files, deck directories, subjects and source links; declarative
note types, fields, forward/reverse templates, conditionals, and cloze expansion;
deck browser and counts; pure versioned SM-2 scheduling; append-only review log,
union-merge setup, deterministic fold and watermarks; queue ordering, daily limits,
sibling burying, leeches, overdue handling, undo/edit/suspend/bury/mark; Custom
Study; browsing, shared queries, initial textual statistics, and due-card pre-render.

**Acceptance criteria.** Golden scheduling vectors cover every grade, learning,
lapse, elapsed-time, and overdue boundary; replaying shuffled/merged history in
the specified order yields the same state and detects divergent stored `post`
values; duplicate IDs are ignored; two-clone offline reviews union-merge without
markers and both count; generated sibling cards render from one note; due-card
pre-render does not block review start.

**Demo.** Create a basic-reversed and cloze note, review them through all grades,
undo/edit one grade, merge offline histories from two fixture clones, browse the
audit trail, and run a query-backed Custom Study session.

**Dependencies.** Milestones 2–4; Milestone 5 is not a blocker.

**Risks.** Scheduling replay correctness, append-only enforcement, template edge
cases, and render-prefetch pressure.

**Effort.** XL, 16–22 focused tasks.

## Milestone 7 — calendar time-blocking

**Outcome.** Events can be planned from a keyboard-driven week grid, recur across
time zones correctly, notify while cwiki runs, and export as iCalendar.

**Scope.** Sorted daily files and recurring-definition files; structured event
form and deterministic quick-add; month/week/day views; overlap layout; move and
resize; RFC 5545 RRULE subset including required BYxxx, overrides, exclusions,
and series splitting; IANA zones and DST policy; OSC 99 reminders; stable UID
iCalendar export; and event search.

**Acceptance criteria.** Property tests compare recurrence expansion to independent
golden cases across leap dates, month ends, DST gaps/folds, exclusions, and series
splits; concurrent inserts at different times merge cleanly; overlap layout is
stable; every view edits through the same form; export preserves stable identities,
zones, recurrence, and overrides.

**Demo.** Plan a fixture week, create overlapping and recurring events, move one
occurrence and split future occurrences, cross a Toronto DST boundary, fire an
OSC 99 reminder, and inspect the exported `.ics`.

**Dependencies.** Milestones 3–4.

**Risks.** RFC 5545 breadth, timezone database behavior, and layout density. Keep
CalDAV and closed-app reminders out of this core slice.

**Effort.** XL, 15–21 focused tasks.

## Milestone 8 — task planning

**Outcome.** Inline and whole-note tasks roll up into a daily dashboard and can be
scheduled into one or more calendar work blocks without duplicating source data.

**Scope.** Extended checkbox states and trailing metadata; whole-note tasks;
stable IDs on first external reference; Inbox/Overdue/Today/Upcoming/Someday
dashboard; query, sort, completion, edit, and archive actions; schedule- and
completion-relative recurrence with collapsed missed occurrences; task work blocks
in calendar views; and list/calendar project-ready sections.

**Acceptance criteria.** Zone-aware indexing ignores task-looking text in invalid
zones; dashboard edits use the durable path and update the exact source record;
due dates remain independent of work blocks; multiple blocks reference one task;
recurrence boundary tests distinguish schedule from completion semantics and
collapse missed occurrences; completing a task confirms before cancelling future
blocks.

**Demo.** Create inline and whole-note tasks, query each dashboard section,
complete a recurring overdue task, schedule it into two calendar blocks, move a
block, and inspect the unchanged single source of truth.

**Dependencies.** Milestones 4 and 7.

**Risks.** Stable identity for inline text, source-preserving rewrites, recurrence
interaction, and stale index projections.

**Effort.** XL, 10–15 focused tasks.

## Milestone 9 — projects

**Outcome.** Project notes aggregate their explicit notes, tasks, events, work
blocks, and deck status into one configurable home without duplicating data.

**Scope.** Project identity, hierarchy, status, outcome, dates, and membership;
query-backed project home sections; list and calendar sections; subtask rollups;
and project-aware search and pickers.

**Acceptance criteria.** Only explicit `project` properties confer membership;
links and backlinks alone do not; every section agrees with its equivalent shared
query; edits reach source records; parent/child and archived states remain
consistent; deleting/rebuilding the index preserves the same project home.

**Demo.** Build a fixture course project from a note, tasks, time blocks, an event,
and a deck; change each source and show the home update after one transactional
index refresh.

**Dependencies.** Milestones 4, 6, 7, and 8.

**Risks.** A project home becoming a bespoke second query/view system. Keep it a
composition of existing index records and UI primitives.

**Effort.** L, 6–9 focused tasks.

## Milestone 10 — optional enrichments and companions

**Outcome.** Isolate approved but nonessential, expensive, or ecosystem-facing
work so none of it delays the core workflow milestones.

**Scope.** Deferred editor conveniences such as 2D jump and split/join; block
references, partial/query/image embeds, media handoff, property maintenance, daily
date links, and other optional note enrichments; image occlusion, flags, richer
statistics, and FSRS after real review history exists; reminder daemon and reviewed
external notifier; CalDAV companion; natural-language and CLI quick-add;
task/project Kanban; terminal hyperlink polish; TeX documentation lookup; and
other inventory rows explicitly assigned M10.

**Acceptance criteria.** Each item receives its own acceptance criteria and demo
before implementation. Companion processes remain optional and machine-local;
external commands use reviewed fixed integration slots; optional features do not
change core file meanings or weaken resource, sync, or data-safety guarantees.

**Demo.** Defined per selected item; M10 is a portfolio, not one release gate.

**Dependencies.** The owning core milestone for each item; FSRS additionally
requires representative retained review history.

**Risks.** Unbounded scope and low-value integrations. Start an M10 item only
after the user explicitly selects it for implementation and any newly exposed
product choices are recorded in `docs/SPEC.md`.

**Effort.** XL and intentionally unbounded as a portfolio; estimate each selected
item independently.

## Milestone operating rules

- Implement milestones in order except that independent M10 items may be selected
  only after their owning core milestone is complete.
- Before each milestone, turn its acceptance criteria into executable checks and
  a user-runnable fixture-vault demo. Never use the real vault before the Milestone
  3 data-safety demo passes, and never modify it without explicit approval.
- Parser, SyncTeX, and Vim-regex-translation fuzz targets start with their owning
  components and remain in all later combined checks.
- Install only test-required TeX packages in CI and cache them.
- Keep shared interfaces and tightly coupled changes in the orchestrator. Every
  independent worker uses a separate worktree and branch, commits locally, and
  never pushes.
- After each verified task, update planning status and create a local checkpoint
  commit with explicit staged paths. Pushing, merging, tagging, deploying, and
  publishing require separate approval.
