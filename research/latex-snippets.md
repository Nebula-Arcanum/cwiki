# LaTeX/Math Snippet Workflow — Feature Inventory

Source: Gilles Castel, "How I'm able to take notes in mathematics lectures using LaTeX and Vim"
(castel.dev/post/lecture-notes-1/), plus linked follow-ups lecture-notes-2 (Inkscape figures) and
lecture-notes-3 (note management). Engine studied: Vim + UltiSnips + VimTeX.

## Snippet Engine Core Mechanics

- **Tab stops / placeholders**: Snippet body contains numbered fields `$1`, `$2`, … and a final
  cursor position `$0`; pressing Tab jumps forward through fields, Shift-Tab jumps backward. Lets
  you type a skeleton and fill in blanks without touching the mouse or arrow keys.
- **Mirrored/linked tab stops**: The same placeholder number can appear twice in a snippet body
  (e.g. `\begin{$1} ... \end{$1}`) so typing once updates both occurrences simultaneously —
  useful for matching environment names.
- **Auto-expand on trigger match (no key needed)**: Snippets flagged `A` expand automatically the
  instant the trigger text is typed, no explicit expand key required — used for very high
  frequency snippets like inline/display math.
- **Manual expand via key**: Snippets without the `A` flag only expand when the trigger key
  (Tab, configurable) is pressed after typing the trigger text — used when the trigger text is a
  plausible substring of normal words, to avoid accidental firing.
- **Word-boundary trigger flag (`w`)**: Snippet only fires if the trigger is preceded by a
  non-word character (start of a word), preventing mid-word false triggers.
- **Beginning-of-line trigger flag (`b`)**: Snippet only fires if the trigger is the first thing
  on the line.
- **Regex-triggered snippets (`r` flag)**: Trigger is a regular expression instead of a literal
  string, matched against the text before the cursor; capture groups are available inside the
  snippet body via `match.group(n)` in embedded Python. Used e.g. to auto-detect `letter+digit`
  and convert to a subscript, or to detect an already-typed expression ending in `)/ ` and wrap
  everything back to the matching `(` in a `\frac{}{}`.
- **Embedded Python interpolation (`` `!p ... ` ``)**: Snippet bodies can contain inline Python
  code blocks whose `snip.rv` return value is spliced into the expansion — enables arbitrary
  logic (regex capture manipulation, conditional whitespace, bracket-matching, calling external
  programs) rather than static text.
- **Priority ordering**: Snippets can declare a `priority` so that when multiple regex triggers
  could match, a more specific/complex one (e.g. full parenthesis-matching fraction) is tried
  before a simpler generic one.
- **Context-sensitive expansion (`context "expr"`)**: A snippet can require a Python predicate to
  return true before it's allowed to expand, evaluated against the current cursor position —
  this is how "math-mode-only" snippets are implemented (see below), turning a snippet engine that
  matches on text alone into one that also understands document structure/semantics.
- **Postfix/suffix snippets**: Snippets triggered by typing after an existing symbol rather than
  before it (e.g. typing `pbar` after already having `p`) via regex triggers anchored to a
  preceding identifier, so decorations read in the natural math order (variable first, decoration
  second) instead of prefix-notation macros.
- **Visual-selection placeholder (`${VISUAL}`)**: If text is visually selected before the snippet
  key is pressed, that selection is inserted into a placeholder slot instead of leaving it empty —
  lets you select an existing expression and wrap it (e.g. into a fraction numerator) instead of
  retyping it.
- **Course-specific / scoped snippet sets**: Separate snippet files can be loaded per subject
  (e.g. a quantum-mechanics file defining `<q|` → `\bra{\psi}` bra-ket notation with a
  domain-specific letter substitution), layered on top of a global snippet set, via runtime-path
  symlink switching to whatever course is "current."

## Math-Mode / Context Awareness

- **Math-zone detection predicate**: A helper function (`math()`) queries the syntax engine for
  whether the cursor is currently inside a math environment (inline `$...$`, display `\[...\]`,
  or environments like `align`), used as a gate on math-only snippets so ordinary English words
  containing a trigger substring (e.g. "sr" inside many English/Dutch words) don't misfire.
- **Comment-zone detection predicate**: Analogous helper (`comment()`) so snippets don't expand
  inside LaTeX comments.
- **Environment-membership predicate**: Helper (`env(name)`) checks whether the cursor is inside
  a specific named LaTeX environment, allowing snippets scoped to e.g. only a `pmatrix` or a
  particular custom environment.
- **Smart inline-math wrapper snippet**: A single trigger inserts `$...$` and, via embedded
  Python, inspects the character immediately after the insertion point to decide whether to also
  insert a trailing space (skips it before punctuation), avoiding manual spacing cleanup.

## Concrete Example Snippets

- `//` → `\frac{}{}` with two tab stops for numerator/denominator (basic fraction).
- Regex fraction snippet: typing an expression ending in `)/` auto-wraps everything back to the
  matching `(` into `\frac{...}{}`, using an embedded Python bracket-depth counter to find the
  matching parenthesis rather than a simple regex.
- `sr` → `^2` (squared), context-gated to math mode only, since "sr" is a common substring in
  ordinary words.
- `cb` → `^3` (cubed), same pattern.
- Auto subscript: typing a letter immediately followed by a digit (regex `([A-Za-z])(\d)`) is
  rewritten live into `letter_digit` (proper LaTeX subscript), so `x1` becomes `x_1` as you type.
