# Core Vim editing feature inventory (source: vim)

Baseline Vim/Neovim behavior that the Spirit/nvim config relies on but never
configures or installs a plugin for — it's just how Vim works out of the box.
Listed here so cwiki can decide, independently of that config, which subset
of *stock Vim* (not plugin behavior) is worth reimplementing. Every bullet is
tagged `[vim]`.

## Modes

- **Normal mode** `[vim]` — the default mode; keys are commands (motions,
  operators, etc.) rather than inserted text. Everything below assumes this
  is the resting state.
- **Insert mode** `[vim]` — entered via `i`/`a`/`I`/`A`/`o`/`O`/`c`-family
  operators etc.; keys are inserted as literal text until `<Esc>` (or
  `<C-c>`) returns to Normal mode.
- **Replace mode** `[vim]` — entered via `R`; typed characters overwrite
  existing text instead of inserting, respecting line length (no shift of
  trailing text) until `<Esc>`.
- **Command-line mode** `[vim]` — entered via `:` (ex commands), `/` or `?`
  (search), used to run commands or enter search patterns with their own
  history and completion.

## Motions

- **Character motions** `[vim]` — `h`/`j`/`k`/`l` move left/down/up/right by
  one character/line; `0`/`^`/`$` jump to start-of-line, first non-blank, and
  end-of-line.
- **Word motions** `[vim]` — `w`/`W` move to next word start (`w` stops at
  punctuation as its own word, `W` treats punctuation as part of the word);
  `b`/`B` move to previous word start; `e`/`E` move to end of current/next
  word.
- **Paragraph and sentence motions** `[vim]` — `{`/`}` jump to previous/next
  blank-line-delimited paragraph boundary; `(`/`)` jump to previous/next
  sentence boundary (sentence = ends in `.`/`!`/`?` followed by whitespace or
  EOL).
- **Line/screen motions** `[vim]` — `gg`/`G` jump to first/last line of
  buffer (or line N with a count); `H`/`M`/`L` jump to top/middle/bottom
  visible line of the window; `-`/`+`/`_` move to first non-blank of
  previous/next/current+N line.
- **Character search motions** `[vim]` — `f{char}`/`F{char}` jump forward/
  backward on the current line to the next occurrence of `{char}`;
  `t{char}`/`T{char}` jump to just before/after it; `;`/`,` repeat the last
  `f`/`F`/`t`/`T` forward/backward.
- **Search motions** `[vim]` — `/{pattern}` and `?{pattern}` search
  forward/backward for a regex pattern and move the cursor to the match;
  `n`/`N` repeat the last search in the same/opposite direction; `*`/`#`
  search forward/backward for the word under the cursor.
- **Percent / matching bracket motion** `[vim]` — `%` jumps to the matching
  `()`/`[]`/`{}` pair (or a matching language keyword pair with matchit).
- **Text-object motions** `[vim]` — motions like `w`, `(`, `{`, `` ` `` etc.
  double as the "target" half of an operator+motion command even though they
  are not themselves text objects (see Text Objects below for the dedicated
  `i`/`a` forms).
- **Jump motions (`` ` `` / `'`)** `[vim]` — `` `{mark} `` jumps to the exact
  cursor position of a mark; `'{mark}` jumps to the first non-blank of the
  mark's line. `` `` `` / `''` jump back to the position before the last jump.

## Operators and operator+motion composition

- **Delete (`d`)** `[vim]` — deletes text and yanks it into the unnamed
  register; combines with any motion or text object (`dw`, `d$`, `dip`) or
  doubled (`dd`) to act on the whole line.
- **Change (`c`)** `[vim]` — like delete but drops into Insert mode
  afterward; `cw`, `c$`, `ciw`, `cc` (whole line) are the common forms.
- **Yank (`y`)** `[vim]` — copies text into a register without deleting it;
  same motion/text-object composition as `d` (`yw`, `y$`, `yip`, `yy` for
  whole line).
- **Put/paste (`p`/`P`)** `[vim]` — inserts the contents of a register after
  (`p`) or before (`P`) the cursor/line, adjusting for whether the yanked
  text was charwise, linewise, or blockwise.
- **Indent shift (`<`/`>`)** `[vim]` — shift a motion's lines left/right by
  one `shiftwidth`; `<<`/`>>` for the current line, repeatable with `.`.
- **Case-change operators (`gu`/`gU`/`g~`)** `[vim]` — lowercase/uppercase/
  toggle-case a motion's text; `~` alone toggles case of the single character
  under the cursor.
