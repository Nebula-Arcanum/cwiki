# cwiki roadmap

Status: **specification approved; Milestone 0 verification harness complete.**

The approved specification is the implementation authority. `docs/FEATURES.md`
assigns every kept or changed inventory row to exactly one milestone; dropped
rows remain unassigned. Milestones are ordered first by dependency and data
safety, then by the workflow priorities in `docs/PRODUCT.md`. No product-code
implementation has started.

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

Status: **in progress.** The UTF-8 line buffer and position fix-up, in-session
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
mirrors, transforms, nested tab stops, and transactional undo. Strict, sanitizer,
model, protocol, fault-injection, and parser/translator/snippet fuzz tests cover
these slices. A bounded shared structural-search primitive and named-action
registry establish the common paths for later motions, clues, palette, macros,
replay, and command dispatch. The keymap foundation adds mode-scoped physical
key sequences, rebinding, prefix matching, and deterministic clue metadata.
Application integration, the builtin snippet catalog, key dispatch, and the
remaining editor and highlighting work are not yet implemented. Plain Markdown
document ownership now composes buffer
encoding with the durable-write outcomes while preserving dirty state unless
durability is fully confirmed.

**Outcome.** A fixture vault can be opened in kitty and used to take a source-mode
class note quickly, with snippets and source highlighting. Rendered mode is not
part of this milestone.

**Scope.** The C application skeleton; kitty capability and keyboard protocols;
synchronized redraw; the UTF-8 line buffer and position fix-up; basic
Normal/Insert/Replace/Command-line operation; the first `d`/`c`/`y` operator
slice over the approved M1 grapheme, word/WORD, display/source-line, document,
viewport, line, paragraph, and sentence motions; one unnamed yank slot with
`p`/`P`; in-session undo; source highlighting
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
show the authored Markdown.

**Dependencies.** Milestone 0.

**Risks.** The zone stack, display/source mapping, operator grammar, and undo are
the first schedule-dominating components. Keep the operator slice narrow and
defer counts, text objects, block Visual mode, additional operators, the full
register model, and broad Vim parity.

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
render/search/link navigation; and line-granular source↔render synchronization.

**Acceptance criteria.** Switching an uncached note to rendered mode returns
without waiting; successful formula, chemistry, and TikZ blocks replace their
placeholders; one failing block shows its mapped diagnostic while successful
blocks remain visible; changing source, preamble, colors, cell metrics, or engine
invalidates only incompatible artifacts; no partial artifact is displayed; source
and inverse jumps land on the specified line/fallback; render and SyncTeX fuzz
targets pass where applicable.

**Demo.** Open one fixture note in source and rendered windows, save an edit,
observe non-blocking stale/placeholder behavior and the eventual image, trigger a
TeX error, inspect its raw log, and jump in both directions.

**Dependencies.** Milestone 1.

**Risks.** Asynchronous ownership and cancellation, TeX log attribution,
placeholder/image layout, cross-platform TeX package subsets, and SyncTeX
precision. CI installs only packages exercised by fixtures and caches them.

**Effort.** XL, 18–25 focused tasks.

**Decisions due before coding.** Decide whether SyncTeX sidecars ship in this
milestone or Milestone 5, and whether to include the off-by-default per-row stale
render markers from R1.12.9. Record both outcomes in `docs/SPEC.md`.

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
outline; full-text, phrase, Boolean, typed, property, and bounded regex queries;
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
list; search, substitute, formatting, filters, dot repeat, surround, LaTeX motions
and manipulations; folding and indentation; tabs, split layout, sidebars, buffer
reuse/removal, and session policy; command/environment/file completion; command
palette, contextual help, themes, and the remaining accepted editor polish.

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
