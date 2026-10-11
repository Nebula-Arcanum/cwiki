# Architecture — cwiki

`docs/SPEC.md` is the architecture authority: every requirement carries an `R<section>.<n>` id
and its rejected alternatives. This file is a map into it, not a second copy. When the two
disagree, the spec wins and the disagreement is reported.

## Shape
One single-threaded C11 process, `build/cwiki`, entered at `src/main.c` and run by the loop in
`src/app.c`. It puts kitty into raw mode, probes capabilities by query rather than `$TERM`
(R1.5.7), reads kitty-keyboard-protocol events, drains all pending input, then emits one
synchronized redraw (R1.5.6). Every user-visible command is a named action reached through the
keymap (R1.14.1); nothing handles a key inline. `cwiki --inspect-config NOTE` and
`cwiki --reset-terminal` are the non-interactive entry points. There is no server, daemon or
network path in the delivered milestones.

## Module map
All product code is flat in `src/`, one `.c`/`.h` pair per module. Tests mirror it under `tests/<area>/`.

| Area | Modules | Spec |
|---|---|---|
| Text and Unicode | `buffer`, `unicode`, `undo` | §1.1–§1.3 |
| Terminal and input | `terminal`, `capabilities`, `input`, `key_record` | §1.5 |
| Parsing | `zone`, `structural_search`, `regex`, `vim_regex` | §1.9, §1.11 |
| Display | `highlight`, `conceal`, `layout`, `render` | §1.4, §1.6, §1.7 |
| Editing | `motion`, `editor`, `editor_input` | §1.7, §1.2 |
| Snippets | `snippet`, `snippet_catalog` | §1.10, R9.4.1 |
| Dispatch | `action`, `keymap` | §1.14 |
| Transient UI | `line_edit`, `picker`, `picker_render`, `float`, `clue_render` | R1.12.1a |
| Configuration | `config`, `config_files` | §7 |
| Files | `document`, `durable_write` | §3.1 |
| Application | `app`, `main` | — |

The zone engine is the one parser: highlighting, conceal, snippet gating, folds, the outline and
LaTeX objects all query its per-line zone stack (R1.9.7). PCRE2 is the one regex engine (R1.11.1).
Test seams are compile-time `CWIKI_<MODULE>_TESTING` guards in the module header.

Not yet built, with their owning milestone in `docs/ROADMAP.md`: rendered mode and the TeX/PDF/PNG
pipeline (M2), recovery journal, persistent undo, external-change merge and Git Sync (M3), SQLite
index, links and query language (M4), windows/tabs and the full Vim grammar (M5), flashcards (M6),
calendar (M7), tasks (M8), projects (M9).

## Data ownership
- **The vault** is a directory outside this repository, synchronized by Git, holding only
  user-authored plain files. cwiki writes notes solely through `durable_write` (temp file, fsync,
  rename, directory fsync). cwiki never rewrites note bytes it did not author (R1.1.2, R1.3.4).
- **Outside the vault and Git**, rebuildable or machine-local: undo trees and recovery journals in
  the OS state directory; render cache, compiled preambles and the index in the OS cache directory;
  global and machine-local configuration in the OS configuration directory (R7.2.1).
- **This repository** owns source, tests, fixtures and approved snapshots. Tests use fixture
  vaults confined to a temporary root; the real vault is off limits.
- External programs, when their milestones arrive: LuaLaTeX/pdfLaTeX and Poppler `pdftocairo`
  as subprocesses, always `-no-shell-escape` unless machine-locally trusted (§6.4); `git` for Sync.

## Environments
No dev/uat/prod. One local binary per machine; CI (`.github/workflows/ci.yml`) builds and tests
on Arch Linux, macOS with Homebrew and FreeBSD. No secrets. Dependencies must be packaged on all
three systems, and the build must not assume GNU make.

## What is deliberately not here
Decided and recorded as rejected in `docs/SPEC.md`; do not reintroduce:
- tree-sitter or per-language grammars (R1.4.1); a second parser or second regex engine
- gap buffer, piece table or rope (§1.1); a linear undo stack or time-split undo steps (§1.2)
- embedded Lua, any scripting runtime, plugin API or generic shell hooks (§7.4)
- live preview or rendering unwritten buffer content (R1.12.6, §6.2)
- mouse input, tmux/SSH operation, terminals other than kitty (R9.3.2, PRODUCT)
- swapfiles in the vault, background auto-sync, a general Git client (§1.13, §3.3, §9.2)
- tags, graph view, GUI or mobile clients, external-editor handoff (`docs/FEATURES.md`)
