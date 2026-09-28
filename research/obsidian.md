# Obsidian Feature Inventory

Research pass for cwiki planning. Source: official Obsidian help docs (help.obsidian.md /
obsidian.md/help) plus obsidianstats.com / forum for community plugin notes. Feature
inventory only — no feasibility/cost ratings (done elsewhere).

## Markdown Editing (basic syntax)

- **Headings** — Six levels via 1-6 `#` symbols; standard document structuring.
- **Bold / italics / strikethrough** — `**bold**`, `*italic*`, `~~strike~~`; standard inline emphasis.
- **Highlight** — `==text==` marks text with a background color; useful for flagging important passages during review.
- **Paragraphs & line breaks** — Blank line = new paragraph; two trailing spaces or Shift+Enter = soft line break within a paragraph.
- **Blockquotes** — `>` prefix; for quoting or visually offsetting text.
- **Lists** — Unordered (`-`,`*`,`+`), ordered (`1.`), and nested lists via indentation; core outlining tool.
- **Task lists** — `- [ ]` / `- [x]` checkboxes; lightweight todo tracking inline in any note.
- **Horizontal rules** — `---`/`***`/`___`; visual section dividers.
- **Inline code & code blocks** — Backticks / triple-backtick fences with optional language tag for syntax highlighting.
- **Tables** — Pipe/hyphen syntax with column alignment; structured tabular data, supports inline formatting and links in cells.
- **Footnotes** — `[^label]` reference plus definition; supplementary notes without breaking reading flow.
- **Comments** — `%% text %%` hides text from rendered/exported output; useful for private annotations or draft notes.
- **Escaping** — Backslash-escape special characters to display them literally.
- **Editing shortcuts & folding** — Keyboard shortcuts for common edits, plus the ability to fold headings/lists to collapse sections — useful for navigating long documents.
- **Edit / Preview modes (Live Preview)** — Toggle or blend raw markdown editing with rendered preview in the same view; lets users see formatted output without leaving the editor.
- **HTML content** — Raw HTML is allowed inline in notes for cases markdown syntax doesn't cover.

## Advanced Formatting

