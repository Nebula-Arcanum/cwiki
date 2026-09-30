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

Status: **open**. Milestone 2 is paused until this table closes and the user
accepts the Milestone 1 demo.

| Requirement slice | Sources | Current evidence | Closure needed |
|---|---|---|---|
| Kitty capability gate, raw mode, keyboard protocol, synchronized updates, ordered crash restoration | R1.5; M1 terminal rows | PTY protocol/fault/signal tests; real-kitty startup exercised | Production acceptance scenario for unsupported capability and reset path |
| Raw key recording and replay-safe crash flush | R1.5.9 | Unit/fault tests and terminal crash integration | Include rejected-input/replay artifact in production acceptance evidence |
| UTF-8 buffer, grapheme edits, positions, undo | R1.1–R1.3 | Model, sanitizer, editor, and PTY tests | Production scenarios include combining and four-byte graphemes plus undo |
| Plain Markdown load/save and durable failure semantics | R1.12; M1 acceptance | Round-trip and injected durable-write tests | Production save/reopen plus fault scenario in release gate |
| Normal/Insert/Replace/Command-line and bounded M1 edits | R1.3; M1 scope | Editor/input/PTy tests include `I A o O x r`, `d c y`, put, text objects | Broaden production acceptance replay beyond the original happy path |
| Approved M1 motions and display/source-row behavior | R1.3, R1.7; M1 scope | Motion/layout/editor tests | Production acceptance covers wrapped movement and operator ranges |
| Zone stack, parser spans, source highlighting, degraded fallback | R1.4, R1.9 | Parser/highlight tests and fuzzing | Implement and accept `% VimTeX: SynIgnore on/off` |
| Conceal categories, mappings, reveal lifecycle | R1.6 | Conceal/layout/editor/PTy tests; inspected real-kitty frame | Add runtime enable/disable action and production acceptance |
| Soft wrap, code no-wrap, continuation rows | R1.7 | Layout/render tests | Add per-window `\w` action and production acceptance |
| Relative numbers and smart cursorline | M1 display rows | Not implemented | Implement, configure, and accept in real kitty |
| Three-space indentation defaults | M1 indentation row | Not implemented; prior literal-Tab wording was not user-confirmed | Add `shiftwidth`, `tabstop`, and `expandtab`; ordinary Tab defaults to three spaces |
| Snippet engine, contexts, mirrors, transforms, nesting, subject layers | R1.10 | Engine/catalog/config tests and app PTY coverage | Broaden production acceptance across prose/math/chemistry/TikZ/comment/code, auto/regex/postfix, mirrors, nesting, undo |
| `<C-l>` expansion/forward and `<C-h>` backward snippet navigation | R1.10.2; user acceptance | Editor-input and PTY regression tests | Include both directions in production acceptance replay |
| List snippets active at cursor | R1.10.10c | Not integrated | Add a named action and clue/overlay presentation with context tests |
| Named actions, physical-key maps, rebinding, removal, clue popup | R1.14.1 | Registry/keymap/config/clue/app tests | Production acceptance covers default and configured prefixes |
| Runtime option toggles | R1.7.2; M1 toggle row | Not integrated | Add named actions for M1 display options with visible state feedback |
| Per-subsystem disable switches | R1.14.3 | Conceal has category config but no top-level switch | Ship the switch for each M1 subsystem; later subsystem switches ship with their owner |
| Declarative configuration and provenance inspection | R7.2 | Strict schema/layering tests; `--inspect-config` | Acceptance scenario for global/vault/local precedence and rejected config |
| Reusable line editor and M1 prompt surface | M1 input-prompt rows | Line-editor component tests only | Integrate the shared editor into the M1 command/prompt path and exercise its controls |
| Reusable picker/float model | M1 scope; R9.1 | Model and renderer tests | Foundation only in M1; Notes provider owns indexed data in M4, other providers M5 |
| Vault recognition | R2.1.2, R9.1.2; M1 fixture scope | `.cwiki` ancestor discovery for config and explicit CLI note path | Production fixture acceptance; configured default and vault picker follow their provider workflow |
| Persistent lightweight status | M1 scope | Mode, filename, dirty state, source position rendered | Accept core fields; render status arrives M2 and Git/sync status M3 |
| Structural search and Vim-pattern translation | R1.9.4, R1.11 | Strict tests, deterministic fuzzing, analyzer | Foundation acceptance through checks; consumers follow owning commands |
| OSC 52 clipboard | R1.16 register scope; clipboard feature row | No M1 clipboard action exists | Deferred to M5 system-clipboard actions; terminal transport ships with that user path |
| Production acceptance and demo | M1 outcome/acceptance/demo | Original test-only replay covers one happy path | Replace with requirement-traceable production scenarios, real-kitty inspection, and user checklist |

No row may be changed from **open** to **verified** from a worker report alone.
The integration branch must contain the implementation, the named checks must be
rerun there, and user-visible rows must be exercised through the production
binary.
