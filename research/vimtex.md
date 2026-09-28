# vimtex feature inventory (source: vimtex)

Source: `lervag/vimtex` — the help file `doc/vimtex.txt` (~7800 lines, read in full), supplemented by
the repository's `autoload/vimtex/` sources where the docs are vague: `syntax.vim`, `text_obj.vim`,
`motion.vim`, `delim.vim`, `re.vim`, `qf.vim`, `qf/latexlog.vim`, `options.vim`. Nothing was cloned.

vimtex is a filetype plugin, not an editor: nearly every feature is a thin LaTeX-aware layer on top
of Vim machinery (regex `syntax` groups, `conceallevel`/`concealcursor`, `foldexpr`/`foldtext`,
`quickfix`/`errorformat`, `includeexpr`, `searchpos()` with timeouts). Each section below flags where
cwiki would have to build the substrate itself rather than inherit it.

## Syntax zones / context detection

- **Everything context-aware is one query against a syntax-group stack** — vimtex does not maintain
  its own document model for "where am I". It asks Vim for the list of syntax groups active at a
  position (`synstack()` → group names) and pattern-matches that list. `vimtex#syntax#stack()`
  returns the stack at the cursor or at an explicit `(line, column)`, and — important detail —
  subtracts 1 from the column when called in insert mode, because in insert mode the cursor sits
  *after* the character whose context you actually care about. cwiki has no `synstack()`: it must
  build the zone stack itself (a per-line incremental highlighter/tokeniser keeping an open-zone
  stack), which is the real cost of this whole feature family.
- **`vimtex#syntax#in(name [, line, col])`** — the generic predicate: true if any group in the stack
  has a name matching the regex `^name`. This one function, plus the naming convention of the syntax
  groups, is the entire context API that text objects, motions, delimiter matching and the
  user-facing snippet gates are built on.
- **`vimtex#syntax#in_comment([line, col])`** — `in('texComment')`. Used to keep snippets, motions
  and delimiter matching out of `%` comments.
- **`vimtex#syntax#in_mathzone([line, col])`** — the load-bearing predicate, and it is deliberately
  *not* "is `texMathZone` anywhere in the stack". It reverses the stack (innermost group first),
  finds the first group matching any of `texMathZone`, `texMathText`, `texMathTag`, `texRefArg`, and
  returns true only if that innermost match is a `texMathZone*`. The three non-math matchers are
  "holes": `\text{...}` (`texMathText`), tag contents and `\label{...}`/`\ref{...}` arguments
  (`texRefArg`) are inside a math zone but are *not* math, so a math-only snippet must not fire
  there. `vimtex#syntax#add_to_mathzone_ignore(regex)` lets package add-ons register further hole
  groups. This innermost-wins rule is exactly the semantics cwiki needs for "math snippets must
  never rewrite chemistry or diagram syntax": the answer is not a boolean per zone type but
  "which zone is innermost at the cursor".
- **Math zones are distinguished by how they were opened** — separate groups per delimiter form, all
  linked to `texMathZone`: `texMathZoneLI` (`\( \)`), `texMathZoneLD` (`\[ \]`), `texMathZoneTI`
  (`$ $`), `texMathZoneTD` (`$$ $$`), `texMathZoneEnv` (`\begin{equation}` …),
  `texMathZoneEnvStarred`, `texMathZoneEnsured` (`\ensuremath{}`). The delimiters themselves get
  their own groups (`texMathDelimZoneLI/LD/TI/TD`), so "am I on the `$`" is distinguishable from
  "am I inside the `$…$`".
- **Asymmetry of `$`-zones, documented in code** — for `$ … $` and `$$ … $$` the *opening*
  delimiter is inside the math zone but the *closing* one is not. `vimtex#motion#math()` relies on
  this asymmetry to decide whether a found `$` opens or closes a zone (for a closing `$` it probes
  two positions back and asks `in_mathzone()` there). Any cwiki zone model needs an explicit rule
  for which side of a symmetric delimiter belongs to the zone, or these queries become ambiguous.
- **Verbatim and comment zones are first-class** — `texVerbZone` (`\begin{verbatim}`),
  `texVerbZoneInline` (`\verb+…+`), `texComment`, `texCommentTodo`. Nested-syntax regions
  (`texPythonCodeZone`-style, created for `minted`/`listings`/`robust_externalize`) are also zones,
  so code inside a fenced LaTeX environment gets that language's syntax and is excluded from LaTeX
  context tests.
- **Comment avoidance without the syntax engine** — for the cheap cases vimtex uses pure regex
  lookbehind instead of the syntax stack: `g:vimtex#re#not_bslash` = `\v%(\\@<!%(\\\\)*)@<=` (the
  position is not escaped by an odd number of backslashes) and `g:vimtex#re#not_comment` =
  `\v%(<not_bslash>\%.*)@<!` (the position is not preceded on the line by an unescaped `%`). All the
  `[`/`]` motions prepend `not_comment` to their patterns. This is a useful, cheap trick for cwiki:
  escape-awareness and comment-awareness as a regex prefix rather than a parse.
- **Delimiter searches can exclude a syntax zone explicitly** — the internal delimiter finder takes
  a `syn_exclude` option; when a candidate match is inside that syntax group it skips it and
  continues searching backwards. This is how `%` and `dsd` avoid matching a `(` that lives inside a
  comment or verbatim block.
