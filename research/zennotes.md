# ZenNotes — Feature Inventory

Source: github.com/ZenNotes/zennotes (desktop/web app, Electron + Go self-hosted backend), github.com/ZenNotes/tui (companion Go TUI `zn`), zennotes.org + zennotes.org/docs.

ZenNotes is a free, MIT-licensed, keyboard-first Markdown notes app. Core pitch: "real Vim underneath," plain `.md` files on disk, no hidden database, one shared core across desktop/self-hosted-web/(planned) hosted, plus a first-party MCP server for AI agent access. There is a separate official TUI client (`zn`) that reads the exact same vault.

## Editing Model
- **Real Vim engine (not emulation)** — full motions, operators, registers, marks, macros, dot-repeat, undo/redo, text objects, visual modes, search/substitute, ex command line (`:`). Vim is the default, not a bolt-on plugin.
- **CodeMirror 6 editor core** (desktop/web) — live preview, heading folding, outline extraction, syntax-highlighted fenced code blocks, configurable line numbers.
- **Edit / Preview / Split modes** — toggle or split-pane between raw markdown and rendered view without losing scroll/cursor context.
- **Optional non-Vim mode** — Vim can be disabled per config for a plain text-editor experience.
- **Command palette + which-key overlays** — leader-key (`Space`) driven discovery menus for commands, reducing need to memorize everything upfront.
- **Detached/floating note windows** (desktop) — pop a note out into its own OS window.
- **Zen mode** — strips all sidebars, tabs, and status bar down to just the text.

## Linking Between Notes
- **`[[wikilinks]]`** — Obsidian-style double-bracket links between notes, including "chase unresolved ones" (create-on-follow for links to nonexistent notes).
- **Backlinks panel** — shows in preview mode which notes link to the current note.
- **Local embeds** — `![[image.png]]`-style embedding of images/files inline, Obsidian-compatible syntax.
- **Deep links** — `zennotes://` custom URL scheme for linking into specific notes from outside the app (desktop).
- **Link-following in editor** — TUI and editor support jumping to a linked note directly from cursor position.

## Organization
- **Folders** — a vault is literally a folder tree of `.md` files; system folders (Inbox, Quick Notes, Archive, Trash) are user-relabelable/remappable to any path.
- **Tags** — freeform `#tag` anywhere in note text; dedicated vault-wide Tags view for browsing/filtering by tag.
- **Flat or hierarchical vaults** — supports Obsidian-style flat vaults (notes at vault root) as well as nested folder structures.
- **No graph view** — notably absent; ZenNotes relies on backlinks/tags/folders rather than a visual link graph.
- **Multi-select sidebar operations** — bulk actions (move, archive, trash, etc.) on selected notes in the tree.
- **Folder icons** — assignable per folder via context menu for visual scanning.
- **Assets folder** — pasted/dragged media auto-collected into a vault-level `assets/` directory.

## Search
- **Vault-wide full-text search** with swappable backends: built-in engine, or `ripgrep`/`fzf` if installed (auto-detected, or custom binary path configurable).
- **Title/path search** — fast fuzzy note-name lookup (`⎵f` fuzzy finder) separate from full-text search.
- **`zen` CLI search with JSON output** — pipeable into tools like `jq` for scripted workflows.
- **Task filter box** — searches/filters tasks by text, tag, priority token (`!high`), or custom `@key:value` metadata; filters can be saved in `config.toml`.

## Rendering (Markdown / LaTeX / Diagrams)
- **GitHub-Flavored Markdown** — tables, footnotes, callouts, standard GFM syntax.
- **KaTeX math** — inline and block LaTeX-style math rendering (Typst also selectable as an alternate math renderer, with 50–200% scaling).
- **Diagrams from fenced code blocks** — Mermaid, TikZ, JSXGraph, and function-plot render directly from code fences, no separate diagram tool needed.
- **Terminal-native rendering in the TUI** — inline images via Kitty/Ghostty terminal graphics protocols, Mermaid rendering, and math typesetting via external tool hand-off.
- **Theming for rendered output** — multiple built-in theme families (Apple, Gruvbox, Catppuccin, GitHub, Solarized, etc.) plus custom CSS themes.

## File Format & Storage
- **Plain `.md` files on disk** — no hidden/proprietary database; "a vault is just a folder."
- **Embedded media** — images, PDFs, audio, video, SVGs stored as normal files and opened in tabs or reference panes.
- **CSV-backed databases** — any `.csv` in the vault becomes a Notion-style database automatically. Two views (Table = spreadsheet-style inline-editable grid; Board = grouped by a select field) over the same underlying `.csv` data. Typed fields include note-links and select options auto-discovered from a folder/tag. Sidecar file `<Name>.csv.base.json` stores field-type metadata alongside the plain `<Name>.csv`. Records can render as their own Markdown pages. Vim-driven grid navigation in the TUI.
- **Vault/config portability** — `config.toml` (settings) and `vault.json` travel with the vault folder itself; desktop, web, and TUI clients all read the same files, no format lock-in.
- **File watcher** — detects external changes to vault files (e.g., edited by another app or synced in) and reloads.

