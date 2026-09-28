# Neovim config feature inventory (source: Spirit/nvim)

Extracted from `research/nvim-config/` (init.lua + plugin/1_options.lua,
2_keymaps.lua, 3_mini.lua, 4_plugins.lua). Config is built almost entirely on
**mini.nvim** (a bundle of small single-purpose modules) plus
nvim-treesitter for syntax/structure. LSP, completion engine (mini.completion,
mini.snippets), formatter (conform.nvim) and linters are intentionally
excluded per scope.

## Editing

- **Surround actions** — add/delete/replace/find surrounding pairs (quotes,
  brackets, tags) around the cursor or a text object. Provided by
  `mini.surround`.
- **Extended text objects (a/i)** — adds custom `a`/`i` text objects beyond
  vim's built-ins: `aB`/`iB` for whole buffer, `aF`/`iF` for a function
  definition (via tree-sitter query `@function.outer`/`@function.inner`).
  Search method is set to `cover` (only matches the object under the cursor,
  not "search forward" like vanilla vim does by default). Provided by
  `mini.ai` (+ `mini.extra` for the buffer spec, `nvim-treesitter-textobjects`
  for structural queries).
- **Text alignment** — align text around a character/pattern (e.g. align a
  block of `=` signs or table columns). Provided by `mini.align`.
- **Split/join arguments** — toggle a comma-separated argument list (function
  call, array, etc.) between single-line and multi-line. Provided by
  `mini.splitjoin`.
- **Move selected text** — move a visual selection or line up/down/left/right
  with Alt+arrow-style keys, auto-reindenting. Provided by `mini.move`.
- **Autopairs** — auto-insert matching closing bracket/quote; also enabled in
  command-line mode. Backspace over an auto-inserted pair deletes both sides
  via a multi-step keymap (`mini.keymap.map_multistep('i', '<BS>',
  {'minipairs_bs'})`). Provided by `mini.pairs` + `mini.keymap`.
- **Comment continuation disabled** — a `FileType` autocommand strips `c` and
  `o` from `formatoptions` on every buffer so Neovim never auto-inserts a
  comment leader after `o`/`O` or hard-wraps comments while typing. Pure
  option/autocmd behavior (no plugin).
- **Yank highlight** — briefly highlights the region just yanked. Built-in
  `vim.hl.on_yank()` wired to `TextYankPost`.
- **System clipboard shortcuts** — `gy`/`gp` (normal + visual) map to
  `"+y`/`"+p` for explicit copy/paste to the OS clipboard, kept separate from
  the default register. Custom keymap.
- **Trim trailing whitespace on demand** — `<Leader>ot` trims trailing
  spaces; trailing whitespace is also highlighted automatically. Provided by
  `mini.trailspace`.

## Navigation

- **Fuzzy picker (files/buffers/grep/help/resume)** — `<Leader>ff` files
  (including hidden, via a custom `ripgrep --hidden` registry entry),
  `<Leader>fb` buffers, `<Leader>fg` live grep, `<Leader>fG` grep word under
  cursor, `<Leader>fh` help tags, `<Leader>fr` resume last picker session.
  Provided by `mini.pick` (+ `mini.extra` for extra picker sources like git
  hunks/commits).
- **File explorer / directory browser** — tree-style file browser with
  directory/file preview pane; `<Leader>ed` opens at cwd, `<Leader>ef` opens
  at the current file's directory. Custom bookmarks are registered for the
  config dir, plugin dir, and cwd (press a mark letter to jump there inside
  the explorer). Provided by `mini.files`.
- **Bracketed navigation (`[`/`]`)** — move through vim-style "lists"
  (buffers, comments, conflicts, files, indent, jumps, quickfix, treesitter
  nodes, windows, etc.) using `[x`/`]x` mnemonics. Provided by
  `mini.bracketed`.
- **2D jump / label-based jumping** — jump to any visible position by
  labeling candidate spots (like easymotion/hop), for jumping across the
  visible window rather than stepping motion-by-motion. Provided by
  `mini.jump2d`.
- **Enhanced character jump (f/t)** — improves `f`/`F`/`t`/`T` character
  search: highlights the target and allows repeating the last jump without
  re-specifying the character. Provided by `mini.jump`.
- **Window focus navigation** — `<C-h/j/k/l>` in normal mode move focus
  between splits directly (wraps `<C-w>h/j/k/l`), skipping the `<C-w>`
  prefix. Custom keymap.
- **Window management submode** — pressing `<C-w>` shows a clue popup; a
  resize submode lets you keep pressing `+`/`-`/etc. after one `<C-w>s`
  without re-issuing the prefix, until `<Esc>` or a non-submode key. Provided
  by `mini.clue` (`gen_clues.windows({ submode_resize = true })`).
- **Split placement** — new horizontal splits open below, new vertical
  splits open to the right, and the view stays stable (`screen`) when
  splitting instead of jumping. Options `splitbelow`, `splitright`,
  `splitkeep=screen`.