- **`% VimTeX: SynIgnore on` / `off`** — an in-document escape hatch that switches a region to a
  severely reduced syntax zone, for code that no heuristic parser can survive (catcode games,
  `\catcode`\$=11`). Overleaf's `%%begin novalidate` / `%%end novalidate` is accepted as a synonym.
  Cheap insurance worth copying: a "stop parsing here" marker beats a parser that silently
  mis-highlights the rest of the note.
- **Explicitly best-effort, by design** — the docs state plainly that "LaTeX is a macro expansion
  language and it is impossible to write a fully correct syntax parser without running the `tex`
  compiler itself", and that vimtex aims for a pragmatic trade-off. It also documents that a command
  like `\foo{bar}{baz}` cannot be parsed correctly without knowing `\foo`'s definition, and exposes
  the heuristic as a user hook (`g:vimtex_parser_cmd_separator_check`): the parser greedily swallows
  `<overlay>`, `[opt]`, `{arg}` groups as long as a predicate accepts the text between groups (the
  default accepts a single newline plus indentation, rejects a plain space). Every downstream
  feature inherits that fuzziness.
- **Custom zones are user-declarable** — `g:vimtex_syntax_custom_envs` registers an environment as a
  math zone (`math: v:true`), as a plain region, or as a host for nested syntax, including
  *predicated* nesting (pick the nested language from a string in the environment's optional
  argument, e.g. `language=python` → python syntax). `g:vimtex_syntax_custom_cmds` and
  `..._with_concealed_delims` register per-command styling, math-ness, conceal characters and
  argument styles (`bold`, `ital`, …), and whether an argument is spell-checked (`argspell`).
- **Syntax group naming is a deliberate, documented scheme** — `texCmd{Type}`, `tex{Type}Opt`,
  `tex{Type}Arg`, `tex{Type}Zone`, primitives linked to conventional highlight groups so a
  colourscheme only has to theme the primitives. The docs' tables 1–5 enumerate the groups
  (`texCmd`, `texOpt`, `texArg`, `texDelim`, `texRefArg`, `texFileArg`, `texEnvArgName`,
  `texMathOper`, `texMathSuperSub`, `texMathError`, `texStyleBold`/`Ital`/`Under`…). Worth copying
  as a convention for cwiki's highlight/zone identifiers.
- **Failure mode when the substrate is missing is explicit** — with tree-sitter highlighting instead
  of Vim syntax, `i$`/`a$` and everything else built on the stack stop working; `vimtex#text_obj#
  delimited()` refuses outright with a warning when `g:vimtex_syntax_enabled` is 0. The docs advise
  tree-sitter users not to enable it for LaTeX. Direct evidence for cwiki: the zone engine is not an
  optional extra, it is the prerequisite for a third of the feature list.

## Text objects

All are defined for both operator-pending and visual mode (`onoremap`/`xnoremap`), take a `[count]`,
and are dot-repeatable through the normal operator machinery.

- **`ie` / `ae` — environment** — `ae` spans `\begin{x}…\end{x}` inclusive; `ie` is the content
  between them. "Inner" is computed from the parsed `\begin` *command*, not from the literal
  `\begin{x}` text: the inner start is the end of the whole `\begin{x}[opt]{arg}` command, so an
  environment with an optional argument block (`\begin{axis}[width=6cm,…]`) still gets a correct
  inner start. Never matches the top-level `document` environment.
- **Block-shaped inner objects collapse to whole lines** — if the open delimiter is alone at the end
  of its line and the close delimiter alone at the start of its line ("is_inline" in the source),
  `ie` shrinks to the full lines in between rather than leaving ragged partial lines; with an
  operator listed in `g:vimtex_text_obj_linewise_operators` (default `['d', 'y']`) the selection is
  promoted to linewise `V`. `c` deliberately stays characterwise, since making `cie` linewise would
  delete the indentation you are about to type into.
- **`i$` / `a$` — math zone** — the only text objects that *require* the syntax engine
  (`g:vimtex_syntax_enabled`), because deciding which `$` opens a zone is a syntax question.
  Handles `$…$`, `$$…$$`, `\(…\)`, `\[…\]` and math environments. Counts are explicitly ignored for
  `i$`/`a$` (no "second-enclosing math zone"), and the surrounding-search is capped at 3 attempts
  versus 100 for other types — a pragmatic performance bound.
- **`id` / `ad` — delimiter** — matches the configured delimiter pairs, including math delimiters
  with size modifiers: `ad` on `\left( asd \right)` covers the modifiers too, `cid` leaves
  `\left(█ \right)`. The recognised set is `g:vimtex_delim_list`, split into `env_tex`
  (`\begin`/`\end`), `env_math` (`\(`/`\)`, `\[`/`\]`, `$$`, `$`), `delim_tex` (`[]`, `{}` with
  unescaped-brace regexes), `delim_math` (`()`, `[]`, `\{\}`, `\langle\rangle`, `\lbrace`,
  `\lvert`, `\lVert`, `\lfloor`, `\lceil`, `\ulcorner`) and `mods`
  (`\left/\right`, `\bigl/\bigr` … `\Biggl/\Biggr`, `\big`, `\Big`, `\bigg`, `\Bigg`). Users can
  extend it (German/French quote pairs are the documented example) and vimtex auto-derives the
  matching regexes from the literal `name` entries when `re` is omitted.
- **`ic` / `ac` — command** — `ac` is the command plus all its argument groups; `ic` is *just the
  command name* (`\comm█and{arg}` + `dic` → `\{arg}`; `gUac` upcases command and argument). How many
  argument groups `ac` swallows is decided by the greedy-parse heuristic above, so `ic`/`ac`
  greediness is user-tunable rather than exact.
- **`iP` / `aP` — section/part** — always linewise (`V`). The section's extent is found by ranking
  section commands by depth in a table (`document` 0, front/main/backmatter and `part` 1, `chapter`
  2, `section` 3, `subsection` 4, `subsubsection` 5, `paragraph` 6, `subparagraph` 7): search
  backwards for any section command, then forwards for the next command whose rank is *less than or
  equal* to it — that's the end. `iP` trims to the first/last non-blank line inside. Recognises
  `\addsec`/`\addchap`/`\addpart` and "fake" sections written as comments (`% Fakesection title`).
  With a count or by repeating in visual mode, the selection grows to the enclosing section level.
- **`im` / `am` — `\item`** — `am` is the whole item block up to the next `\item` or the end of the
  list environment (linewise); `im` is the item's text, starting after `\item` and any of its
  arguments. Nesting is handled with a depth counter over intervening `\begin`/`\end` pairs (bailing
  out past depth 5), so an item containing a nested list still selects correctly.
- **Counts expand to enclosing objects, not to siblings** — for env/math/delim objects, a count
  re-runs the search from just before the previously found opening delimiter, so `2ie` selects the
  next environment out. In visual mode, re-pressing the object when the current selection already
  equals the object grows to the enclosing pair (the "select next pair if we reached the same
  selection" logic). If the count cannot be satisfied, vimtex falls back to the last successful
  object rather than failing (except in `targets.vim` mode).
- **Empty inner objects are handled explicitly** — if the inner region is empty, `y` aborts (nothing
  to yank), while `c`/`d` insert a placeholder character so the operator has something to act on and
  the cursor ends up in the right place. Small, but exactly the kind of edge case a from-scratch
  implementation gets wrong first.
- **Optional `targets.vim` backend** — `g:vimtex_text_obj_variant` (`auto`/`vimtex`/`targets`) swaps
  the env/command objects onto targets.vim, which adds `I`/`A` (exclude inner / include outer
  whitespace) and `n`/`l` (next/last) modifiers. Evidence that "inner/around" is really a 4-way
  (`i`/`I`/`a`/`A`) plus directionality design space, if cwiki wants it.

## Motions

- **Section motions `]]` `][` `[[` `[]`** — next section start, next section end, previous section
  start, previous section end; all `[count]`-aware and |exclusive|. The pattern is anchored to the
  start of a line, prefixed with `not_comment`, and covers `\part`…`\subparagraph`, `\appendix`,
  `\frontmatter`/`\mainmatter`/`\backmatter`, `\addsec`/`\addchap`/`\addpart`,
  `\begin{document}`/`\end{document}` and `% Fake…` comment sections.
- **Environment motions `]m` `[m` `]M` `[M`** — next/previous `\begin{`, next/previous `\end{`.
  Implemented as a plain `search()` with the `not_comment` prefix — no syntax stack needed, so these
  stay fast.
- **Math-zone motions `]n` `[n` `]N` `[N`** — next/previous start and end of a math zone. These do
  need the syntax stack: the regex finds any candidate delimiter (`\[`, `\(`, `\begin{`, `$$`, `$`)
  using `search()`'s `p` flag to learn *which* alternative matched, then confirms with
  `in_mathzone()`, iterating up to 6 candidates before giving up. `\[` and `\(` are trusted
  immediately; `\begin{` and `$`-forms are verified, with the closing-`$` case probing the position
  two characters back (see the asymmetry note above).
- **Frame motions `]r` `[r` `]R` `[R`** — beamer `\begin{frame}`/`\end{frame}`. Same cheap
  regex-search implementation, just a fixed environment name.
- **Comment motions `]*` `[*` (`]/` `[/` for starts)** — move to the next/previous start or end of a
  *block* of `%` comment lines, using lookahead/lookbehind so a run of consecutive comment lines
  counts as one block.
- **`%` extended to LaTeX pairs** — jumps between matching `\begin`/`\end`, `\left(`/`\right)`,
  `$`/`$`, `\[`/`\]` and ordinary brace/bracket pairs, in normal, visual and operator-pending mode.
  Matching is a bounded search, not a parse: `g:vimtex_delim_stopline` (default 500 lines in each
  direction) and `g:vimtex_delim_timeout` / `g:vimtex_delim_insert_timeout` (300 ms / 60 ms) cap the
  work, and the docs are candid that raising them trades accuracy for lag. vimtex explicitly does
  *not* match "middle" delimiters (`\middle|`, `\item` within a list, `\toprule`/`\midrule`,
  `\if`/`\else`/`\fi`) — that is the advertised reason to use the `vim-matchup` plugin instead,
  which adds `g%`, `[%`, `]%` and `a%`/`i%` on top.
- **All motions push the jump list** — each motion function runs `normal! m`` first so `<C-o>`
  returns to the pre-motion position, and counts are applied by repeating the search rather than by
  multiplying a pattern.
- **Delimiter match highlighting** — `g:vimtex_matchparen_enabled` (on by default) replaces Vim's
  `matchparen` with a LaTeX-aware version that highlights `\begin`/`\end` and modified math
  delimiters. The docs and FAQ both admit it can be slow, because it uses syntax information to skip
  commented delimiters on every cursor move; the suggested remedies are the timeout/stopline knobs,
  disabling it, or `vim-matchup` with deferred highlighting.

## Manipulation commands

Default mappings in parentheses. All of these are dot-repeatable, and all of the "change" variants
prompt for input (with completion candidates) and echo a description of what they are waiting for
unless `g:vimtex_echo_verbose_input` is disabled.

- **Delete surrounding environment (`dse`), command (`dsc`), math zone (`ds$`), delimiter (`dsd`)** —
  removes the wrapper, keeps the content. Whitespace cleanup is part of the operation: when a
  delimiter is removed, leading/trailing whitespace on the affected side is collapsed so you don't
  get `\left(  x` residue.
- **Change surrounding environment (`cse`), command (`csc`), math zone (`cs$`), delimiter (`csd`)** —
  same targets, but prompts for the replacement; environment names get command-line completion, and
  `g:vimtex_env_change_autofill` can pre-fill the current name so `align` → `aligned` is a two-key
  edit.
- **Toggle starred form (`tsc` command, `tss` environment)** — `\section` ↔ `\section*`,
  `equation` ↔ `equation*`. Commands are filtered through a whitelist
  (`g:vimtex_toggle_star_cmds`: `part`, `%(sub)*section`, `%(sub)*paragraph`, `[vh]space`,
  `\w*cite\w*`, `\w*ref`, `%(re)?newcommand`, `providecommand`, `DeclareRobustCommand`,
  `DeclareMathOperator`, `%(re)?newenvironment`, `includegraphics`, `verb`) so that starring
  nonsense like `\sin*` or `\frac*` is a no-op. An unmapped `tss` variant
  (`vimtex-cmd-toggle-star-agn`) toggles whichever of command/environment is closest to the cursor.
- **Toggle complementary environment (`tse`)** — cycles through a user map; default
  `itemize` ↔ `enumerate` (`g:vimtex_env_toggle_map`).
- **Toggle inline ↔ display math (`ts$`)** — `$f(x)=1$` becomes a `\[ … \]` block on its own lines
  and back. The cycle is a configurable map, not a boolean: default
  `$` → `\[`, `\[` → `equation`, `$$` → `\[`, `\(` → `$` (`g:vimtex_env_toggle_math_map`), so
  repeated presses walk inline → display → numbered environment.
- **Toggle delimiter size modifier (`tsd`, reverse `tsD`)** — `(…)` ↔ `\left(…\right)` by default,
  but really a cycle over `g:vimtex_delim_toggle_mod_list` (can include `\bigl/\bigr`,
  `\Bigl/\Bigr`, `\biggl`, `\Biggl`, `\mleft/\mright`). Accepts a `[count]` to jump several steps.
  Normal mode acts on the closest surrounding delimiter; visual mode toggles *every* delimiter fully
  contained in the selection and preserves the selection afterwards.
- **Add `\left`/`\right` to everything in the current math scope (`<F8>`)** — walks outwards while
  `in_mathzone()` holds and adds modifiers to all surrounding unmodified delimiters at once.
- **Toggle inline fraction ↔ `\frac` (`tsf`)** — in visual mode, converts the selection if it looks
  like either `\frac{a}{b}` or `a / b`; in normal mode it detects the surrounding fraction command
  *or* inline fraction expression around the cursor. Rules live in `g:vimtex_toggle_fractions`
  (`INLINE` ↔ `frac`, `dfrac` → `INLINE`).
- **Toggle trailing line-break macro `\\` (`tsb`)** — adds/removes `\\` at the end of the current
  line; small, but heavily used inside `align`/`array`.
- **Surround with an environment (`<F6>`)** — linewise only: prompts for a name, puts `\begin{ENV}`
  on the line above and `\end{ENV}` below, then re-indents the region with `==`. Variants for the
  current line, an operator motion (unmapped by default) and a visual selection.
- **Create a command (`<F7>`, insert/normal/visual)** — in insert mode it turns the word just typed
  into `\word{` and parks the cursor at the end (the closing brace is deliberately left to the user,
  with a documented one-line remap to add it); in normal/visual mode it prompts for a command name
  and wraps the word/selection in it.
- **Close the current environment or delimiter in insert mode (`]]`)** — inserts the correct
  `\end{…}` / closing delimiter for whatever is currently open, skipping the top-level `document`
  environment. This is context-aware closing rather than paired insertion — the complement to
  autopairs, not a duplicate of it.
- **Deliberate non-features** — vimtex does *not* provide surround.vim's `ys{motion}e` "add
  surrounding" direction; the docs show the two `b:surround_*` definitions to add it yourself, and
  point at `vim-sandwich` for richer LaTeX surroundings. Worth noting as a scope precedent: vimtex
  ships delete/change/toggle but leaves "add" to a general surround mechanism.

## Insert-mode mappings (imaps)

- **Leader-prefixed math symbol mappings** — a single leader (`` ` `` by default,
  `g:vimtex_imaps_leader`) plus one or two characters expands to a LaTeX command: `` `a `` →
  `\alpha`, `` `e `` → `\epsilon`, `` `D `` → `\Delta`, `` `8 `` → `\infty`, `` `2 `` → `\sqrt`,
  `` `. `` → `\cdot`, `` `jl `` → `\rightarrow`, plus the full Greek alphabet, set/logic symbols and
  arrow families (~70 entries in `g:vimtex_imaps_list`).
- **Context-gated expansion via wrapper functions** — every imap has a `wrapper`:
  `vimtex#imaps#wrap_math` (the default — expand *only* inside a math zone, otherwise the keys
  insert themselves literally), `vimtex#imaps#wrap_trivial` (always expand), or
  `vimtex#imaps#wrap_environment` (expand only inside named environments, and optionally expand to a
  *different* RHS per environment via a `context` list). This is the same idea as UltiSnips'
  `context` predicate, built directly on `in_mathzone()`.
- **Computed right-hand sides** — with `expr: 1` the RHS is evaluated before insertion;
  `vimtex#imaps#style_math('mathrm')` wraps the expansion in a command when in math mode, which is
  how `#r` → `\mathrm{…}` style maps are defined. Per-map custom leaders are allowed.
- **Not a snippet engine, on purpose** — vimtex removed automatic snippet expansion after issue #295
  and documents that snippets belong in UltiSnips/neosnippet, with worked examples of
  auto-triggering `__` → `_{$1}$0`. It also notes that anonymous UltiSnips snippets don't nest and
  that real auto-trigger snippets do — a relevant warning for cwiki's own engine.
- **Discoverability** — `:VimtexImapsList` (`<localleader>lm`) dumps the active insert-mode mappings
  into a scratch buffer closed with `q`/`<esc>`. Individual maps can be suppressed with
  `g:vimtex_imaps_disabled`, and the whole feature with `g:vimtex_imaps_enabled`.

## Conceal

- **Conceal is Vim's, the rules are vimtex's** — vimtex only attaches `conceal`/`cchar` attributes
  to its syntax items. Whether anything is hidden is decided by Vim's `conceallevel` (must be 2) and
  `concealcursor` — and vimtex does *not* set either; the docs merely tell the user to. cwiki has
  neither option, so it must implement the display-substitution layer *and* its policy: a conceal
  pass runs at render time over each visible line, replacing marked source spans with a glyph while
  the buffer text is untouched, and the cursor-line policy is a decision cwiki must make rather than
  inherit.
- **Cursor-line behaviour (the `concealcursor` semantics to reproduce)** — in Vim, conceal is
  suspended on the line the cursor is on unless `concealcursor` names the current mode (`n`, `v`,
  `i`, `c`). The default is empty, i.e. the cursor line shows raw source in every mode and all other
  lines show concealed glyphs. That default exists because concealment changes the mapping between
  buffer columns and screen columns, so editing a concealed line is disorienting and horizontal
  cursor motion appears to jump. The practical consequence for cwiki: concealment is a per-line,
  per-mode decision, and the editor needs a column mapping between source columns and display
  columns for *every* cursor motion, selection and mouse-less navigation on a concealed line.
- **Conceal is categorised, and each category is a separate switch** — `g:vimtex_syntax_conceal` is
  a dictionary of independently toggleable categories, all on by default except `sections`:
  `accents` (`\^a` → `â`), `ligatures` (`\aa` → `å`, `''` → `“`), `cites`, `fancy` (e.g. `\item` →
  `○`), `texTabularChar` (`\\` → `⏎`), `spacing` (`\quad`, `\hspace{1em}`), `greek` (`\alpha` → `α`),
  `math_bounds` (hide the `$`, `$$`, `\(`, `\)`, `\[`, `\]` delimiters themselves),
  `math_delimiters` (`\Biggl\langle … \Biggr\rangle` → `〈 … 〉`, i.e. modifier + command → one
  glyph), `math_fracs` (`\frac 1 2` → `½`), `math_super_sub` (`x^2` → `x²`),
  `math_symbols` (a very large table — the docs warn "be warned!"), `sections`
  (`\section{Test}` → `# Test`, Markdown ATX style), `styles` (hide the `\emph{`/`\textit{`/`\textbf{`
  wrapper and show the argument in the corresponding attribute). `g:vimtex_syntax_conceal_disable`
  kills the lot in one setting.
- **Citation conceal has its own sub-configuration** — `g:vimtex_syntax_conceal_cites` chooses
  `type` `'icon'` (`\cite{Knuth1981}` → `📖`) or `'brackets'` (`→ [Knuth1981]`), the icon character,
  and a `verbose` flag deciding whether the optional argument is shown too
  (`[Figure 1][Knuth1981]` vs `[Knuth1981]`).
- **Package-level conceal** — `g:vimtex_syntax_packages` carries per-package `conceal` flags
  (`amsmath`, `babel`, `hyperref`, `fontawesome5` — the last exists *only* to conceal, so disabling
  its conceal disables the package add-on).
- **User-defined conceal rules without writing syntax code** — `concealchar` on a custom command
  (`{'name': 'R', 'cmdre': 'R>', 'mathmode': 1, 'concealchar': 'ℝ'}` shows `\R` as `ℝ`), and
  `g:vimtex_syntax_custom_cmds_with_concealed_delims` for multi-argument commands with per-position
  replacement characters: `\ket{x}` → `|x>` via `cchar_open`/`cchar_close`, `\binom{n}{k}` → `(n|k)`
  via `cchar_open`/`cchar_mid`/`cchar_close`. Leaving a `cchar_*` undefined hides that part entirely.
- **Font dependency is called out** — the docs spend a paragraph warning that conceal needs a font
  with good Unicode coverage and that this is the user's problem. For cwiki (kitty-only) this is
  simpler: glyph width comes from the terminal, but display-width accounting for the substituted
  glyph (double-width, combining marks) is still cwiki's problem.
- **Conceal is also the reason vimtex rejects tree-sitter highlighting** — tree-sitter cannot
  conceal, so users who want conceal must keep the regex syntax engine. A direct argument that the
  conceal layer and the zone/highlight layer are the same subsystem, not two.

## Table of contents

- **A parsed project outline in its own buffer** — `:VimtexTocOpen` / `:VimtexTocToggle`
  (`<localleader>lt` / `lT`) parse the whole multi-file project (not just the current buffer) and
  show entries in a scratch window. `<cr>` or `<space>` jumps to an entry; the window position is
  configurable (`split_pos`, default `vert leftabove`, `split_width` 30, or `full` to reuse the
  current window), and `mode` chooses window / window+location-list / location-list only.
- **Four independent "layers"** — `content` (real sections/parts/chapters, plus beamer frames),
  `todo` (`TODO`/`FIXME` keywords in comments per `g:vimtex_toc_todo_labels`, `\todo{}` commands and
  `fixme`-package commands), `label` (`\label{…}`) and `include` (included files). Each layer can be
  on/off initially (`layer_status`) and toggled live from inside the ToC with a hotkey
  (`layer_keys`, defaults `C`/`T`/`L`/`I`). TODOs are hoisted to the top of the list by default
  (`todo_sorted`).
- **Depth control and folding** — `tocdepth` (default 3) mimics LaTeX's own `tocdepth` and also sizes
  the section-number column; `fold_enable` and `fold_level_start` fold the ToC tree itself;
  `indent_levels` indents by level; `show_numbers` toggles section numbers; `hide_line_numbers`
  strips the gutter in the ToC window.
- **Per-entry hotkeys** — with `hotkeys_enabled`, each visible entry gets a letter from
  `hotkeys` (`abcdegijklmnopuvxyz`) behind a `hotkeys_leader` (`;`), so jumping is
  `;` + letter rather than scrolling. Same idea as label-based 2D jumping, applied to a list.
- **Refresh policy is explicit** — `refresh_always` (default on) re-parses on every open; the docs
  recommend turning it off for very large projects and re-parsing from a `BufWritePost` autocommand
  or a manual mapping instead. A real precedent for cwiki's index/invalidation design.
- **Extensible via a matcher table** — the parser is a priority-ordered list of matchers, each a
  dictionary with `re` (the real pattern), `prefilter_re` or `prefilter_cmds` (a cheap pattern run
  first, for speed), `priority` (highest wins; only one matcher may claim a line), `in_preamble` /
  `in_content` scoping, a `title`, and optional `get_entry`/`continue` functions for multi-line
  entries. Built-ins can be disabled or re-prioritised (`g:vimtex_toc_config_matchers`), and custom
  ones added (`g:vimtex_toc_custom_matchers`) — e.g. index every `\begin{mycustomenv}`. The
  two-stage cheap-prefilter-then-real-regex design is a pattern cwiki's own indexer can reuse.
- **Manual entries** — a `% vimtex-include: /path/to/file` comment forces an `include`-layer entry
  for an arbitrary file (absolute or relative to the project root), and a file opened that way gets
  attached to the project even if it isn't LaTeX.
- **Multiple, purpose-built ToCs** — `vimtex#toc#new({…})` creates an independent ToC object with its
  own layer set and options, so a user can bind one key to "labels + todos" and another to
  "includes". The same entry collector is also exposed as a source for external pickers (fzf,
  fzf-lua, snacks, denite/unite) with a layer filter string (`'ctli'`), i.e. the outline data model
  is deliberately decoupled from its UI.

## Folding

- **Off by default, and implemented as a `foldexpr`** — `g:vimtex_fold_enabled` sets
  `foldmethod=expr`, `foldexpr=vimtex#fold#level(v:lnum)`, `foldtext=vimtex#fold#text()`. cwiki has
  no `foldexpr` substrate: it would need its own per-line fold-level function plus fold state per
  window.
- **Fold types, each independently configurable** (`g:vimtex_fold_types`, merged over
  `g:vimtex_fold_types_defaults`): `preamble`; `sections` (parts/chapters/sections/subsections/
  subsubsections, plus `appendix`/`frontmatter`/`mainmatter`/`backmatter` as top-level "parts", plus
  `% Fakesection` comment pseudo-sections folded at the real level); `envs` (any environment, with
  `whitelist`/`blacklist`; `document` is never folded); `env_options` (fold only a long optional
  argument block on a `\begin`, e.g. a pgfplots `\begin{axis}[…]`); `items` (`\item` blocks, in the
  environments listed in `g:vimtex_indent_lists`); `comments` (multiline comments, off by default);
  `comment_pkg` (`\begin{comment}` blocks, and folding is disabled *inside* them); `markers`
  (Vim-style `{{{`/`}}}` fold markers, but only recognised inside comments, with configurable
  `open`/`close` patterns); `cmd_single` (long single-argument commands: `\hypersetup{…}`,
  `\tikzset{…}`, `\pgfplotstableread{…}`, `\lstset{…}`); `cmd_single_opt` (`\usepackage[…]{name}`,
  `\includepdf[…]{}`); `cmd_multi` (`\newcommand{\xx}[3]{…}`, `\newenvironment`,
  `\providecommand`, `\Declare…` families); `cmd_addplot` (pgfplots `\addplot+[] table[] {…};`).
- **Fold levels are derived from the document, not fixed** — on open, vimtex parses which section
  levels and parts actually occur and assigns the top fold level accordingly (a document with no
  `\part` starts at `\section`), re-parsing whenever the file changes. The `sections.parse_levels`
  key enables finer level parsing that mirrors the ToC's numbering, and is off by default *because
  it is slow*.
- **Fold text is generated per type** — `foldtext` produces a one-line summary
  (`\begin{axis}[...]`, `\usepackage[...]{name}`, `\newcommand{\xx} ...`), and each fold type can
  override it with a `text(line, level)` function. `g:vimtex_fold_levelmarker` (default `*`) is the
  per-level marker character, and the docs recommend `set fillchars=fold:\ ` for clean fold lines.
- **Performance caveats are documented, not hidden** — "the `fold-expr` method of folding is well
  known to be slow, e.g. for long lines and large files". The mitigation is
  `g:vimtex_fold_manual`: keep `foldmethod=manual` (fast) but compute levels with the same expression
  *on demand only*, refreshing via `:VimtexRefreshFolds` and remapped `zx`/`zX`. The alternative
  offered is the FastFold plugin. Direct evidence for cwiki: a naive "recompute fold levels for
  every line on every change" design will be the first thing to feel slow on a long note.
- **Bib folding** — a separate `foldexpr`/`foldtext` pair for `.bib` files, enabled with
  `g:vimtex_fold_bib_enabled` (defaults to the tex setting), with `@article{Key}` identifiers
  aligned in the fold text and a truncation width option.

## Log and error parsing

- **The TeX log is turned into structured diagnostics through a big `errorformat`** — the
  `latexlog` method (default, `g:vimtex_quickfix_method`) sets a Vim `errorformat` and runs
  `caddfile` over the `.log`. cwiki has no `errorformat` engine; what vimtex expresses declaratively
  (a multi-line stateful pattern grammar with file stacks, continuation and ignore rules) is roughly
  a small parser cwiki must write by hand. Alternative methods (`pplatex`, `pulp`) shell out to an
  external prettifier instead, and require that `-file-line-error` is *not* passed.
- **Message classes it distinguishes** — errors: `! Emergency stop.`, `! LaTeX Error: …`,
  `!pdfTeX error: …`, `file:line: message` (both plain and `==>` variants), `Runaway argument?`.
  Continuation/context lines that belong to the preceding error: `<argument> …` (the offending
  argument for an undefined control sequence) and `l.NNN …` (the input line echo), so an error entry
  carries its context instead of appearing as three unrelated entries. Warnings: `LaTeX Font
  Warning`, generic `LaTeX … Warning` (with or without `on input line N`), `Overfull \hbox/\vbox`,
  `Underfull \hbox/\vbox` (single-line and `at lines N--M` forms), `Missing character:`, and
  package warnings — a generic `Package X Warning: … on input line N` rule plus special-cased
  continuation handling for `natbib`, `biblatex`, `babel`, `hyperref`, `scrreprt`, `fixltx2e`,
  `titlesec`, `silence` (each of which wraps its message across `(pkgname)`-prefixed lines).
  Everything unmatched is explicitly discarded.
- **Line numbers and files are recovered, then repaired** — the format pushes and pops files from a
  stack (`**file` entries) so an entry from an `\input`ed file is attributed to that file; a
  post-processing pass (`fix_paths`) re-reads the log to attach paths to `Overfull`/`Underfull`
  warnings (which don't carry a filename) and to fix `file:line` entries whose filename doesn't
  resolve, caching the mapping. Bibliography logs (`.blg`, from bibtex/biber) are parsed by separate
  backends and can be disabled (`g:vimtex_quickfix_blgparser`).
- **Filtering happens at two levels** — `g:vimtex_quickfix_ignore_filters` is a list of regexes that
  drop matching entries regardless of parse method (the documented example silences "Marginpar on
  page"); `g:vimtex_log_ignore` filters vimtex's own info/warning/error messages from the screen
  while still keeping them in `:VimtexLog`.
- **Presentation policy is separate from parsing** — `g:vimtex_quickfix_mode` (0 never open,
  1 open and focus, 2 open without stealing focus — the default),
  `g:vimtex_quickfix_open_on_warning` (open for warnings-only results), `g:vimtex_quickfix_autojump`
  (jump to the first error on open; explicitly discouraged with continuous compilation), and
  `g:vimtex_quickfix_autoclose_after_keystrokes` (close the list after N cursor movements, so an
  auto-opened diagnostics pane gets out of the way as soon as you resume typing). Navigation is
  Vim's quickfix (`:cn`/`:cp`, `:cwindow`), with `:VimtexErrors` / `<localleader>le` as a toggle,
  and the list is only replaced (not appended to) when it is already a vimtex-owned list.
- **Compile a fragment to isolate its errors** — `:VimtexCompileSelected` (`<localleader>lL`, also an
  operator and a visual mapping, always linewise) copies the selected lines into a temporary
  document with the same preamble, compiles it single-shot, and reports errors for *that fragment*
  in the diagnostics list. The preamble/wrapper can be overridden with a `vimtex-template.tex` (or
  `<name>-vimtex-template.tex`) containing the exact line `%%% VIMTEX PLACEHOLDER`. This is the
  closest thing in vimtex to cwiki's "one TeX run per changed block, errors shown in place of the
  formula", and the template mechanism is essentially cwiki's vault-wide-preamble idea.
- **A separate message log** — `:VimtexLog` opens a scratch buffer of vimtex's own messages with
  timestamps and the code location that raised them, closed with `q`/`<esc>`; message severity has
  dedicated highlight groups (`VimtexMsg`, `VimtexInfo`, `VimtexWarning`, `VimtexError`,
  `VimtexFatal`, `VimtexSuccess`).
- **Linting is a second diagnostics source** — `lacheck`, `chktex` and `biber` are wired up as Vim
  `:compiler`s, so `:lmake` fills the location list; `g:vimtex_lint_chktex_ignore_warnings`
  (default `-n1 -n3 -n8 -n25 -n36`) and `g:vimtex_lint_chktex_parameters` (auto-pointing at
  `$XDG_CONFIG_HOME/chktexrc`) tune it. Parsing is synchronous unless an async-runner plugin is
  used — the docs say so outright.

## Forward and inverse search (source ↔ rendered position mapping)

- **The mapping medium is SyncTeX, produced by the engine** — vimtex passes `-synctex=1` (or
  `--synctex` for tectonic) so the TeX run emits a `.synctex.gz` alongside the PDF. The mapping is
  therefore a *compilation artefact*: no SyncTeX file, no position mapping, and a stale SyncTeX file
  maps to stale positions. cwiki's render pipeline gets the same choice: either emit and consume
  SyncTeX per block, or derive its own source↔image mapping from the fact that it already knows which
  source block produced which image.
- **Forward search: source position → rendered position** — `:VimtexView` (`<localleader>lv`) sends
  the current file and line (and, where the viewer accepts it, the column) to the viewer, which
  scrolls to and usually highlights the corresponding place. The granularity is a *line*, sometimes
  a line+column: SyncTeX records boxes, and the resolution is "which typeset box came from this
  input line", so anything finer than a line is best-effort. Where the mapping is ambiguous or
  missing (a line that produced no box — a comment, a macro definition, a blank line, a line inside
  the preamble), the result is the nearest enclosing or following box, i.e. the viewer lands
  "near", not "exactly". Options exist to soften the visual result of that imprecision: Skim's
  `reading_bar` (highlight the whole line rather than a selection) and `no_select` (don't select the
  text at all).
- **Inverse search: rendered position → source position** — the viewer resolves a click to
  `(line, file)` (optionally `line:col`) and then *executes a shell command* to tell the editor:
  `:VimtexInverseSearch {line} {file}`, wrapping `vimtex#view#inverse_search(line, file)`. In a
  multi-file project the target file may not even be open, so `g:vimtex_view_reverse_search_edit_cmd`
  decides whether it is opened in the current window, a split or a new tab. After jumping, the
  `VimtexEventViewReverse` event fires so the user can flash the cursor line
  (`vimtex#ui#blink()`) — an explicit acknowledgement that an unannounced cursor jump is
  disorienting.
- **The editor and viewer are separate processes kept in step by messages** — Vim must be running a
  server (`+clientserver`, `--servername`, `remote_startserver()`); neovim uses its RPC socket
  instead. `vimtex#view#inverse_search` exists specifically so the viewer doesn't have to know which
  of several running editor instances owns the file: the command is delivered to a headless
  instance, which routes it to the right live instance. cwiki has no such split — the "viewer" is
  its own rendered-mode window in the same process — so the entire transport half of this feature
  disappears and only the *mapping* half matters.
- **Keeping in step across recompiles** — in continuous mode the viewer is (re)synced on every
  successful compile: `g:vimtex_view_automatic` opens the viewer on the first build,
  `g:vimtex_view_forward_search_on_start` decides whether that first open also jumps to the cursor,
  and per-viewer `*_sync` options decide whether each later successful compile re-issues a forward
  search. Because the PDF and SyncTeX file are rewritten in place during compilation, a viewer can
  read a half-written file; `g:vimtex_view_use_temp_files` copies both to `_`-prefixed temporaries
  after a successful run and points the viewer at those. That is exactly the atomic-swap discipline
  cwiki's render cache needs: never let the display read the artefact being written.
- **Degradation is expected and detected** — vimtex checks at startup whether zathura was built with
  libsynctex and warns if not; per-viewer switches (`..._use_synctex`) turn the mapping off when it
  is known broken. The general lesson: position mapping is an optional capability of the render
  path, and the UI must work (scroll, but not jump) when it is unavailable.

## Completion

Completion is Vim omni-completion (`'omnifunc'`, `i_CTRL-X_CTRL-O`); vimtex deliberately does not
implement an autocomplete engine and documents how to hook into eight external ones instead.
Candidate filtering respects `g:vimtex_complete_ignore_case` and `g:vimtex_complete_smart_case`
(defaulting to the user's `'ignorecase'`/`'smartcase'`), and `g:vimtex_complete_close_braces` can
append the closing `}`.

- **Labels (`\ref{`, `\eqref{`)** — candidates come from the project's `.aux` files, which means
  **label completion only works after a successful compilation**. The completion base is matched as
  a regex against, in order, the menu text (the label's value and page number), the label itself,
  and "menu + label" joined by whitespace — so `\ref{eq 2` matches label containing `eq` on page/
  number `2`. `\eqref` candidates are filtered to equation labels only.
  `g:vimtex_complete_ref.custom_patterns` adds trigger patterns for user-defined macros
  (`\figref{…`).
- **Citations (`\cite{` and ~15 related commands)** — parsed from `.bib` files included via
  `\bibliography`/`\addbibresource`/`\addglobalbib`… (the recognised command list is itself a
  configurable regex list, `g:vimtex_bibliography_commands`) and from inline `thebibliography`
  environments. "Smart" mode builds a *match string* per entry from a format template
  (`'@key [@type] @author_all @year, "@title"'`) and matches the user's input against it as a
  regex, so `\cite{Don.*Knuth` and `\cite{algo` both find `knuth1981`; `simple` mode matches only
  the key (recommended when an autocomplete plugin is driving). Separate format strings control the
  menu, info and abbr columns, and author lists are truncated to `auth_len` (20). Five interchangeable
  bib parser backends exist (`bibtex`, pure `vim`, `lua`, `bibparse`, `bibtexparser`) with explicit
  speed/robustness trade-offs — a candid admission that parsing BibTeX correctly is not cheap.
- **Commands (after `\`) and environments (after `\begin{`/`\end{`)** — candidates come from the
  detected package list (one data file per supported package under `autoload/vimtex/complete/`)
  *plus* the project's own preamble, scanned for `\newcommand`, `\let`, `\def` and
  `\newenvironment`. So the candidate set is document-specific, not a fixed table.
- **File names** — `\includegraphics{` completes image files, `\input{`/`\include{`/`\includeonly{`
  and `\includestandalone{` complete `.tex` files, `\includepdf{` completes `.pdf` files.
- **Glossary entries** — `\gls{`, `\glspl{` and variants, from the `glossaries` package's entries.
- **Package, class, bibstyle and beamer theme names** — gathered from the TeX distribution itself by
  reading the `ls-R` filename databases (`kpsewhich --all ls-R`) plus `TEXMFHOME`, filtered by
  filename prefix (`beamertheme*`, `beamercolortheme*`, …).
- **Caching** — completion data is cached under `g:vimtex_cache_root`
  (`$XDG_CACHE_HOME/vimtex`), persistently by default (`g:vimtex_cache_persistent`), with
  `:VimtexClearCache {name}` / `ALL` to invalidate. The docs concede texlab (a Rust LSP) is faster
  and that caching is what keeps the Vimscript implementation usable.

## Multi-file projects, main-file and package detection

- **Seven-step main-file resolution, in priority order** — (1) `b:vimtex_main` buffer variable,
  (2) a `%! TeX root = …` directive in the first 20 lines (case- and space-insensitive, `=` or `:`,
  globbing allowed — multiple matches prompt), (3) the `subfiles` package's
  `\documentclass[../main.tex]{subfiles}` header, (4) an empty marker file
  `main.tex.latexmain`, (5) `@default_files` in a local `latexmkrc`, (6) an upward directory scan
  for a `.tex` file that (a) includes the present file directly or indirectly, (b) has
  `\documentclass` near the top of its *expanded* content and (c) contains `\begin{document}`,
  (7) failing all that, whether the file is listed as a source of an already-open project. The
  documented limits are honest: the scan never descends into sibling directories, and method 7 makes
  the answer depend on the order in which files were opened.
- **Toggling scope** — `:VimtexToggleMain` (`<localleader>ls`) switches between treating the project
  main file and the current file as "the document", so a single chapter can be compiled on its own;
  `g:vimtex_subfile_start_local` makes subfiles start local. Directly analogous to cwiki's
  per-note-versus-vault-preamble question.
- **Package detection drives package-aware behaviour** — the required-package list is built either by
  scanning the `.fls` file (produced by `-recorder`, re-scanned after each successful compile) or,
  failing that, by parsing `\usepackage` from the preamble once at load (slower and less accurate,
  explicitly). That list decides which syntax add-ons load, which command-completion tables load,
  and which package `K` looks up documentation for. `g:vimtex_syntax_packages` can force a package
  add-on to always load (`load: 2`) or never (`0`) — a necessary escape hatch precisely because
  detection fails on documents that have never been compiled.
- **A TeX program directive** — `%! TeX program = lualatex` in the main file selects the engine,
  mapped through `g:vimtex_compiler_latexmk_engines`. Relevant to cwiki's still-open
  pdflatex-vs-lualatex question: a per-note override of the engine is an established convention.
- **File navigation** — vimtex sets `'include'`, `'includeexpr'`, `'suffixesadd'` and `'define'` so
  Vim's own `gf`, `include-search` (`[i`, `:isearch`) and `definition-search` work across the
  project; `includeexpr` resolves package and class names through `kpsewhich`, so `gf` on
  `\usepackage{mypkg}` opens `mypkg.sty` from the TeX tree. `g:vimtex_include_indicators` (default
  `['input', 'include']`) extends which commands count as includes;
  `g:vimtex_include_search_enabled` exists to switch off the `kpsewhich` scanning because it can
  introduce a "significant delay" on first use.

## Indentation and formatting

- **A custom `indentexpr`, LaTeX-aware, all knobs exposed** — `g:vimtex_indent_delims` lists the
  opening/closing delimiters that add and remove an indent level (default `{` / `}`), whether the
  closing line itself stays indented (`close_indented`), and whether modified math delimiters
  (`\left(`/`\right)`) count (`include_modified_math`). The docs state a deliberate limitation: there
  is *no* context-aware delimiter indenting (no "parentheses indent only in math mode").
- **Environment and list rules** — `g:vimtex_indent_ignored_envs` (default `['document']`) lists
  environments that don't change indentation; `g:vimtex_indent_lists` (`itemize`, `description`,
  `enumerate`, `thebibliography`) marks the environments where `\item` continuation lines are
  indented under the item — and the same list is reused by the `items` fold type.
- **Alignment on ampersands** — leading `&` in `align`/`tabular` rows are aligned by default
  (`g:vimtex_indent_on_ampersands`), with a pointer to `vim-easy-align` for anything richer.
- **TikZ multi-line commands** — `g:vimtex_indent_tikz_commands` specifically handles continuation
  indentation of multi-line commands inside `tikzpicture`. Relevant to cwiki's TikZ-authoring
  priority: vimtex treats TikZ as a case needing its own indent rule.
- **`formatexpr` for `gq`** — off by default (`g:vimtex_format_enabled`); when on, `gq` will not join
  an end-of-line comment into prose and will not reflow across environment boundaries. The region
  borders are regexes (`g:vimtex_format_border_begin`/`_end`). The docs argue hard *against* hard
  wrapping LaTeX at all (it wrecks diffs) and recommend one-sentence-per-line with soft wrap —
  which matches cwiki's own "soft wrap for prose" decision.
- **Bib files** — indentation and folding only, plus `'comments'`/`'commentstring'`; vimtex
  explicitly is not a full bib filetype plugin.

## Other substantial features

- **`:VimtexInfo` / `:VimtexInfo!` (`<localleader>li`/`lI`)** — dumps the plugin's whole per-project
  state (main file, root, detected packages, compiler status, source list) for one or all projects.
  A debugging affordance worth copying: one command that prints what the program thinks is true
  about the current document.
- **Word and letter count** — `:VimtexCountWords` / `:VimtexCountLetters` shell out to `texcount`
  over the project (or a selected range; `!` variants report per included file), and
  `vimtex#misc#wordcount(opts)` exposes the number for use in a statusline. Counting words in LaTeX
  correctly means excluding markup, which is why an external tool is used.
- **Documentation lookup (`K`)** — `:VimtexDocPackage` resolves the command or package under the
  cursor to one or more candidate packages and opens their documentation, online via texdoc.org by
  default or locally via `texdoc` by swapping the handler. Handlers are a user-extensible chain of
  functions, each of which may modify the context or handle it.
- **Context menu on citations (`<localleader>la`)** — parses the bib entry under the cursor and offers
  only the actions its metadata supports: jump to the entry in the `.bib` file, show the entry, open
  the `file:` PDF, open the `doi:`, open the `url:`. A precedent for a context-sensitive action menu
  keyed on what the thing under the cursor actually is.
- **Events as extension points** — `User` autocommands (`VimtexEventInitPost`, `VimtexEventCompiling`,
  `VimtexEventCompileSuccess`/`Failed`, `VimtexEventCompileStarted`/`Stopped`, `VimtexEventView`,
  `VimtexEventViewReverse`, `VimtexEventQuit`, …) let users hook the lifecycle without patching the
  plugin.
- **A documented public API** — `vimtex#env#get_inner`/`get_outer`/`get_all` (returning
  `{name, open, close}` with full delimiter objects), `vimtex#env#is_inside(name)`,
  `vimtex#syntax#in`/`in_mathzone`, `vimtex#cite#get_key`/`get_entry`, `vimtex#view#inverse_search`.
  Worth noting as a design decision: the structural queries are a stable API, and the mappings are
  thin wrappers over them. cwiki's equivalent would be a small internal "what is at the cursor"
  interface that snippets, motions, rendering and the ToC all call.
- **Cache and state model** — one state dictionary per *project* (not per buffer), built by
  `vimtex#init()` and shared across the buffers of a multi-file document; caches under the XDG cache
  root with a persistence switch and an explicit clear command.

## Configuration surface

- **Everything is feature-gated** — `g:vimtex_{compiler,complete,doc,fold,imaps,indent,matchparen,
  motion,quickfix,syntax,text_obj,toc,view}_enabled`, plus `g:vimtex_enabled` to switch off the
  whole plugin. Essentially every subsystem described above can be turned off independently, which
  is what makes the plugin survivable in a heavily customised setup.
- **Mappings are indirect and individually overridable** — every default mapping is a thin binding to
  a `<plug>(vimtex-…)` map, and a default is *only* created if that left-hand side is not already
  taken (`g:vimtex_mappings_override_existing` reverses that). `g:vimtex_mappings_enabled` disables
  all defaults, `g:vimtex_mappings_disable` disables named mappings per mode, and
  `g:vimtex_mappings_prefix` relocates the whole `<localleader>l…` family. Nearly every mapping also
  exists as an `:Ex` command.
- **Behaviour tables rather than booleans** — the recurring pattern is that a "toggle" is a
  user-editable map or list: `g:vimtex_env_toggle_map`, `g:vimtex_env_toggle_math_map`,
  `g:vimtex_delim_toggle_mod_list`, `g:vimtex_toggle_fractions`, `g:vimtex_toggle_star_cmds`,
  `g:vimtex_delim_list`, `g:vimtex_fold_types`, `g:vimtex_toc_config(_matchers)`,
  `g:vimtex_syntax_conceal`, `g:vimtex_syntax_packages`, `g:vimtex_imaps_list`. Most accept Vim
  "very magic" regexes, and several accept function references so the rule itself can be code.
- **UI backends are pluggable** — `g:vimtex_ui_method` selects how confirm/input/select dialogues are
  drawn (native popup vs. `echo`+`input()` legacy), i.e. the prompts are routed through one
  abstraction rather than hard-coded per feature.
- **Performance knobs are first-class** — `g:vimtex_delim_timeout` / `_insert_timeout` /
  `_stopline`, `g:vimtex_fold_manual`, `sections.parse_levels`, `toc.refresh_always`,
  `g:vimtex_include_search_enabled`, `g:vimtex_matchparen_enabled`. The plugin's own answer to "this
  is slow" is a documented bound on how far and how long a search may run — a reasonable pattern for
  a C implementation too, and a warning that these searches are expensive even in a fast language.

## Vim machinery vimtex leans on that cwiki would have to build

- **`syntax`/`synstack()` and syntax-group names** — the substrate for `vimtex#syntax#in*`, `i$`/`a$`,
  the math motions, conceal, matchparen and the snippet context gates. cwiki needs an incremental
  per-line tokeniser that keeps an open-zone stack and can answer "what zones are active at
  (line, col), innermost first" cheaply enough to run on every keystroke.
- **`conceallevel` / `concealcursor` / `cchar`** — the entire display-substitution mechanism,
  including the cursor-line suspension policy and the source-column ↔ screen-column mapping that
  makes motions on a concealed line behave.
- **`foldmethod=expr` / `foldexpr` / `foldtext` / `fillchars`** — fold level computation, fold text
  rendering, per-window fold state, and the manual-fold fast path.
- **`quickfix` / `errorformat` / `caddfile` / location lists** — the multi-line stateful log parser,
  the diagnostics list data structure, its window, and `:cn`/`:cp` navigation.
- **`searchpos()` with `stopline`/`timeout` flags and the `p` "which alternative matched" flag** —
  the bounded-search primitive nearly every structural query is built from.
- **`'include'` / `'includeexpr'` / `'suffixesadd'` / `'define'` / `gf` / `[i` / tags** — cross-file
  navigation.
- **`indentexpr` / `formatexpr` / `==` / `gq`** — indentation and reflow.
- **Operator-pending and visual mode mapping modes (`onoremap`/`xnoremap`), `v:count1`, `v:operator`,
  `'selection'`, dot-repeat** — every text object and manipulation mapping is expressed in terms of
  these; the objects themselves are only ~500 lines because the operator grammar already exists.
- **`+clientserver` / RPC, `--remote`, `v:servername`** — inverse search transport. This is the one
  item cwiki does not need at all: its rendered view is in-process.