- `mk` → `$$` (inline math shell) with the smart trailing-space logic above.
- `dm` → `\[\]` (display math shell).
- `phat` → `\hat{p}` and `zbar` → `\overline{z}`: postfix decoration snippets, generalized via a
  single regex snippet `([a-zA-Z])bar` → `\overline{<letter>}` rather than one snippet per letter.
- `lim` → `\lim_{n \to \infty}` (common operator template).
- Greek letters: short mnemonic triggers (e.g. `\alpha`-style triggers) expand to the LaTeX greek
  macro; combined with conceal (below) they display as the actual glyph immediately after
  expansion.
- Domain-specific bra-ket: `<q|` → `\bra{\psi}` in a quantum-mechanics-specific snippet file,
  substituting a mnemonic letter for a specific symbol used throughout that course.

## External Computation From Inside a Snippet

- **SymPy-evaluated snippet**: A snippet bounded by a trigger phrase (e.g. `sympy ... sympy`)
  passes the enclosed text into a live Python/SymPy `eval`, converts the result back into LaTeX,
  and substitutes it inline — lets you type a raw expression and have it symbolically evaluated
  and formatted without leaving the editor.
- **External CAS subprocess snippet**: Equivalent idea shelling out to Mathematica/`wolframscript`
  for evaluation, same trigger-bounded pattern.

## Vim/Editor Integration

- **Filetype-scoped snippet loading**: Snippet files are associated with the `tex` filetype so
  the math snippets only load in LaTeX buffers, not globally.
- **Persistent tab-stop navigation keys**: Expand/jump-forward/jump-backward all bound to a single
  key pair (Tab / Shift-Tab) so hands never leave the home row during a snippet fill-in.
- **Conceal mode for readability**: Vim's `conceallevel`/`concealcursor` plus a tex-specific
  conceal setting rewrites source markup for display only — `\alpha` renders as `α`, `\in` as
  `∈`, math delimiters (`$`) are hidden — so the buffer visually resembles the rendered math while
  the underlying LaTeX source is untouched and still editable/greppable.
- **Inline spell-check with fast correction**: `spell` is enabled with a specific dictionary/
  language, and a single mapped key combo jumps to the previous misspelling and accepts the top
  suggestion in one keystroke (chained `[s` jump + `1z=` accept + cursor restore), so prose inside
  the notes gets corrected without breaking flow — spell-check is implicitly not applied inside
  math zones (Vim's syntax-aware spellcheck skips math regions), so equations aren't flagged as
  misspelled words.

## Diagram/Figure Workflow (from lecture-notes-2, Inkscape rather than TikZ)

- **Deliberate choice against code-based diagrams**: Author explicitly rejects TikZ/Asymptote for
  in-lecture figures because drawing is a graphical task better done with direct manipulation than
  by writing/compiling code under time pressure.
- **PDF+LaTeX export pipeline**: Inkscape's "PDF+LaTeX" export keeps vector graphics in a PDF and
  any text as a separate LaTeX-typeset overlay, so figure labels/formulas match the document's
  font and math rendering exactly.
- **Editor-triggered figure creation**: A keybinding from inside Vim creates a new figure file
  from a template and opens it in Inkscape, avoiding manual file/directory setup mid-note.
  file-watcher auto-re-exports the PDF+LaTeX whenever the SVG is saved, so the compiled document
  always reflects the latest edit with no manual export step.
- **Fast figure-picker**: A keybinding opens a selector over existing figures for quick re-editing
  of a previously drawn diagram.
- **Custom low-level keyboard-shortcut layer inside the drawing tool**: A separate hotkey daemon
  intercepts keystrokes to add chorded style shortcuts (e.g. two keys held together apply a
  preset stroke+fill style), named/saved custom styles recalled by typing a short name, and
  one-key insertion of pre-built shapes (axes, common curves) — same "define once, invoke by short
  trigger" philosophy as the text snippets, applied to vector drawing.
- **In-place LaTeX text entry inside the drawing tool**: A key opens a small Vim instance for
  typing a text/label, and a variant renders a typed LaTeX formula straight to SVG for use as a
  diagram label, so switching to the text editor for figure labels isn't needed.

## Note/Course Management Automation (from lecture-notes-3, adjacent workflow)

- **Hierarchical file layout + shared preamble**: Notes organized university → degree → semester →
  course, with one shared LaTeX preamble per semester so macros/snippets defined once apply
  everywhere.
- **"Current course" symlink**: A symlink is repointed to whichever course is active, so global
  keybindings/scripts and snippet loading resolve to the right course-specific files without
  per-course reconfiguration.
- **Two-tier snippet loading**: Global snippets (all math/prose shortcuts) live in the main dotfiles;
  a second, course-specific snippet file is loaded on top via the current-course path — same
  mechanism as the quantum-mechanics bra-ket example above.
- **Partial/fast compilation**: Only the most recent lecture(s) are compiled by default for speed,
  with a keybinding to opt into compiling the full document when a complete view is needed.
- **One-key new-lecture creation**: A keybinding creates a new dated lecture file from a template
  and inserts it into the master document automatically.
- **One-key lecture browser**: A keybinding lists all lectures with parsed metadata (title, date,
  week) for fast jump-to-lecture navigation.
- **YAML-driven course metadata**: Small per-course YAML files store title/URL/abbreviation, read
  by shell scripts (via a YAML query tool) to drive shortcuts and status displays without
  hardcoding values per script.
- **Global hotkey daemon**: A system-wide hotkey manager (independent of the editor) dispatches
  the above note-taking shortcuts (new lecture, figure edit, lecture list, etc.) so they work the
  same regardless of which application has focus.