- **Folding** — indent-based folding is enabled by default (`foldmethod =
  'indent'`) with folds mostly pre-opened (`foldlevel = 10`, capped nesting
  `foldnestmax = 10`) and a custom fold-line character (`fillchars`
  `fold:╌`, blank `foldtext`).
- **Marks and registers clues** — pressing `'`/`` ` `` (marks) or `"`
  (registers) pops up a live list of available marks/registers and their
  contents/positions before you complete the command. Provided by
  `mini.clue` (`gen_clues.marks()`, `gen_clues.registers()`).
- **Quickfix list toggle** — `<Leader>eq` opens the quickfix window if
  closed, closes it if open. Custom keymap.
- **Cursor-word highlight** — automatically highlights all other occurrences
  of the word currently under the cursor. Provided by `mini.cursorword`.
- **Indent-scope visualization** — draws a vertical guide/animation showing
  the current indent scope (like indent-blankline's active-scope feature).
  Provided by `mini.indentscope`.
- **Restore cursor position on reopen** — reopening a file returns the
  cursor to where it was last left. Provided by `mini.misc`
  (`setup_restore_cursor`).
- **Auto root detection** — cwd automatically changes to the nearest parent
  directory containing `.git` or `Makefile` when opening a file, useful when
  working across multiple projects in one session. Provided by `mini.misc`
  (`setup_auto_root`).

## Search

- **Incremental search** — matches highlight live as you type a search
  pattern. Option `incsearch = true`.
- **Case-insensitive search with smart override** — searches ignore case by
  default (`ignorecase = true`) but become case-sensitive automatically if
  the pattern contains an uppercase letter (`smartcase = true`).
- **Infer case on insert-completion** — case of a completed word is adjusted
  to match typed case. Option `infercase = true`.
- **Toggle search highlight** — `\h` toggles `hlsearch` on/off with a
  notification of the new state. Custom keymap (generic option-toggle
  helper).
- **Dash-as-word-character** — `-` is added to `iskeyword`, so word motions
  and word text objects (`w`, `iw`, etc.) treat kebab-case identifiers
  (`foo-bar`) as one word. Option `iskeyword`.

## Buffers, windows, sessions

- **Buffer deletion/wipeout** — `<Leader>bd`/`<Leader>bD` delete (force),
  `<Leader>bw`/`<Leader>bW` wipeout (force), without closing the window
  layout. Provided by `mini.bufremove`.
- **Alternate buffer** — `<Leader>ba` jumps to the alternate buffer (`:b#`).
  Custom keymap.
- **Buffer reuse across tabs** — switching to an already-open buffer focuses
  its existing window/tab instead of opening a duplicate. Option `switchbuf
  = 'usetab'`.
- **Session management** — save/restore/delete named sessions and restart
  into a session; `<Leader>sw`/`sn`/`sr`/`sd`/`sR`. Provided by
  `mini.sessions`.
- **Statusline** — a minimal built-in statusline (mode, git branch/diff
  summary, diagnostics count, filename, position) replacing vim's default.
  Provided by `mini.statusline`.
- **Tabline** — a buffer-list-style tabline (shows open buffers across the
  top rather than vim tabs). Provided by `mini.tabline`.
- **Start screen** — a minimal dashboard shown when Neovim opens with no
  file argument. Provided by `mini.starter`.
- **Terminal splits** — `<Leader>tt` opens a vertical terminal,
  `<Leader>tT` a horizontal one. Custom keymap around `:term`.
- **Notification history** — `<Leader>en` shows a history of past
  notifications. Provided by `mini.notify`.

## Git

- **Git command wrappers** — `<Leader>gd`/`gD` diff (all/buffer),
  `<Leader>ga`/`gA` staged diff (all/buffer), `<Leader>gc`/`gC` commit/amend,
  `<Leader>gl`/`gL` custom-formatted log (all/buffer, one line per commit with
  hash/date/subject). These shell out to `:Git` (external git command
  integration, not a plugin listed in this config's own plugin file — assumed
  provided by an external `Git` command/plugin already on the system).
- **Inline diff overlay** — `<Leader>go` toggles an overlay showing
  added/changed/removed lines directly in the buffer gutter/text. Provided by
  `mini.diff`.
- **Git blame/show at cursor** — `<Leader>gs` (normal and visual) shows git
  info (blame-style) for the line/selection under the cursor. Provided by
  `mini.git`.
- **Pickers over git history** — `<Leader>gfc`/`gfC` pick commits
  (all/buffer), `<Leader>gfa`/`gfA`/`gfm`/`gfM` pick hunks (staged/modified,
  all/buffer). Provided by `mini.pick` + `mini.extra` git sources.

## Appearance / feedback

- **Icons** — filetype/extension-aware icons used across pickers, file
  explorer, statusline, etc., with custom rules to prefer filetype icons over
  extension icons for certain extensions. Provided by `mini.icons`.
- **Highlight FIXME/HACK/TODO/NOTE** — these words are highlighted anywhere
  in a buffer (not just comments) with distinct colors. Provided by
  `mini.hipatterns`.
- **Hex color preview** — a literal hex color string like `#aabbcc` is shown
  with that color as its own highlight background. Provided by
  `mini.hipatterns` (`gen_highlighter.hex_color`).
- **Cursorline (smart)** — current line is highlighted only for the
  screen-line and number column (not full width) via `cursorlineopt =
  'screenline,number'`, toggle via `\c`.
- **Relative + toggleable line numbers** — `relativenumber = true` by
  default; `\n`/`\r` toggle absolute/relative numbers on the fly. Custom
  toggles + options.
- **Sign column always shown** — gutter for diagnostics/git signs is always
  reserved (`signcolumn = 'yes'`) so text doesn't shift when signs appear.
- **Popup menu styling** — completion/other popup menus get a single-line
  border and constrained height/width (`pumborder`, `pumheight = 10`,
  `pummaxwidth = 100`).
- **Window border style** — all floating windows use a single-line border
  (`winborder = 'single'`).
- **Terminal background sync** — Neovim's background color is synced to the
  terminal emulator's background so there's no visible color mismatch/padding
  around the Neovim window. Provided by `mini.misc`
  (`setup_termbg_sync`).
- **Quiet UI / reduced messages** — `shortmess = 'CFOWaco'` suppresses
  various intro/completion/file messages; `showmode = false` hides the
  `-- INSERT --` style mode indicator (redundant with the statusline);
  `ruler = false` disables the built-in ruler (also redundant with
  statusline).

## Option-driven behavior (not tied to a specific plugin)

- **Persistent undo** — `undofile = true` writes an undo history file per
  edited file so undo survives closing and reopening Neovim. No custom
  `undodir` is set, so it uses Neovim's default undo-file directory
  (`~/.local/state/nvim/undo` on Linux/macOS).
- **No swapfile** — `swapfile = false` disables `.swp` crash-recovery files.
- **Indentation: 3-space, spaces-only** — `shiftwidth = 3`, `tabstop = 3`,
  `softtabstop = 3`, `expandtab = true` together mean every indent level,
  literal Tab press, and stored tab character are all treated as 3 spaces
  (config's chosen indent width). `autoindent` and `smartindent` keep new
  lines aligned with the previous line's/structural indent.
- **No line wrap, but breakindent configured** — `wrap = false` disables
  visual line-wrapping entirely; `breakindent = true` and `linebreak = true`
  are set but only take visible effect if `wrap` is later toggled on (e.g.
  via the `\w` toggle), in which case wrapped lines stay indented to match
  the original line and break at word boundaries rather than mid-word.
- **Mouse disabled** — `mouse = ''` turns off all mouse interaction; the
  config is keyboard-only by design.
- **Virtual block editing** — `virtualedit = 'block'` allows visual-block
  selection to extend past the end of shorter lines (e.g. to build a
  rectangular selection over ragged-length lines).
- **Spelling: camelCase-aware** — `spelloptions = 'camel'` makes spellcheck
  treat camelCase boundaries as word boundaries; `\s` toggles spellcheck on.
- **Custom list-continuation pattern** — `formatlistpat` recognizes numbered
  list items (`1. `, `2) `, `- `, `* `, `+ `) so the `gw`/`gq` formatting
  commands correctly preserve/reflow list indentation.
- **Limited session/marks-file (ShaDa) size** — `shada` caps how much
  history (registers, marks, search/command history) is remembered between
  sessions, mainly to keep startup fast.

## Interaction chrome

- **Leader-key clue popup** — pressing `<Leader>` (or other configured
  trigger keys: `\`, `[`, `]`, `` ` ``, `'`, `"`, `<C-w>`, `g`, `s`, `z`, and
  `<C-x>`/`<C-r>` in insert/cmdline mode) opens a popup listing the available
  next keys and their descriptions, functioning as an in-editor cheat sheet.
  Provided by `mini.clue`, configured with explicit leader-group labels
  (`+Buffer`, `+Find`, `+Git`, `+Git Find`, `+Language`, `+Session`,
  `+Terminal`, `+Toggles`, etc.) plus generated clues for marks, registers,
  windows, `g`-commands, `z`-commands, bracket motions, and built-in
  completion.
- **Option toggles under `\`** — a small namespace of "flip this setting"
  keys (`\n` numbers, `\r` relative numbers, `\w` wrap, `\s` spell, `\c`
  cursorline, `\h` search highlight), each printing the new value via
  `vim.notify`. Custom keymap helper.
- **Command-line UX tweaks** — adjustments to the built-in command-line
  behavior/appearance. Provided by `mini.cmdline`.
- **Custom input prompts** — `vim.ui.input`-style prompts (e.g. "Session
  name:") are rendered through a nicer UI rather than the bare command-line
  prompt. Provided by `mini.input`.