- **Callouts** — Blockquote-based `[!type]` blocks (note, tip, warning, danger, bug, example, success, etc.) with icon/color per type, optional custom title, foldable (+/-), nestable, and support full markdown/links/embeds inside — a structured way to visually flag asides without a heavier block-editor model.
- **Math / LaTeX rendering** — Inline math with single `$...$`, block/display math with `$$...$$`, rendered via LaTeX-style notation (KaTeX under the hood); critical for technical/academic notes.
- **Diagrams (Mermaid)** — Fenced ` ```mermaid ` code blocks render flowcharts, sequence diagrams, timelines, etc., including clickable internal-link nodes.
- **Comments, footnotes** — (see basic syntax) also usable inside advanced/nested contexts like callouts and tables.

## Wikilinks & Backlinks

- **Wikilink syntax** — `[[Note name]]` (or `[[Folder/Note]]`) as the primary internal-link format; alternative standard markdown link syntax also works. This is the core mechanism that turns a folder of files into an interlinked wiki.
- **Auto-updating links on rename** — Renaming/moving a note automatically rewrites all internal links pointing to it (can be disabled); prevents link rot from file reorganization.
- **Link to headings/subheadings** — `[[Note#Heading]]`, chainable to subheadings; deep-links into a specific section of a note.
- **Block references** — Append `^identifier` to a line (auto-suggested or manually authored, e.g. `^quote-of-the-day`) then link with `[[Note#^identifier]]`; lets you link/embed a single paragraph or list item, not just a whole note.
- **Custom link display text / aliases** — Pipe syntax `[[Note|Display]]` for one-off custom text, or note-level **aliases** (metadata field) to give a note multiple stable names it can be referenced by.
- **Backlinks pane** — Shows every note that links to the currently open note, split into **Linked mentions** (formal `[[...]]` links) and **Unlinked mentions** (plain-text occurrences of the note's title not yet turned into links) — the unlinked-mentions surfacing is notable since it helps users retroactively discover/convert connections. Supports collapsing, full-context display, sorting, and text filtering. Viewable in sidebar, a dedicated pane, or inline at the bottom of the note.
- **Outgoing links pane** — The reverse view: lists all links the current note makes out to other notes/files.

## Graph View

- **Global graph view** — Interactive node graph of the entire vault: notes as nodes (circles), links as edges (lines). Supports hover-to-highlight, click-to-open, filtering by search term/tag/attachment type, color-grouping, display toggles (arrows, node size, link thickness), physics/force tuning, and a time-lapse animation of note creation order. The signature "visualize your whole wiki as a network" feature.
- **Local graph view** — Same rendering engine scoped to only the notes connected to the currently active note, with an adjustable depth slider to reveal further-out connections incrementally; useful for exploring a topic's neighborhood without the noise of the full vault.

## Tags

- **Inline tags** — `#tag` syntax anywhere in note body; lightweight, non-hierarchical categorization orthogonal to folder location.
- **Nested tags** — `#parent/child` hierarchical tags, viewable as a tree or flattened list.
- **Tag pane** — Lists every tag in the vault with per-tag note counts; sortable alphabetically or by frequency, with expand/collapse for nested tags.
- **Click-to-search / toggle tag filters** — Clicking a tag runs a search for it; Ctrl/Cmd-click toggles it into a compound search query.

## Folders

- **Vault = folder on disk** — A vault is just an ordinary filesystem folder (with subfolders); no proprietary container format, so any file manager or sync tool works alongside it.
- **File explorer pane** — Standard tree view of the vault's folder/file hierarchy for browsing and organizing notes.
- **Multiple vaults** — Separate folders can be opened as independent, isolated vaults (e.g. work vs. personal); nesting one vault inside another is explicitly discouraged since links may not resolve correctly.
- **Attachments handling** — Configurable default location for dropped/pasted attachments (images, PDFs, etc.), independent of note folder structure.

## Search

- **Full-text search pane** — Vault-wide search accessible via sidebar or hotkey.
- **Boolean/operator query syntax** — AND (implicit, `term1 term2`), OR, negation (`-term`), grouping with parentheses, exact-phrase quoting.
- **Field-scoped operators** — `file:`/`path:` (filename/location), `content:` (body text), `tag:` (indexed tag search, faster than full text), `block:`/`section:` (proximity within a block or heading section), `task:`/`task-todo:`/`task-done:` (checkbox state), and bracket syntax for property/metadata filters (e.g. `[property:value]`, numeric comparisons like `[duration:<5]`).
- **Regex search** — Pattern matching via `/regex/` delimiters.
- **Sort & export results** — Sort by filename or creation/modification date; copy results out; results can also be embedded live in a note via a search/query code block.

## Plugins & Themes

- **Core plugins** — First-party, toggleable built-in features (e.g. Daily notes, Templates, Backlinks, Graph view, Canvas, Tags, Search, Properties, File explorer, Command palette) that ship with the app but can be disabled individually — a modular-core design rather than one monolith.
- **Community plugins** — Third-party plugin ecosystem (browsable in-app or at community.obsidian.md) that extends functionality arbitrarily (new file formats, third-party service integrations, UI additions, etc.). Requires explicitly leaving "Restricted Mode" to enable. Plugins don't auto-update (security-conscious manual review step), and the docs explicitly flag that plugins run arbitrary third-party code and can be harmful — a real security consideration for any similarly extensible design.
- **Notable community plugins (brief)**:
  - **Dataview** — Query-language plugin that treats notes/frontmatter as a database, rendering dynamic tables/lists from queries embedded in notes.
  - **Templater** — More powerful templating with scripting/logic beyond the core Templates plugin.
  - **Periodic Notes** — Extends daily notes to weekly/monthly/quarterly/yearly notes.
  - **Calendar** — Popular sidebar month calendar that jumps to/creates daily notes; pairs with Periodic Notes.
  - **Tasks** — Adds due dates, recurring tasks, done dates, and a global query view for checkbox tasks across the vault; the closest analog to a task manager built on plain markdown checkboxes.
  - **Full Calendar** — Embeds a FullCalendar-style calendar UI for scheduling events alongside notes.
  - **Spaced repetition plugins (multiple)** — e.g. "obsidian-spaced-repetition" implements Anki-like SM-2 review scheduling directly on note content, with flashcard syntax (`Q::A`, `Q:::A` reversed, cloze via highlight/bold, multi-line cards) plus stats; other plugins ("Flashcards", "Yanki") sync/export Anki-compatible decks or map vault folders to Anki decks. Directly relevant prior art for cwiki's Anki-inspired spaced-repetition angle.
- **Themes** — Community-built visual skins (Settings → Appearance → Themes) that restyle the whole app; installed/managed in-app or via community.obsidian.md, don't auto-update.
- **CSS snippets** — User-supplied CSS files toggled on/off for finer-grained visual tweaks without a full theme, and also used to define custom callout styling (icon/color variables).

## Canvas

- **Infinite spatial canvas** — A core plugin giving a pannable/zoomable 2D board for laying out and visually connecting notes, attachments, web pages, and free-floating text cards. Saved as an open, documented `.canvas` JSON format (not proprietary), so it's plain-text-adjacent and diffable/portable — notable design choice worth mirroring.
- **Card types on canvas** — Text cards (markdown without a backing file), embedded vault notes, media (image/audio/PDF), embedded web pages via URL, and whole folders added as a group of cards.
- **Connections/edges** — Draw labeled, colored lines between cards to express relationships, distinct from and complementary to wikilinks.
- **Grouping & color-coding** — Cards can be grouped and color-tagged for visual organization (e.g. project boards, brainstorms, mind maps).

## Daily Notes & Templates

- **Daily notes (core plugin)** — One command/hotkey opens (or creates) "today's" note by date-based filename (default `YYYY-MM-DD`); configurable folder location including date-based subfolder patterns (`YYYY/MMMM/...`). The standard journal/log entry point.
- **Daily note templating** — A designated template file is applied automatically to new daily notes, so recurring structure (habit trackers, task sections) appears every day without retyping.
- **Date-property auto-linking** — A "date" property/frontmatter field on any note is automatically rendered as a clickable link to that day's daily note.
- **Templates (core plugin)** — Reusable text snippets stored as template files in a designated folder, inserted into the active note on demand via command palette/ribbon.
- **Template variables** — `{{title}}`, `{{date}}`, `{{time}}` placeholders auto-fill from context, with Moment.js-style custom formatting (e.g. `{{date:YYYY-MM-DD}}`); also usable standalone to insert just a date/time stamp.
- **Periodic notes (community plugin)** — Generalizes daily notes to weekly/monthly/quarterly/yearly cadences, each with its own template/folder.

## Sync

- **Obsidian Sync (official paid add-on)** — End-to-end encrypted, cross-device sync of the vault, sold as a subscription with tiered storage limits; explicitly framed as "private" sync (encryption keys stay client-side).
- **Version history** — Sync retains prior versions of each note so you can view/restore an earlier state — a lightweight built-in undo-across-time safety net, distinct from full git-style history.
- **Third-party sync alternative** — Because a vault is just plain files in a folder, users commonly sync via Git, Syncthing, iCloud/Dropbox/etc. instead of the official service — the plain-file storage model is precisely what makes this possible, and is directly relevant to a C/TUI tool's storage design.

## Math / LaTeX

- **KaTeX-based rendering** — Inline `$...$` and block `$$...$$` math syntax renders standard LaTeX math notation without needing an external LaTeX toolchain; important for anyone doing technical notes (echoes the Neovim-LaTeX-snippet inspiration named in the brief).

## Embeds

- **File embeds** — Prefixing an internal link with `!` (`![[File]]`) embeds that file's live content inline (image, PDF, audio, video, or another note) rather than just linking to it; stays in sync automatically if the source changes.
- **Partial embeds** — Can embed just a specific heading section or a single block (via block reference) from another note, not only the whole file.
- **Sizing controls** — Image embeds support `|WxH` dimension suffix; PDF embeds support a `#height=N` viewer-height parameter.
- **Drag-and-drop embedding** (desktop) — Dragging a supported file directly into a note auto-inserts it as an embed.
- **Web page embeds** — URLs can be embedded to show an inline iframe-style preview of an external page.
- **Query/search embeds** — A search query can be embedded as a live, auto-updating block inside a note (results re-render as the vault changes).

## Metadata / Frontmatter (Properties)

- **YAML frontmatter ("Properties")** — Structured key/value metadata block at the top of a note, exposed through a dedicated Properties UI rather than raw YAML editing alone.
- **Typed properties** — Values have recognized types (text, number, checkbox/boolean, date, date & time, list, tag-like), which the UI renders with a fitting input widget instead of freeform text.
- **File properties view** — Sidebar panel showing/editing the active note's own metadata fields.
- **All properties view** — Vault-wide panel listing every property name in use, its type, and usage count; supports sorting by name/frequency, searching, and bulk-renaming a property across every note that uses it via a right-click action.
- **Property-based search** — Frontmatter fields are queryable through the search syntax (bracket filters, including numeric comparisons), effectively making notes a lightweight structured database.

## Panes / Workspace Layout

- **Workspace as unified container** — The whole UI (ribbon, sidebars, tab groups, status bar) is one configurable "workspace," savable/restorable as a layout.
- **Split panes / tab groups** — The central editing area can be split vertically or horizontally into multiple tab groups, each holding its own tabs, for viewing/editing several notes side by side.
- **Collapsible left/right sidebars** — Left sidebar typically holds navigation (file explorer, search, etc.); right holds contextual panels (backlinks, outline, properties); both collapsible to reclaim screen space.
- **Ribbon** — Customizable vertical icon bar for quick access to common actions/panels.
- **Mobile-adapted layout** — On mobile, sidebars open via edge-swipe gestures, a bottom navigation bar replaces the ribbon (accessed through an "Open menu" action), and an editor toolbar sits above the on-screen keyboard.

## Command Palette

- **Fuzzy-matched command launcher** — `Ctrl/Cmd+P` opens a searchable list of every available command (core + plugin-contributed), matched by fuzzy substring rather than exact name.
- **Pinned & recent commands** — Frequently-used commands can be pinned to the top; recently-used commands also surface near the top automatically.

## Hotkeys

- **Fully rebindable keyboard shortcuts** — Every command (core or plugin) can be bound to a custom key combination via Settings → Hotkeys, with support for multiple bindings per command.
- **Layout-aware key capture** — Bindings are captured by physical key press, so they work correctly on non-US keyboard layouts even though the settings UI displays them in US-layout notation.
- **Distinct from OS-level shortcuts** — Hotkeys only cover in-app commands; OS-native shortcuts (e.g. copy/paste) aren't overridden or managed by this system.

## Mobile Support

- **Native iOS/iPadOS and Android apps** — Same core note-taking engine as desktop, adapted for touch.
- **Mobile-only UI affordances** — Customizable bottom editing toolbar, a "quick action" pull-down gesture (defaults to opening the command palette but is user-configurable), and a bottom navigation bar with a tab counter and back/forward controls.
- **Same vault, same plain files** — Mobile reads/writes the identical markdown-file vault (via Sync or a file-sync app), so it isn't a separate/limited data model, just a different shell.

## File Format / Storage Model

- **Plain markdown files on disk** — Every note is an ordinary `.md` plain-text file; fully editable in any external text editor, and other tools/scripts can read the vault directly. This "your data is just files" guarantee is arguably Obsidian's most copied design principle and the most directly relevant one for a C-based tool.
- **Hidden `.obsidian` config folder** — Per-vault settings (hotkeys, enabled themes/plugins, workspace layout) live in a `.obsidian` folder at the vault root, kept separate from note content itself.
- **External change detection** — Obsidian watches the vault folder and reloads notes automatically if they're edited outside the app (another editor, sync client, script), rather than only trusting its own in-app state.
- **Global app settings separate from vault** — Cross-vault preferences live in the OS-standard app-data location (Library/Application Support, %APPDATA%, or .config), distinct from any given vault's own `.obsidian` folder.

## Tasks / Calendar (plugin ecosystem, not core)

- **Tasks plugin** — Turns plain `- [ ]` checkboxes into a queryable task system: due/scheduled/start dates, recurrence rules, done-dates, and a dedicated query block that aggregates matching tasks from across the whole vault into one view.
- **Calendar plugin** — Sidebar month-view calendar for quick navigation to (and creation of) daily/periodic notes by date.
- **Full Calendar plugin** — Embeds a full scheduling UI (day/week/month views, timed events) backed by notes/frontmatter, closer to a traditional calendar app than the daily-notes model alone.

## Spaced Repetition / Flashcards (plugin ecosystem, not core)

- **obsidian-spaced-repetition** — The dominant community plugin: implements an SM-2-family scheduling algorithm directly against note content. Supports single-line (`Q::A`), single-line-reversed (`Q:::A`), multi-line, and cloze-deletion (via highlight or bold markup) card formats, all authored as ordinary markdown inside normal notes rather than a separate deck format. Also supports whole-note review (not just discrete cards), rich content in cards (images/audio/video/LaTeX/code), and review statistics.
- **Anki-bridge plugins (Flashcards, Yanki, etc.)** — Alternative plugins that instead sync/export vault content to real Anki decks (mapping folders to decks, supporting Basic/Reversed/Cloze/Type-in-answer note types), for users who want Anki's own review engine but author cards in their notes.