- **Format (`gq`/`gw`)** `[vim]` — reflow a motion's lines to `textwidth`
  (`gq` moves the cursor, `gw` keeps cursor position); respects
  `formatoptions`/`formatlistpat` for comment leaders and list continuation.
- **Filter through external command (`!`)** `[vim]` — pipes a motion's lines
  through an external shell command and replaces them with its output.
- **Operator + motion composition itself** `[vim]` — the general Vim grammar
  `{operator}{count}{motion-or-textobject}` (e.g. `d3w`, `2dj`, `y i (`)
  is the core mechanism that makes operators combine with every motion and
  text object rather than needing a dedicated command per combination; this
  compositionality (not any single operator) is arguably Vim's single
  biggest editing idea.

## Text objects

- **Word / WORD (`iw`/`aw`, `iW`/`aW`)** `[vim]` — inner/around a word
  (punctuation-delimited) or a WORD (whitespace-delimited); "around" includes
  trailing whitespace, "inner" doesn't.
- **Sentence (`is`/`as`)** `[vim]` — inner/around the sentence under the
  cursor, using the same sentence boundary rules as `(`/`)`.
- **Paragraph (`ip`/`ap`)** `[vim]` — inner/around the blank-line-delimited
  paragraph under the cursor.
- **Quoted string (`i"`/`a"`, `i'`/`a'`, `` i` ``/`` a` ``)** `[vim]` —
  inner/around the nearest quoted string on the current line for each quote
  style.
- **Bracket pairs (`i(`/`a(`, `i[`/`a[`, `i{`/`a{`, `i<`/`a<`)** `[vim]` —
  inner/around the nearest enclosing bracket pair, with `)`/`]`/`}`/`>` as
  equivalent aliases.
- **Tag block (`it`/`at`)** `[vim]` — inner/around the nearest enclosing
  HTML/XML tag pair, e.g. `dit` deletes the content between `<p>` and
  `</p>`.
- **Block/general composition** `[vim]` — every text object works as the
  target of any operator (`d`, `c`, `y`, `<`/`>`, `gu`, etc.) and in Visual
  mode as a selection (`vip`, `viw`), which is what makes text objects
  powerful rather than just a list of shortcuts.

## Counts

- **Numeric prefix on motions** `[vim]` — prefixing a motion with a number
  repeats it that many times (`5j` moves down 5 lines, `3w` moves 3 words
  forward).
- **Numeric prefix on operators/commands** `[vim]` — a count before an
  operator, or between an operator and its motion, multiplies the effect
  (`d3w` and `3dw` both delete 3 words; `2dd` deletes 2 lines).
- **Counts on inserts and other repeatable actions** `[vim]` — `3iabc<Esc>`
  inserts "abc" three times; `5.` (see dot-repeat) can override the repeated
  command's own count.

## Registers

- **Unnamed register (`"`)** `[vim]` — the default register that ordinary
  `y`/`d`/`c`/`p` read/write when no register is specified.
- **Named registers (`"a`–`"z`)** `[vim]` — 26 general-purpose registers
  explicitly selected before an operator/paste (`"ayy` yanks a line into
  register `a`); using an uppercase letter (`"A`) appends instead of
  overwriting.
- **Numbered registers (`"0`–`"9`)** `[vim]` — `"0` always holds the most
  recent yank specifically; `"1`–`"9` form a rotating history of the last few
  deletes/changes (`"1` most recent, shifting down as new deletes occur).
- **Special registers** `[vim]` — `"%` current filename, `".` last inserted
  text, `":` last command-line, `"/` last search pattern, `"+`/`"*` system
  clipboard, `"_` the "black hole" register that discards whatever is
  written to it (delete without clobbering other registers).
- **Small-delete register (`"-`)** `[vim]` — holds the text from the most
  recent delete/change that was less than one line.
- **Expression register (`"=`)** `[vim]` — evaluates a Vim expression and
  inserts/pastes its result, usable inline in Insert or command-line mode.

## Macros

- **Record (`q{register}` ... `q`)** `[vim]` — starts recording keystrokes
  into a named register (`qa` starts recording into `a`); a second `q` stops
  recording.
- **Replay (`@{register}`)** `[vim]` — replays the recorded keystrokes once;
  `@@` replays the last-played macro again; a count (`5@a`) replays it that
  many times in sequence, which is the idiomatic "do this edit to the next N
  lines" workflow.
- **Macros are just registers** `[vim]` — a macro is stored as literal text
  in a register, so it can be edited in place (paste the register into a
  buffer, tweak the keystroke text, yank it back) rather than needing a
  separate macro-editing UI.

