
# Interaction philosophy of the Spirit/nvim config

This config is small (four Lua files, one plugin library) but it is
opinionated about *how you discover and use commands*, not just which
commands exist. The philosophy that comes through is: keep the keyboard
vocabulary small and mnemonic, never make the user memorize a command they
haven't used in a week, and never require a mouse. That's the core of what
cwiki's own screens should borrow.

**Clue popups instead of memorized cheat sheets.** Almost every multi-key
entry point (`<Leader>`, `\`, `[`/`]`, `` ` ``/`'`, `"`, `<C-w>`, `g`, `s`,
`z`) is registered as a trigger for `mini.clue`, a which-key-style popup. The
moment you press a leading key and hesitate, a window appears listing the
plausible next keys with human-readable descriptions ("+Buffer", "+Find",
"+Git", "+Language", "+Session", "+Terminal", "+Toggles"). Leaf commands get
their description from the keymap itself; whole *groups* of commands
(everything under `<Leader>g`, for instance) get one label so the popup reads
as a table of contents, not a wall of bindings. This means the keybinding
surface can grow without becoming unlearnable — discovery is inline and
progressive instead of front-loaded into documentation. For cwiki, this
argues for a similar just-in-time key-hint layer: a status line or popup that
shows "what can I press next" scoped to the current mode/prefix, rather than
expecting the user to hold a keymap in their head or open a help file.

**Pickers as the single search/navigate surface.** Files, buffers, live
grep, grep-for-word-under-cursor, help tags, resumed searches, git commits,
and git hunks are all exposed through the same fuzzy-picker widget
(`mini.pick`), each just a different "source" fed into one UI pattern: type
to filter, arrow/`<C-n>`/`<C-p>` to move, `<CR>` to jump. The user learns one
interaction shape and reuses it for a dozen different questions ("find a
file," "find text," "find a commit," "find where I just was"). Everything
lives under one `<Leader>f`/`<Leader>gf` namespace so it's discoverable
through the same clue popup. cwiki should adopt this pattern directly: one
fuzzy-filter list-and-jump component, reused for "find a page," "find a
heading," "search page contents," "recent pages," rather than bespoke UI per
search type.

**No mouse, ever.** `mouse = ''` is set unconditionally. Every interaction —
opening a file, resizing a split, jumping to a diagnostic, browsing history —
has a keyboard path by construction, because there is no fallback. Window
focus is one keystroke (`<C-h/j/k/l>`), not a click; even the file explorer
and resize operations are keyboard submodes rather than drag targets. cwiki's
screens should be designed the same way: assume keyboard-only from the start
rather than retrofitting shortcuts onto a mouse-first UI, since retrofits
tend to leave gaps (a context menu with no keyboard equivalent, a
drag-to-resize with no keyed alternative).

**Windows and buffers stay lightweight and visible.** Splits open below/right
predictably, the view doesn't jump around when splitting (`splitkeep =
'screen'`), and a persistent statusline/tabline keeps buffer identity and git
state visible at all times instead of requiring a command to check "what
files are open" or "what changed." Buffer deletion is separated from window
deletion (`mini.bufremove`) so closing a document never disturbs the split
layout the user built. For cwiki, this suggests: make pane/split management
predictable and non-destructive by default, and keep lightweight persistent
status (current file, unsaved state, position) always on screen rather than
queried on demand.

Overall: small vocabulary, progressive disclosure via popups, one reusable
picker pattern for "find anything," and a hard commitment to keyboard-only
interaction. cwiki's editor and wiki-browsing screens should follow the same
three moves — hint what's pressable, unify search/navigate into one picker
idiom, and never assume a pointing device.