## Sync & Backup
- **No built-in proprietary sync** — instead, "sync it with Git, iCloud, Dropbox — or don't"; since it's just files, any filesystem-level sync tool works.
- **ZenNotes Cloud (optional paid add-on)** — device-to-device sync, daily/manual backups with selective restore, and web publishing of notes.
- **Self-hosted web backend (Go)** — Docker image (`adibhanna/zennotes`) lets you run ZenNotes as a browser-accessible server pointed at a mounted vault folder; desktop/TUI can connect to it as a remote vault.
- **Trash as soft-delete** — trashed notes are recoverable (restore/permanently delete) rather than backup per se.
- **Security/auth for self-hosted mode** — token-based auth (`ZENNOTES_AUTH_TOKEN`/`_FILE`), bootstrap-token browser login upgrading to HttpOnly session cookies, configurable file/dir permission modes (default notes 0600, dirs 0700), non-root Docker container, TLS-proxy awareness, per-note/asset size caps.

## Tasks
- **Inline checkbox tasks** — standard `- [ ]` / `- [x]`, plus extended states: `- [/]` in-progress (half-filled), `- [-]` cancelled (muted), `- [>]` forwarded (links to a target note).
- **Task metadata** — inline or frontmatter `due:YYYY-MM-DD`, priority tokens `!high`/`!med`/`!low`, `@waiting` status grouping, and arbitrary `@status:<id>` for custom Kanban columns.
- **Whole-note tasks** — a note itself can be a task via `tags: [task]` frontmatter, with status (open/in-progress/done/cancelled), priority, due/scheduled dates; checking it off rewrites frontmatter rather than a checkbox glyph. Compatible with Obsidian's TaskNotes convention.
- **Tasks view with three layouts** — List, Calendar, and Kanban (folder-based board: one column per note folder), accessible via `:tasks`.
- **Subtask progress chips** — computed "2/5" style rollups, never written back into the Markdown.
- **Mobile task+calendar+Kanban views** — iOS/Android apps include dedicated task management with calendar and Kanban.
- **Archiving hides tasks** — archived notes' tasks drop out of active task views (recoverable via a setting).

## Calendar / Periodic Notes
- **Daily and weekly (and monthly, in TUI) notes** — one-keystroke open/auto-create of today's daily note and the current week's/month's periodic note, with customizable folder, date-format pattern, and localization (ISO date `2026-04-21`, ISO week `2026-W24`).
- **Task Calendar layout** — tasks with due dates render on a calendar grid, separate from the plain periodic-notes feature.
- No dedicated scheduling/agenda/reminder engine beyond periodic notes + task due dates (nothing like calcurse/remind-style recurring-event handling).

## Flashcards / Spaced Repetition
- **None found.** No flashcard or spaced-repetition feature is mentioned anywhere in the README, docs, or site — explicitly absent.

## UI/UX Approach
- **Keyboard-first, leader-key driven** — `Space` leader with which-key popups; one-keystroke access to Quick Notes, daily note, fuzzy finder (`⎵f`), new note (`⎵n`), quick capture (`⎵q`).
- **Layout flexibility** — split tabs side-by-side, pinned reference panes, detached floating windows, and a distraction-free Zen mode.
- **Deep customization** — separate font choices for UI/prose/code, editor font size/line-height, preview/editor width and content alignment, full keymap remapping with conflict detection, light/dark/auto theme families.
- **Cross-platform native apps** — macOS (Apple Silicon & Intel), Windows, Linux (multiple package formats), iOS 17+, Android 6+, plus self-hosted web via Docker.
- **`zen` CLI companion** — list/read/search/capture/edit/archive/trash notes, plus tasks/folders/MCP, from the command line; TUI (`zn`) commands maintain parity with this CLI so scripts work against either.
- **Raycast extension** (macOS) for quick capture/search from a launcher.

## Distinctive / Unique Features vs. Obsidian
- **First-party MCP server** — exposes vault operations (list/read/create/write/move/rename/duplicate/archive/trash/delete notes, full-text/title/tag search, backlink & unresolved-link discovery, task list/toggle with metadata, line-level insert/replace, folder/tag ops, comment add/reply/resolve) directly to Claude Code, Claude Desktop, and Codex — "your vault, open to assistants," with one-click setup.
- **Native (non-emulated) Vim** as the default editing mode across every runtime, including the TUI.
- **CSV-as-database** (Table + Board views, typed columns) built directly into the file format story — no plugin required, unlike Obsidian's Bases/community plugins.
- **Comment/annotation system** — inline comments on notes with reply/resolve workflow (more like a docs-review feature than typical note apps).
- **Official parallel TUI client (`zn`)** sharing one vault format with the GUI app — a genuinely dual-mode (GUI + terminal) product rather than TUI being an afterthought.
- **Multi-runtime single core** — same product logic shipped as Electron desktop, Go self-hosted web server, and Go TUI, all reading identical `config.toml`/`vault.json`/`.md`/`.csv` files.