## Marks

- **Manual marks (`m{a-zA-Z}`)** `[vim]` — `m{letter}` sets a mark at the
  cursor position; lowercase marks are buffer-local, uppercase marks are
  global (jump to that file+position from anywhere).
- **Automatic marks** `[vim]` — Vim maintains marks like `` `` `` (position
  before last jump), `` `. `` (last change), `` `^ `` (last insert-mode
  exit), `` `[ ``/`` `] `` (start/end of last yank or change) without any
  user action.
- **Jumping to marks** `[vim]` — `` `{mark} `` for exact position, `'{mark}`
  for first non-blank of that line, usable as a motion for operators
  (`` d`a ``).

## Jump list and change list

- **Jump list (`<C-o>`/`<C-i>`)** `[vim]` — Vim automatically records
  "jumps" (search, `G`, `%`, mark jumps, etc.) in a per-window list;
  `<C-o>`/`<C-i>` step backward/forward through it, letting you retrace
  navigation across the whole buffer or even across files.
- **Change list (`g;`/`g,`)** `[vim]` — separately records positions of
  recent edits; `g;`/`g,` step backward/forward through the last few places
  you changed text, independent of the jump list.

## Visual modes

- **Character-wise visual (`v`)** `[vim]` — selects an arbitrary span of
  characters, extendable with any motion; operators then act on exactly that
  span.
- **Line-wise visual (`V`)** `[vim]` — selects whole lines regardless of
  where the motion ends horizontally.
- **Block-wise visual (`<C-v>`)** `[vim]` — selects a rectangular column
  block across multiple lines; supports block insert/append (`I`/`A` in
  block-visual inserts/appends the typed text on every selected line
  simultaneously) and block delete/yank — this is Vim's closest built-in
  analog to a "multi-cursor" edit.
- **Reselect last visual (`gv`)** `[vim]` — reselects the most recent visual
  selection, e.g. to reapply an operator to the same region.
- **`o` to swap selection anchor** `[vim]` — while in any visual mode, `o`
  jumps the cursor to the opposite end of the selection, letting you adjust
  either boundary without restarting the selection.

## Search and substitute

- **Search with regex (`/`, `?`)** `[vim]` — Vim's own regex flavor
  (magic/nomagic, `\v`/`\V`/`\m`/`\M` switches) drives `/`/`?`, `:g`, and
  `:s`.
- **Search offsets** `[vim]` — appending an offset to a search pattern
  (`/pattern/e`, `/pattern/+2`, `/pattern/e-1`) lands the cursor at the match
  end, N lines after the match, or N characters before the match end,
  respectively, rather than always at the match start.
- **Substitute command (`:s`)** `[vim]` — `:[range]s/pattern/replacement/
  [flags]` replaces matches in the given line range (default: current line).
- **Substitute flags** `[vim]` — `g` replace all matches on a line (not just
  the first); `c` confirm each substitution interactively; `i`/`I` force
  case-insensitive/case-sensitive for this command regardless of
  `ignorecase`; `n` report match count without substituting; `e` suppress
  errors for no-match.
- **Substitute across ranges** `[vim]` — `%` as a range applies to the whole
  buffer (`:%s/.../.../g`); a visual selection followed by `:s` restricts the
  range to the selected lines; `:g/pattern/s/.../.../ ` combines global
  command with substitute for conditional replace.
- **Repeat last substitute (`&`, `g&`)** `[vim]` — `&` repeats the last `:s`
  on the current line; `g&` repeats it across the whole file with the last
  flags and search pattern.
- **Global command (`:g`/`:v`)** `[vim]` — `:g/pattern/{cmd}` runs an ex
  command on every line matching pattern; `:v` (or `:g!`) runs it on every
  line *not* matching, a general-purpose "for each matching line" primitive
  independent of `:s`.

## Dot-repeat

- **Repeat last change (`.`)** `[vim]` — repeats the most recent buffer-
  changing command (an operator+motion, an insert, `x`, a substitution,
  etc.) exactly as it was performed, including any count or inserted text;
  combined with a search (`/pattern<CR>`, then `.` to repeat an edit at each
  next match) this is one of Vim's most common "repeat an edit N times"
  idioms without needing a macro.
- **Count overrides on repeat** `[vim]` — prefixing `.` with a new count
  (`3.`) reapplies the last change but with that count substituted for the
  original, where applicable (e.g. last change was `dw`, `3.` becomes `d3w`
  in effect).
