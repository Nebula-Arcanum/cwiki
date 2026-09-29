# cwiki — specification

Decisions from the completed product interview, stated as requirements. Each
section records the alternatives that were rejected and why, so later work does
not relitigate them.

Status: **approved for implementation on 2026-09-28.** The remaining
[Open questions](#open-questions) are explicitly deferred to their owning
milestones and do not block implementation.

---

## 0. Additional influence: vimtex

**vimtex.** The user edits LaTeX with
[vimtex](https://github.com/lervag/vimtex) and wants cwiki's LaTeX experience
to be close to it. The initial inventory missed it. It is now inventoried in
`../research/vimtex.md`, and `FEATURES.md` has gained 68 vimtex rows (section
"Editor — vimtex LaTeX editing") plus 11 existing rows amended to add `vimtex`
as a source. The completed interview decided those rows.

The architecture-grade rows among them are folded into §1 rather than left to the
end-of-interview `c`/`?` pass, since the same context detection serves conceal,
snippet gating and source highlighting:

| Row | Landed as |
|---|---|
| Zone stack, one "innermost first" query | R1.9.1, R1.9.2 (a region table evaluated per line *is* a per-line incremental tokeniser whose rules are patterns) |
| Innermost-zone-wins with registered holes | R1.9.5, extended with `\label{}`/`\ref{}` arguments as holes |
| Escape- and comment-aware position idiom | **R1.9.6a** (new) — and the reason the fast structural searches need no zone lookup |
| In-note "stop parsing here" directive | **R1.9.6b** (new) |
| User-declarable zones, language from the block's own argument | R1.9.6, extended |
| Bounded structural search as a shared primitive | R1.9.4, extended — reports which alternative matched |
| Per-expansion context wrappers, incl. different result per environment | **R1.10.10a** (new) |
| Leader symbol expansions (~70 entries) | **R1.10.10b** (new) — they become default snippets, not a second mechanism |
| List expansions active in this context | **R1.10.10c** (new) |
| Rendered→source jump with a visual cue | **R1.8.17** (new) — cue is in the editing window, so no conflict with R1.8.9 |
| Mapping as an optional capability, degrade to scrolling | **R1.8.18** (new) |
| Never display a half-written artefact | **R1.8.19** (new) |
| Named actions behind an indirection; per-subsystem switches | **R1.14.1, R1.14.3** (new) |
| Behaviour as user-editable tables, not booleans | **R1.14.2** (new) |
| On-demand recomputation, explicit refresh policy | **R1.14.4** (new) |
| Internal message log; state dump | **R1.14.5, R1.14.6** (new) |

The follow-up interview resolved the dependencies this inventory exposed:
command-object greediness is fixed by R1.15.2; comment syntax and motions by
§1.9; default snippets by R9.4.1; structured TeX diagnostics by §6.5; outline
and label indexing by §4; per-note preambles by §2.8; and the extension boundary
by §7. R1.10.7's closed transform set remains compatible with R1.14.2 because
transforms are reviewed code while user behavior is declarative data.

Package availability was revalidated against the official Arch package index,
Homebrew formula/cask index, and FreeBSD ports/packages on **2026-09-28**:

| Dependency | FreeBSD package | Arch package | Homebrew package |
|---|---|---|---|
| kitty | `kitty` (`x11/kitty`) | `extra/kitty` | cask `kitty` |
| utf8proc | `utf8proc` (`textproc/utf8proc`) | `extra/libutf8proc` | `utf8proc` |
| PCRE2 | `pcre2` (`devel/pcre2`) | `core/pcre2` | `pcre2` |
| SQLite | `sqlite3` (`databases/sqlite3`) | `core/sqlite` | `sqlite` |
| Poppler tools | `poppler-utils` (`graphics/poppler-utils`) | `extra/poppler` | `poppler` |
| libyaml | `libyaml` (`textproc/libyaml`) | `extra/libyaml` | `libyaml` |
| LuaLaTeX / TeX Live | `texlive-full` | official `texlive-*` packages | cask `mactex-no-gui` |

No dependency requires the AUR, a non-core Homebrew tap, or building a FreeBSD
port from source. Revalidate this matrix before implementation first introduces
each dependency; package names and versions are not permanent architecture.

What the inventory changed or confirmed in decisions already committed:

| Decision | Effect |
|---|---|
| R1.1.3 per-line cached parse state | **Changed.** vimtex's context API is a query against a *stack* of syntax groups, not a scalar, with innermost-zone-wins and registered "holes" — `\text{}` inside math returns to text context, and a math zone can nest inside an environment inside a command argument. A scalar end-of-line state cannot express that. R1.1.3 now specifies a zone stack. |
| R1.3.6 grapheme-cluster motion | **Extended**, see R1.6.9. Conceal breaks the source-column ↔ screen-column identity, so motion consults a mapping. The inventory names this as the row most likely to be underestimated as "just a display transform". |
| R1.4.1 own Markdown+LaTeX parser is the highlighter | **Confirmed and widened.** vimtex gets zone predicates, conceal categories, fold levels and delimiter matching from Vim's syntax machinery for free; cwiki's parser must supply all four. The inventory notes vimtex proves the dependency negatively: with tree-sitter instead of Vim syntax, `i$`/`a$` and the math motions stop working outright. |
| SyncTeX granularity (R1.8.9) | **Bounded.** SyncTeX records boxes, so character-level source↔render mapping is not available at any price. Line granularity within a block is the ceiling, which is exactly what the sync marker needs, but no UI may promise more. |
| TeX diagnostics (§6.5) | TeX log parsing is an **L-sized subsystem**, not a detail of the render pipeline: the decided "LaTeX errors shown in place of the formula" needs a stateful multi-line parse producing line numbers, `l.NNN` context lines and per-included-file attribution, which Vim hands vimtex for free as `errorformat`. |
| Snippet context (§1.10) | vimtex's `in_mathzone()` predicate family is exactly the gate snippet expansion needs — one mechanism, not two. Note also that vimtex deliberately *removed* snippets in favour of context-aware insert-mode maps, which remains relevant prior art for implementation. |

Two things the inventory flags as not achievable exactly, by construction, both
to be handled by choosing and documenting one rule rather than chasing
correctness:

- **Command-argument boundaries** (`ic`/`ac`, command conceal): impossible
  without macro expansion, since `\foo{bar}{baz}` cannot be split without
  knowing `\foo`'s arity. vimtex exposes a heuristic hook and documents the
  limit.
- **Sub-line render mapping**, as above.

Terminology: **vault** = the directory of notes, synced by git. **note** = one
Markdown file in the vault. **buffer** = a note loaded into memory for
editing. **window** = one viewport onto a buffer.

---

## 1. Editor internals

### 1.1 Text buffer

**R1.1.1** A buffer is an array of lines. The array holds line records; each
line record owns a growable byte array holding that line's UTF-8 bytes with no
terminator, plus cached derived state.

```c
struct Line {
    char        *bytes;      /* UTF-8, no NUL, no newline */
    size_t       len, cap;
    ZoneStack    end_zones;  /* zone stack at end of line (R1.1.3) */
    bool         dirty;      /* zone state needs recompute */
};   /* row counts are per-window, not here — see R1.12.3 */

struct Buffer {
    Line   *lines;
    size_t  n, cap;
    /* ... */
};
```

**R1.1.2** Line endings are normalised to LF in memory. A file read with CRLF
endings is recorded as such on the buffer and written back with CRLF, so cwiki
never rewrites line endings it did not author. Files cwiki creates use LF. A
file mixing LF and CRLF is rejected with a clear error rather than silently
normalised; exact mixed-ending preservation would require per-line metadata
outside the approved buffer-level line-ending model.

**R1.1.3** Each line caches the parser state in effect at its end (soft-wrap row
counts are per window, R1.12.3). That state is a **zone stack**, not a scalar:
zones nest, so
`\text{…}` inside display math inside a `figure` environment is three entries
deep and the innermost one wins. An edit to line N marks N dirty; recomputation
proceeds downward from N and stops at the first line whose recomputed end stack
equals its previous value. The stack is depth-bounded; text past the bound is
treated as belonging to the outermost zone still on the stack rather than
failing.

```c
typedef struct { ZoneKind kind; uint16_t detail; } Zone;  /* detail: fence lang, env id */
typedef struct { Zone z[CWIKI_ZONE_MAX]; uint8_t depth; } ZoneStack;
```

**R1.1.4** Positions inside a buffer (cursor, marks, snippet tab stops,
selection anchors, per-window scroll anchors) are `(line index, byte offset)`
pairs. Every edit fixes up all live positions: an insertion at `(L, O)` shifts
offsets on line `L` at or after `O`; a line split or join shifts line indices
above it.

**R1.1.5** Degradation cap: a single line longer than 1 MiB is not soft-wrapped
and not syntax-highlighted; it renders as raw truncated text. No other size
limit applies.

Rejected:

- **Gap buffer.** Needs a separately maintained `line_starts[]` index for line
  numbers, wrapping and rendering — so the O(lines) index patch per edit
  returns anyway, plus gap-crossing logic in every read path, plus per-line
  cached state has no natural home and becomes hand-maintained parallel arrays.
- **Piece table.** Fragments heavily under sustained typing, and cwiki's reads
  are constant (per-keystroke context parse, soft-wrap re-measure, backward
  scan for snippet triggers) so every read becomes a piece walk. A grapheme
  cluster can straddle a piece boundary, forcing a piece-crossing byte iterator
  into the segmenter. Its one advantage, cheap undo, is worth little given
  R1.2.
- **Rope.** Most code and most subtle bugs, and it only pays off at file sizes
  class notes never reach. Same cluster-straddling problem as a piece table.

### 1.2 Undo

**R1.2.1** Undo is a tree, not a stack. Undoing and then editing creates a new
branch rather than discarding the previous redo path. All states stay
reachable.

**R1.2.2** Required undo commands: `u` (undo), `<C-r>` (redo), `g-` / `g+`
(walk all states in chronological order across branches), and a command listing
the branch points with their timestamps.

**R1.2.3** One undo step is one insert-mode session (everything between
entering and leaving insert mode) or one normal-mode change (one operator
application, one `x`, one `p`, one `:s`, one snippet expansion). A snippet
expansion together with the text typed into its tab stops is **not** collapsed:
the expansion is one step and each tab-stop fill is part of the insert session
it belongs to.

**R1.2.4** Undo steps are never split by elapsed time. Granularity must be a
pure function of the keystrokes, so that a recorded key sequence replays to an
identical undo tree.

**R1.2.5** Persistent undo. The undo tree is written to the OS state directory,
never into the vault and never into git:

| Platform | Location |
|---|---|
| Linux, FreeBSD | `$XDG_STATE_HOME/cwiki/undo/`, default `~/.local/state/cwiki/undo/` |
| macOS | `~/Library/Application Support/cwiki/undo/` |

**R1.2.6** An undo file is named by a hash of the note's vault-relative path.
Its header records a hash of the note content the tree applies to. On opening a
note, cwiki loads the undo tree only if the stored content hash matches the
file on disk; on any mismatch the tree is discarded and the note opens with
empty undo history. A stale tree is never replayed onto changed content.

**R1.2.7** When an external change replaces a clean buffer's content, cwiki
starts a new active undo tree at that content. The old tree is archived as
recovery metadata but is not reachable through normal undo and is never
replayed automatically. Replaying recorded offsets against text they were not
recorded against is the one failure mode that can silently corrupt a note.

Rejected:

- **Linear undo stack.** Undoing too far and typing one character destroys work
  irrecoverably, and the on-disk format cannot grow into a tree without a
  migration.
- **Tree in memory, trunk on disk.** Branches vanish on reload, which is more
  confusing than either consistent option.
- **Time-based step splitting.** Makes undo granularity depend on typing speed,
  so the same keystrokes produce different results on different days, and makes
  replay tests untestable without faking the clock.
- **Cache directory for undo files.** Cache is disposable by definition —
  cleanup tools and habit delete it. Undo history is state. (The TeX render
  cache does belong in the cache directory; see the rendering topic.)
- **Keeping a stale tree with a warning.** The safety gain over discarding is
  small and the failure is invisible.

### 1.3 Unicode

**R1.3.1** cwiki depends on **utf8proc** (MIT, ≥ 2.11) for UAX #29 extended
grapheme cluster segmentation, NFC/NFD normalisation and case folding.
Packaging is confirmed on all three targets, binary packages in every case
(revalidated 2026-09-28):

| Platform | Package |
|---|---|
| FreeBSD | `pkg install utf8proc` (`textproc/utf8proc`) |
| Arch | `extra/libutf8proc` |
| macOS | `brew install utf8proc` (homebrew/core) |

**R1.3.2** Display width is computed by cwiki, not by `utf8proc_charwidth`,
which is per-codepoint and does not match kitty's emoji handling. The rule,
applied per grapheme cluster:

- East Asian Wide or Fullwidth base → 2 cells
- base with Emoji_Presentation, or followed by U+FE0F → 2 cells
- combining marks, zero-width and default-ignorable characters → 0 cells
- everything else → 1 cell
- cluster width = width of its base character; marks add nothing

**R1.3.3** cwiki targets **Unicode 17.0** through utf8proc's tables. This pin
must be reviewed when the minimum kitty version moves. A CI test prints a fixed
corpus of clusters (combining sequences, East Asian wide, emoji with and
without VS16, ZWJ sequences, flags, default-ignorables) to kitty and reads the
cursor column back via a cursor-position report, asserting the column matches
cwiki's prediction. Disagreement between kitty's Unicode tables and utf8proc's
fails the build rather than desynchronising a screen during a lecture.

**R1.3.4** cwiki never rewrites the bytes of note content. Normalisation is
applied only when constructing comparison keys — link targets, heading anchors,
index terms, search queries — which are NFC-folded before matching. An NFD
heading resolves from an NFC link and vice versa.

**R1.3.5** Filenames cwiki creates (new note, create-on-follow, attachments)
are always written in NFC. Existing NFD paths authored by other tools open and
resolve normally through the folded key.

**R1.3.6** Horizontal cursor motion and all character-wise editing operate on
grapheme clusters. `h`, `l`, `x`, `r` and the char-wise halves of operators
move and act on whole user-perceived characters. Byte offsets remain the
internal representation; clusters are the interaction unit.

Rejected:

- **libgrapheme.** AUR-only on Arch and ports-only on FreeBSD, which the brief
  requires be flagged, and it provides no normalisation — so cwiki would need
  utf8proc as well, or a hand-rolled NFC.
- **Hand-rolled Unicode tables generated from the UCD** (kitty's own approach).
  Zero runtime dependency, but cwiki would own UAX #29 segmentation,
  normalisation and width correctness plus every Unicode version bump
  permanently.
- **Learning widths by querying kitty at startup.** No protocol exists for
  asking about arbitrary strings, so it can only sample. Retained as the CI
  check in R1.3.3, rejected as the source of truth.
- **Normalising note content to NFC on save.** Rewrites lines the user never
  touched, producing surprise git diffs and noisy merges, and silently destroys
  deliberately-decomposed text.
- **Codepoint or byte motion units.** Codepoint motion puts the cursor in
  positions kitty cannot draw a cursor in, reintroducing exactly the desync
  R1.3.2 exists to prevent. Byte motion produces invalid UTF-8 on any delete
  inside a multi-byte sequence.

### 1.4 Consequence for syntax highlighting

**R1.4.1** cwiki implements its own Markdown + LaTeX parser, required in any
case for snippet context gating (§1.6), math delimiter detection and fenced
block recognition. That parser is also the source highlighter. cwiki does not
use tree-sitter and ships no per-language grammars.

Recheck of FEATURES.md note 5: tree-sitter appears four times in the reference
nvim config — as the engine behind *all* syntax highlighting
(`vim.treesitter.start`), for `mini.ai`'s `aF`/`iF` function text objects, for
`render-markdown.nvim`, and for `mini.bracketed`'s node motions (not bound).
Only the `aF`/`iF` case has no cwiki equivalent under R1.4.1, and it is the one
the user does not use. Structural text objects inside code fences would need
real per-language parsers and stay dropped.

### 1.5 Key input and the terminal layer

**R1.5.1** cwiki pushes kitty keyboard protocol enhancement flags
`1|4|8|16` = 29 at startup — disambiguate escape codes (1), report alternate
keys (4), report all keys as escape codes (8), report associated text (16) —
using the stack push form `CSI > 29 u`, and pops with `CSI < u` on exit. Flags
are pushed, never set absolutely, so cwiki cannot clobber a state another
program established.

**R1.5.2** Every keypress therefore arrives as a single `CSI … u` sequence
carrying key code, modifiers, the base-layout key and the text the key
produced. There is one input parser and no state machine deciding whether a
byte is text or part of a sequence. Normal mode dispatches on key code plus
modifiers; insert mode inserts the associated-text field, which is absent for
keys that produce no text.

**R1.5.3** Alternate-key reporting (flag 4) supplies the base-layout key, so
keymaps bind to physical keys and work unchanged on non-US layouts. This is
what satisfies the layout-aware-hotkey row in FEATURES.md.

**R1.5.4** Bracketed paste (`DECSET 2004`) is enabled. Pasted text arrives
literally between `CSI 200~` and `CSI 201~` as one event, not as one key event
per character, and is inserted without triggering snippet expansion, autopairs
or abbreviations.

**R1.5.5** The terminal is put in raw mode; cwiki handles `Ctrl+C` itself and
no key generates a signal from the tty.

**R1.5.6** The input loop drains all pending input before redrawing, and each
redraw is wrapped in a synchronized update (`DECSET 2026` … `DECRST 2026`). A
held motion key produces one redraw per drain, not one per event.

**R1.5.7** Startup capability detection queries the terminal rather than
reading `$TERM`: keyboard protocol flags via `CSI ? u`, graphics support via an
Application Program Command graphics query, then a primary device attributes
query (`CSI c`) as a synchronisation barrier. DA1 is answered by every
terminal, so its arrival without the preceding replies proves the capability is
absent, with no reliance on a timeout. A 2 s timeout remains as a backstop for
a terminal that answers nothing. Missing either capability is a clean exit with
a message naming what was missing and what cwiki needs.

**R1.5.8** Terminal restoration happens on every exit path, including a crash.
Handlers are installed for `SIGSEGV`, `SIGBUS`, `SIGILL`, `SIGFPE`, `SIGABRT`,
`SIGTERM`, `SIGHUP` and `SIGINT`, and do only async-signal-safe work:

1. one `write(2)` of a fixed byte string — end synchronized update, pop
   keyboard flags, disable bracketed paste, leave the alternate screen, show
   the cursor — in that order, since a pending synchronized update would
   otherwise suppress the rest;
2. `tcsetattr` restoring the saved termios;
3. flush the key recording ring buffer (R1.5.9) to a crash file;
4. restore the default disposition and re-raise, so a core dump is still
   produced.

`atexit` covers normal exits. `cwiki --reset-terminal` emits the same
restoration string alone, for the case where even the handler failed.

**R1.5.9** Key recording is always on, into a ring buffer, and records the raw
byte stream cwiki read from the terminal. Replay feeds those bytes to the real
input parser, so input-layer bugs reproduce and not only editor bugs. A
human-readable decoded sidecar is generated alongside (one key per line, with
timestamp, decode and paste blocks summarised rather than expanded) for the
user and for review agents. Because recording is unconditional, a crash always
carries the preceding key history even when recording was not requested — this
is the mechanism that turns "it crashed in class" into a failing test.

**R1.5.10** Minimum kitty version is **0.40.0**, the lowest release supporting
everything cwiki uses including OSC 66 text sizing, so no feature needs a
runtime capability gate. The floor is raised only when a feature cwiki actually
adopts requires it. Margin against packaged versions:

| Target | Packaged kitty |
|---|---|
| FreeBSD 13 quarterly | 0.46.2 |
| FreeBSD 14 / 15 quarterly | 0.47.4 |
| FreeBSD 14 / 15 latest | 0.48.2 |
| Arch `extra` | 0.48.2 |
| Homebrew cask | 0.49.0 |

The pin is a documentation and support boundary. cwiki still detects
capabilities at runtime per R1.5.7 and never infers them from a version string.

Rejected:

- **Flag `1` alone, or `1|4`.** Satisfies the brief's letter (Ctrl+i distinct
  from Tab, no Esc timeout) but leaves printable keys arriving as raw UTF-8
  bytes, so the input layer needs two paths and a state machine to choose
  between them.
- **Adding flag `2` (event types).** Key release events are useless to a modal
  editor and every one must be filtered; distinguishing auto-repeat is better
  done from input-queue depth. More traffic, more filtering, no capability
  gained.
- **Restoring the terminal without re-raising the signal.** No core dump, so a
  crash that the key log cannot reproduce leaves no evidence at all.
- **A shell wrapper that resets the terminal after cwiki returns.** Trivially
  bypassed by running the binary directly, under a debugger, or from another
  program, and the brief asks the program itself to be safe.
- **Recording decoded key events instead of bytes.** Makes escape-sequence
  parser bugs unreproducible from a recording, and the parser is the component
  most exposed to surprises.
- **Recording both but replaying the decoded stream.** The bytes would be
  stored and never exercised.
- **A 0.28.0 floor with OSC 66 gated at runtime.** Two heading render paths,
  one of them untested, to support versions no target ships.
- **A 0.46.2 floor (the oldest currently packaged).** Makes the floor track
  distro churn rather than cwiki's needs, and excludes users on a slightly
  older kitty for features cwiki does not use.

### 1.6 Conceal in editing mode

**R1.6.1** Editing mode has an optional conceal layer: LaTeX markup is replaced
on screen by a width-safe character denoting the same concept — for example,
`\alpha` as 𝛼 and `\le` as ⩽ — as a purely textual display substitution. When
the familiar glyph (such as α, ≤ or ∈) has ambiguous East Asian width and no
faithful non-ambiguous equivalent exists, the source remains visible. No image
and no TeX run is involved, so the brief's "no live preview" rule still holds:
rendered output exists only in rendered mode.

**R1.6.2** Conceal is a togglable option, **on by default**, per window
(R1.12.3). With it off, editing mode is pure source.

**R1.6.3** Conceal on the cursor's own line follows vim's `concealcursor`
model: a per-mode setting listing the modes in which the cursor line stays
concealed. Default `nc` — concealed in normal and command-line mode, real
source shown in insert and visual mode.

**R1.6.4** Consequence of R1.6.3, recorded because it is a real cost: since the
cursor line can be concealed while normal-mode motions run over it, the
source↔display mapping must be *invertible on the cursor line too*, not merely
display-only. A display column can cover a multi-byte source run, so cursor
placement, `|`, `0`, `$`, visual-block columns and wrap measurement all consult
the mapping rather than summing widths. (The rejected `concealcursor=""`
default would have confined the mapping to non-cursor lines and made it
one-directional.)

**R1.6.5** The row count of a line's *concealed* form is cached **per window**,
not per line in the buffer (see R1.12.3 — conceal level and width are
window-local, so the same line has different row counts in different windows).
The cursor line's unconcealed row count is recomputed when the cursor changes
line. With wrap on, a line therefore reflows as the cursor arrives and leaves in
the modes where R1.6.3 suspends conceal — the same visible behaviour as vim.

**R1.6.6** Concealed replacement characters must have unambiguous East Asian
width. Characters with width class Ambiguous are excluded from the replacement
set, since cwiki and kitty could disagree about their column count and
desynchronise the screen (R1.3.2). This rule takes precedence over using the
most familiar glyph: use a faithful non-ambiguous equivalent when one exists,
otherwise do not conceal that source. This precedence was accepted on
2026-09-29. A curated width-1 exception list was rejected because it recreates
the cursor-desynchronisation risk; leaving every ambiguous form visible even
when a faithful safe equivalent exists was rejected as unnecessarily reducing
conceal coverage.

**R1.6.7** Conceal applies only where Unicode has the target form. `x^2`
stays as source because `²` has ambiguous width, while `x^5` may conceal to
the width-safe `x⁵`; `x^{2n}` has no whole superscript form and stays as source.

**R1.6.8** Conceal categories are individually configurable with cwiki's
14-category table: accents, Greek, math symbols, ligatures, fractions, math
bounds, size-modified delimiters, sub/superscripts, styles, environments,
`\item` markers, citations, spacing and sections. By default, accents, Greek,
math symbols, ligatures, fractions, size-modified delimiters and
sub/superscripts are enabled. Structural or invisible substitutions — math
bounds, styles, environments, `\item` markers, citations, spacing and sections
— are opt-in. This deliberately follows vimtex's granularity as a design
pattern rather than copying its current keys: current vimtex has no independent
environment category, groups item markers under `fancy`, and includes a tabular
row-marker category instead.

**R1.6.9** Horizontal motion reveals a concealed run on approach. Moving into a
concealed run unconceals **that run only**, leaving the rest of the line
concealed; motion inside the revealed run is per grapheme cluster over real
source, per R1.3.6. Leaving the run re-conceals it. Consequence, accepted: text
to the right of the revealed run shifts as the cursor enters and leaves it, and
with wrap on the line's row count can change mid-motion.

Rejected:

- **`concealcursor=""` (vim's own default, which vimtex leaves alone).** Would
  have kept the inverse mapping out of the editing path entirely, at the cost
  of the cursor line visibly expanding on arrival in every mode.
- **Concealing the cursor line unconditionally.** Routinely means editing text
  that is not on screen — deleting inside a `\frac` shown as a fraction glyph,
  or fixing a macro name that is concealed away.
- **Compiled-image previews inline in editing mode.** Contradicts the brief's
  separation of editing and rendered modes.
- **Current vimtex category keys and defaults.** They conceal nearly everything
  except sections, including structural math bounds, styles, spacing and
  citations; they also lack the independently configurable environment category
  selected for cwiki.
- **The broad cwiki profile (everything except citations and sections).** Hides
  structural and invisible source by default, making editing harder to audit.
- **The minimal math profile (Greek, symbols, fractions and sub/superscripts
  only).** Leaves useful, unambiguous accents, ligatures and size-modified
  delimiter substitutions disabled despite conceal being on by default.
- **Treating a whole concealed run as one motion step** (better than vim, since
  the screen cursor always visibly moves). Rejected in favour of R1.6.9, which
  keeps source-accurate motion counts inside the run.
- **Vim's own behaviour: per-cluster motion with the screen cursor parked on
  the replacement glyph.** Motion counts stop matching the screen, which is the
  specific long-standing complaint about conceal in vim.

### 1.7 Soft wrap

**R1.7.1** Soft wrap is on by default in editing mode, breaking at word
boundaries. Continuation rows are indented to line up under the start of the
wrapped line, and carry a marker in the left margin identifying them as
continuations.

**R1.7.2** Wrap is togglable per window, under the existing option-toggle
namespace (`\w`).

**R1.7.3** Fenced code blocks never soft-wrap regardless of the window setting;
they scroll horizontally. Wrapped code is unreadable and its indentation is
semantic.

**R1.7.4** Wrap measurement uses displayed width, so it accounts for conceal
per R1.6.5 and R1.6.9.

**R1.7.5** `j` and `k` move by **display row**; `gj` and `gk` move by source
line. This inverts vim's default deliberately: in a note a source line is often
a whole paragraph wrapping to several rows, so vim's `j` leaps a screenful and
`$` lands off-screen. Both are configurable, so the mapping can be swapped
back.

**R1.7.6** Operators composed with `j`/`k` act on the display rows the motion
covers, and with `gj`/`gk` on whole source lines. `dd` remains source-line-wise
regardless.

Rejected:

- **Wrap off by default** (matching the nvim config). That config also edits
  code; cwiki's buffers are prose, and horizontally scrolling to read the end
  of a sentence during a lecture is the failure case that matters.
- **Vim's `j`/`k` semantics.** Preserves muscle memory, but in wrapped prose it
  skips whole paragraphs, which is why prose-focused vim users commonly remap
  it. Retained as a configuration option rather than the default.

### 1.8 Rendered mode and view sync

**R1.8.1** Rendered mode is a viewer, not a second editor. A window in rendered
mode holds a scroll position, not a text position, and has no cursor.

**R1.8.2** Mode is per window, per the brief, so one window can render a note
while another edits the same buffer.

**R1.8.3** Entering rendered mode always scrolls the view to the rendered
position corresponding to the editing cursor, independently of whether
continuous sync is enabled. Not losing your place on a mode switch is not a
sync feature.

**R1.8.4** Returning to editing mode restores that window's own remembered
cursor when continuous sync is off, since independent scrolling is the purpose
of switching sync off. With sync on, the cursor has been following the view
already.

**R1.8.5** Continuous sync is togglable, with a global option setting the
default and a per-window toggle overriding it.

**R1.8.6** Forward sync: moving the editing cursor scrolls a synced rendered
window by the minimum needed to bring the corresponding rendered row into view,
rather than recentring on every motion.

**R1.8.7** Cursor movements caused by sync are recorded in the jump list, so
`<C-o>` and `<C-i>` retrace them. Whether sync jumps are recorded is itself
togglable.

**R1.8.8** Each sync direction is independently configurable as **continuous**
or **explicit**:

| Direction | Continuous | Explicit |
|---|---|---|
| Forward (editing → rendered) | moving the cursor scrolls the rendered view by the minimum needed to bring the target row into view | a key scrolls the rendered view to the cursor's target row |
| Inverse (rendered → editing) | scrolling the rendered view moves the editing cursor to the marked line | a key moves the editing cursor to the marked line |

Sync-induced moves are flagged so they cannot trigger the opposite direction,
which is what makes continuous-both-ways safe. With continuous inverse sync,
jump-list entries are recorded only when a sync move exceeds one screen, since
recording every scroll step would flood the list.

**R1.8.9 (sync marker)** The rendered view shows which source line an inverse
sync would jump to, as a marker in a **narrow sidebar gutter**, fixed at the
exact vertical middle of the viewport, naming the target line number. Content
scrolls past a stationary reticle. The marker never highlights or overlays
anything in the rendered content itself — a highlight inside typeset output is
visual clutter. The sidebar's presence is configurable.

**R1.8.10 (mapping)** The layout pass builds two tables per rendered view:
rendered row → source line range, and source line → rendered row. Prose maps
exactly, because cwiki performed the layout. A formula or diagram image maps as
a whole to the source range of its block.

**R1.8.11 (SyncTeX)** Blocks spanning more than one source line are additionally
compiled with `-synctex=-1`, which writes an **uncompressed** side file, so
cwiki needs no zlib. The sidecar is stored in the render cache beside the image.
Single-line blocks and prose get no sidecar: the tables in R1.8.10 already
answer exactly.

**R1.8.12** cwiki parses the sidecar itself with a minimal reader — parse one
page's record tree, find the innermost box containing a point, return its file
tag and line — rather than vendoring the canonical `synctex_parser.c`, which is
~200 KB of third-party C that would have to be exempted from the strict build,
clang-tidy and the sanitizers. The reader is fuzz-tested alongside the Markdown
parser.

**R1.8.13** A sidecar is parsed on first need per block and the parsed form kept
in a bounded in-memory cache. Because R1.8.9 displays a target line
continuously, scrolling the marker into a multi-line block triggers that block's
parse; scrolling within it does not. Rendering itself never parses a sidecar.

**R1.8.14** Because the block is compiled as a standalone temporary document,
SyncTeX line numbers refer to that temporary file. cwiki generated it, so
temp-line → note-line is a table it already holds and must retain in the cache
entry.

**R1.8.15** Mapping precision is **line granularity and no finer**, inside
blocks as well as in prose. SyncTeX records boxes, so character-level positions
are not available at any cost; no part of the UI may imply otherwise.

**R1.8.16** A source line that produced no output (a comment, a blank line, a
line consumed entirely by a macro definition) has no rendered row. Forward sync
targets the nearest following line that does, and if none exists, the nearest
preceding one.

**R1.8.17 (post-jump cue)** After an inverse sync moves the editing cursor, the
destination line is briefly highlighted **in the editing window**. A cursor that
teleports with no explanation is disorienting; this is the cheap fix. It does not
conflict with R1.8.9's ban on highlighting inside rendered content — the cue is in
the source view, not the typeset one.

**R1.8.18 (mapping is an optional capability)** Position mapping is treated as a
capability that may be unavailable — a block whose render failed, a sidecar that
could not be parsed, a note not yet rendered. When it is unavailable cwiki
**degrades to scrolling rather than jumping**, and says so. A silent, confidently
wrong cursor jump is the failure mode this exists to prevent.

**R1.8.19 (atomic artefact swap)** A rendered artefact is never displayed while it
is being written. Each render writes to a temporary path and is swapped in by
rename after the run succeeds — the same discipline as note writes. A background
re-render can therefore never make a half-written image appear in rendered mode.

Rejected:

- **A single cursor shared by both modes, always a source position.** Attractive
  because mode switching becomes free, but a rendered view is a viewer and an
  insertion point in typeset output is meaningless.
- **Two cursors mapped at switch time.** A conversion that runs only on switch
  rounds differently in each direction, so a round trip does not return you to
  where you started.
- **Block-granular mapping only.** Loses precision in prose, which is the case
  that maps perfectly for free.
- **SyncTeX for every block.** Pays the flag and the cache storage for inline
  formulas whose answer is already known exactly, and a vault of class notes is
  mostly inline formulas.
- **No SyncTeX at all.** On a long derivation — the thing most read — the sync
  marker would name a line up to thirty lines away from what is on screen.
- **Highlighting the sync target inside the rendered content.** Clutters
  typeset output; the sidebar reticle carries the same information.
- **Continuous inverse sync as the only model, or explicit as the only model.**
  Both are now configuration (R1.8.8).

### 1.9 Zone engine

**R1.9.1** Zones are recognised by a **declarative region table** in the model
of Vim's syntax regions: each region has a start pattern, an end pattern, an
optional skip pattern for escaped delimiters, and a set of regions it may
contain. This is the model vimtex's own definitions are written against, which
is what makes vimtex-level fidelity reachable.

**R1.9.2** Regions are evaluated **per line**, entered with the previous line's
cached zone stack (R1.1.3). An edit re-runs the region patterns over the edited
line and continues downward only until a line's end stack matches its previous
value. There is no whole-buffer scan, and the per-keystroke cost is one line.

**R1.9.3** Region start, end and skip patterns are **anchored** at the scan
position. Unanchored whole-line searching is not used for zone recognition.

**R1.9.4 (bounded structural search)** Structural search is **one shared
primitive**, not a pattern each caller reimplements. Every call takes a
line-distance limit and a time budget, and every result reports *which
alternative of the pattern matched* so callers can branch without re-examining the
text. Exceeding a bound reports "not found" rather than stalling: a bounded wrong
answer is preferable to an unbounded right one while typing. The bounds exist from
the first search written — vimtex's own answer to "this is slow" is these limits,
and retrofitting them is worse than having them.

**R1.9.5** The **innermost zone wins**, and the table may register *holes* —
regions inside a zone that revert to an outer context. Holes include `\text{…}`
and `\intertext{…}` inside math, and the argument of a reference-like command
(`\label{}`, `\ref{}`, `\eqref{}`), which is an identifier rather than maths.
`\ce{…}` inside math is a chemistry zone, not a hole. Every consumer queries the
innermost zone, so a context test answers "which zone owns the cursor", never "is
this zone anywhere above me".

**R1.9.6** The region table is user-extensible configuration. Adding an
environment, a fence language or a custom math zone is a config entry, not a
code change. Two specific forms must be expressible: extra environments declared
as math zones, and environments or fences that **host another language, with the
language named by the block's own argument** — so ```` ```python ```` and
`\begin{minted}{python}` both select a nested syntax from the block header rather
than from a hard-coded list.

**R1.9.6a (escape- and comment-aware position idiom)** Two reusable patterns are
defined once and used by every structural search and region rule: "this position
is not escaped" (not preceded by an odd number of backslashes) and "this position
is not inside a line comment" (not preceded on its line by an unescaped comment
marker). Both are lookbehinds, which is part of why PCRE2 was chosen (R1.11.1).
This is what lets the cheap structural searches skip comments **without
consulting the zone engine at all** — the fast path stays fast.

**R1.9.6b (parser escape hatch)** A directive in the note switches the tokeniser
into a dumb zone and back — the equivalent of vimtex's `% VimTeX: SynIgnore
on/off`. One pathological block must not corrupt highlighting, conceal or snippet
gating for the rest of the note.

**R1.9.6c (comment zones)** Three comment forms, as distinct zones:

| Form | Where |
|---|---|
| `%% … %%` inline, and `%%` alone on a line opening a block | anywhere in a note |
| `<!-- … -->` | anywhere in a note, for compatibility with Markdown pasted from elsewhere |
| `%` to end of line | **only inside math, LaTeX and TikZ zones**, where it genuinely is a comment |

`%` is never a comment in prose, where it means percent. `\%` and `\$` are
handled by the escape-aware idiom of R1.9.6a, so `50\%` inside math is not a
comment. `%%` was chosen over `<!-- -->` as the primary form because comments
written in it survive as literal text in other Markdown tools rather than
vanishing silently, and because two characters is short enough for a quick aside.
This is what gives note 16 ("TODO/FIXME highlighted and indexed only in comment
blocks") something to scope to, and what row 179's comment-block motions move
over.

**R1.9.7** One per-line zone dataset feeds all of: source highlighting, conceal
category selection, snippet context gating, fold levels, the outline, and the
LaTeX text objects and motions. There is no second parser for any of them.

**R1.9.8** The zone engine is fuzz-tested. Notes arrive over git from another
machine, and a parser that can loop or over-read on malformed input is a
correctness and safety problem, not only a display one.

Rejected:

- **A table-driven tokenizer over literal open/close tokens**, with no regex
  involved. Cheaper and sufficient for all four consumers, and it was the
  recommendation on cost grounds. Rejected because cwiki needs a regex engine
  anyway for search, `:s` and regex snippet triggers, so the engine is shared
  rather than an additional cost — and because the literal-token model cannot
  express the region definitions vimtex fidelity is specified in terms of, and
  would make user extension a code change.
- **A recursive-descent parser producing a tree.** Cleanest semantics, but the
  per-keystroke cursor-zone query would reparse the enclosing block every
  keystroke, where a cached per-line stack answers in O(1), and incremental
  reparse boundaries are the part that goes wrong.

### 1.10 Snippet engine

**R1.10.1** Snippet sessions form a **stack** to a bounded depth, so expanding a
snippet inside another's tab stop nests. `//` inside a fraction numerator is the
everyday case and must work.

**R1.10.2** Tab stops, mirrors and the session's own extent are ordinary buffer
positions, so the position-fixup machinery of R1.1.4 keeps them correct across
edits with no separate tracking mechanism.

**R1.10.3** A mirror update is performed as a real buffer edit, guarded against
re-entrancy so that updating one mirror cannot retrigger mirror updating.

**R1.10.4** A session pops when the cursor jumps past its `$0`, or when an edit
lands outside its extent. Popping the innermost session does not disturb the
ones below it.

**R1.10.5** Undo interaction: one expansion is one undo step (R1.2.3). The
mirror updates caused by an edit belong to the **same** undo step as that edit,
so undo can never leave mirrors disagreeing. Undoing an expansion pops its
session.

**R1.10.6** Body syntax supports numbered tab stops, defaults
(`${1:placeholder}`), mirrors (the same number used twice), a final `$0`, regex
capture references, and a visual-selection placeholder that receives the text
selected before expansion.

**R1.10.7** Computation in bodies is a **closed set of named transforms**
applied to a capture or a stop — initially `upper`, `lower`, `capitalize`,
`match-bracket-backward` and `space-unless-punctuation` — extended only when a
real snippet needs one. There is no interpreter, nothing to sandbox, and no
shell escape reachable from a snippet.

Body references use explicit, non-overlapping spellings: `${capture:1}` inserts
regex capture 1, `${capture:1|upper}` applies a named transform to that capture,
and `${stop:1|upper}` inserts a transformed mirror of tab stop 1. Standard `$1`,
`${1:placeholder}` and `$0` retain their usual tab-stop meanings. This syntax was
accepted on 2026-09-29. Compact `${c1}` / `${s1}` forms were rejected as opaque;
function-like `${upper(capture:1)}` was rejected as unnecessary expression
syntax; deferring captures and transforms was rejected because the curated M1
snippet library requires them.

**R1.10.8** Trigger matching on each keystroke: literal triggers live in a trie
keyed on reversed text, so matching the text before the cursor is one walk
bounded by the longest trigger and independent of library size. Each regex
trigger is tagged with a required final character and a required zone, and runs
only when both match. Cost does not grow as the snippet library grows, which it
will, per subject, for years.

**R1.10.9** Trigger flags: auto-expand versus expand-on-key, word-boundary,
beginning-of-line, and an explicit priority ordering between competing matches.

**R1.10.10** Context gating is by **innermost zone** (R1.9.5). A math snippet
does not fire inside `\ce{}`, inside a code fence, inside a comment, or inside
`\text{}` within math.

**R1.10.10a (zone-dependent bodies)** A snippet may declare **several bodies keyed
by zone**, not only a single body plus a gate. One trigger can therefore expand
differently in an `align` environment than in inline math, or differently in
`\ce{}` than in prose. This is the brief's per-context requirement expressed as a
property of a snippet rather than as separate snippet files per context.

**R1.10.10b (symbol expansions are snippets)** The leader-prefixed insert-mode
symbol table vimtex ships — roughly seventy entries mapping a short key sequence
to `\alpha`, `\infty`, `\rightarrow` and so on — is **not a second expansion
mechanism**. It ships as default auto-expanding snippets gated to math zones. This
is the resolution of the tension noted in §0: vimtex removed snippets in favour of
insert-mode maps, but cwiki has a snippet engine already, so the maps become data
for it.

**R1.10.10c** A command lists the expansions currently active at the cursor, given
its zone. It is surfaced through the clue-popup system (R1.14.1), not as a separate
viewer.

**R1.10.11** Snippet sets layer: a global set plus per-subject sets, selected
from the note's own declared subject metadata. Not by switching a symlink, which
is how the researched workflow does it.

**R1.10.12** Pasted text never triggers snippet expansion, autopairs or
abbreviation (follows from R1.5.4).

Rejected:

- **A single session that ends when a nested snippet expands**, or one in which
  nested triggers stay literal. Both break nested fractions, subscripts inside
  limits, and `\ce{}` inside an equation — most of the LaTeX actually written.
- **One small typed expression language shared with calendar recurrence.**
  Appealing as "one language", but the two need nearly disjoint primitives, so
  it means one parser and two builtin sets, and it puts a language design on the
  critical path of the first milestone used daily.
- **Embedding Lua.** Arbitrary code running per keystroke during a lecture,
  needing a sandbox and an error policy, for four helpers.
- **Static bodies with no transforms.** Kills the bracket-matching fraction
  snippet, one of the highest-value ones in the research.
- **Evaluating every snippet on every keystroke.** Degrades precisely as the
  feature succeeds.
- **Auto-expanding literal triggers only.** The auto-subscript and postfix
  decorations are regex triggers whose whole value is firing without a keypress.

### 1.11 Regex engine

**R1.11.1** cwiki has exactly one regex engine, **PCRE2 ≥ 10.40**, shared by all
four consumers: the zone engine (§1.9), typed search, `:s`/`:g`, and regex
snippet triggers. Official binary packages on every target exceed the floor
(revalidated 2026-09-28):

| Platform | Package and current version |
|---|---|
| FreeBSD | `pkg install pcre2` (`devel/pcre2`); quarterly 10.47_1, latest 10.48 |
| Arch | `core/pcre2` 10.48 |
| macOS | `brew install pcre2` (homebrew/core) 10.48 |

PCRE2 was chosen over a hand-written non-backtracking engine specifically for
**lookbehind**, which is what makes the region patterns vimtex fidelity depends
on — "a delimiter not preceded by a backslash" — expressible at all.

**R1.11.2** Patterns are compiled once when their source is loaded (config load,
or command entry) and JIT-compiled. Nothing compiles a pattern per keystroke.

**R1.11.3** `pcre2_set_match_limit` and `pcre2_set_depth_limit` are set on every
compiled pattern. PCRE2's interpreter enforces both. PCRE2 explicitly ignores
the depth limit when JIT code runs, so every pattern is also assigned a bounded
JIT stack; exhausting it is a distinct limit result. Together these bound both
execution paths rather than pretending the interpreter's depth counter applies
to JIT.

**R1.11.4** Hitting a limit is never silently wrong, and the response differs by
consumer:

| Consumer | On limit |
|---|---|
| Typed search or substitute | abort, message naming the pattern and the limit breached |
| Zone engine | no-match at that position, continue, mark the line degraded in the gutter so highlighting and snippet gating there are visibly untrustworthy |
| Snippet trigger | disable that trigger for the session, report once — a trigger that stalls every keystroke is worse than a missing trigger |

The shared runtime distinguishes match-limit, interpreter depth-limit, and JIT
stack-limit results. This JIT policy was accepted on 2026-09-28 after direct
implementation showed that unanchored `pcre2_match()` automatically uses JIT,
while runtime `PCRE2_ANCHORED` falls back to the interpreter.

**R1.11.5** PCRE2 runs in UTF and UCP mode. `.` matches one codepoint; `\w`,
`\d` and `\b` follow Unicode properties, so Greek and accented letters are
letters; `\p{…}` classes are available; case-insensitive matching uses Unicode
case folding; `\X` matches a grapheme cluster. This is deliberately a different
granularity from R1.3.6 — motion and editing operate on clusters, patterns on
codepoints, `\X` bridges them — because that is how every other regex engine
behaves, so patterns carried in from elsewhere act as expected.

**R1.11.6** Patterns the user **types** (`/`, `?`, `:s`, `:g`) are in **Vim's
dialect**, with `\m` the default magic level and `\v`, `\M`, `\V` switches, and
are mechanically rewritten to PCRE2 syntax before compiling. Rationale: the user
continues to use nvim daily for C, so `/` and `:s` reflexes must produce
identical results in both programs; a dialect split between programs never
amortises.

**R1.11.7** Patterns in **configuration** — the zone region table, snippet
triggers — are written in PCRE directly. Authored patterns want the opposite
dialect from reflex-typed ones: the regex snippet triggers actually written
(`([a-zA-Z])bar`, `([A-Za-z])(\d)`) need no backslashes in PCRE but would need
`\(…\)` in Vim dialect, and PCRE is the dialect every external reference uses.

**R1.11.8** `set regex-dialect=pcre` switches typed patterns to PCRE for a user
who prefers a single dialect.

**R1.11.9** Vim constructs with no PCRE2 equivalent — `\zs`/`\ze` after a
variable-width prefix (PCRE2 lookbehind is width-bounded), `\%V`, `\%d123` — are
**rejected with a message naming the construct**. The translator never guesses;
a mistranslated substitute rewrites text wrongly and silently.

**R1.11.10** Typed searches ignore case unless the pattern contains an uppercase
letter, carrying over `ignorecase` + `smartcase` from the nvim config. Config
patterns are always case-sensitive.

**R1.11.11** The dialect translator is tested by a table of Vim→PCRE pairs and
fuzzed, with the invariants that it never crashes, never emits a pattern PCRE2
fails to compile, and rejects rather than mistranslates.

Rejected:

- **A hand-written non-backtracking engine** (Thompson NFA/DFA). Guaranteed
  linear time and no dependency, but no lookbehind and no backreferences in
  patterns, so the "not preceded by a backslash" idiom would need rebuilding —
  and it means owning a regex engine on top of a zone engine, a snippet engine
  and a renderer.
- **Oniguruma.** Packaged everywhere and supports several dialects natively, but
  none of them is Vim's, so it buys no dialect cwiki wants, and its backtracking
  limit controls are weaker than PCRE2's.
- **PCRE everywhere including typed patterns.** One dialect to document, but
  search reflexes would differ between nvim and cwiki permanently, failing as a
  search that finds nothing rather than as an error.
- **Vim dialect everywhere including config.** Would route the zone table's
  lookbehind patterns — the most correctness-critical in the system — through the
  lossiest part of the translator.
- **Silently treating a limit breach as no-match.** In a program reviewed through
  demos rather than code, an invisible wrong answer is the worst outcome.
- **Unloading a pattern on a limit breach.** One pathological line would disable
  a zone rule across the whole vault.
- **Forcing the interpreter for every match.** It makes depth-limit reporting
  uniform by disabling the JIT execution R1.11.2 requires; bounded JIT memory is
  the correct control for the JIT path.
- **Using JIT's default stack without reporting exhaustion.** This keeps JIT but
  leaves its memory boundary implicit and turns exhaustion into a generic engine
  failure instead of the visible limit result R1.11.4 requires.
- **ASCII-only classes, or byte-mode patterns.** `\w` failing on Greek is
  disqualifying for physics notes, and byte mode lets `:s` produce invalid UTF-8.

### 1.12 Buffers, windows and rendered-view refresh

**R1.12.1** Windows may be arranged freely: a recursive tree of horizontal and
vertical splits, with split, close, close-others, directional focus, move a
window within the tree, exchange, rotate and equalise, plus tab pages holding
independent whole layouts. Semantics follow nvim, including `switchbuf=usetab` —
opening a note already open focuses its existing window rather than duplicating
it. Recorded limitation: a split tree cannot express non-guillotine geometry (a
"pinwheel", where no single line cuts the screen without crossing a window). No
terminal editor offers that and it is not planned.

**R1.12.1a** A separate **floating-window layer** sits above the tree, used for
transient UI — the fuzzy picker, clue popups, input prompts — positioned relative
to the screen or to a window, with configurable size, anchor and border. A float
may be pinned so it persists. This covers FEATURES.md note 8: a floating file
tree and a side panel are the same component differently configured, so the
choice is configuration rather than two implementations.

**R1.12.2** Closing a window never closes its buffer, and deleting a buffer never
disturbs the window layout. A window whose buffer is deleted shows the alternate
buffer or an empty buffer.

**R1.12.3** State is split as in vim:

| Per buffer | Per window |
|---|---|
| text, line records | cursor position, scroll anchor |
| undo tree | mode (editing / rendered) |
| buffer-local marks `a`–`z` | wrap on/off, conceal level and `concealcursor` |
| zone stack cache (R1.1.3) | which folds are open |
| dirty flag, file path, line-ending style | jump list |
| computed fold *levels* | view cache: per-line row counts, source↔display mapping |
| render cache keys (per block) | rendered-view scroll offset, sync mode and toggles |
| snippet session stack | |

The per-window view cache is invalidated on buffer edit, on window resize and on
any change to that window's wrap or conceal options.

**R1.12.4** Two windows on one buffer stay consistent: an edit in one fixes up
the other's cursor, scroll anchor, selection anchors and any live snippet tab
stops, by the same position-fixup pass as R1.1.4.

**R1.12.5** A snippet session belongs to the window whose cursor anchors it. A
session is abandoned if an edit from another window lands inside its extent.

**R1.12.6 (refresh)** A rendered view tracks **the file on disk, not the
buffer**. It re-renders when the note is written, and never as a consequence of
typing. Unwritten buffer changes do not affect any rendered view.

This is what resolves the brief's internal tension — "there is no live preview"
against "one window can render the note another is editing". Both hold: the
split view works, and nothing re-typesets while you type. It also means a
rendered view always shows what git will sync.

**R1.12.7** A write from outside cwiki that changes the note — a git pull, an
external tool — refreshes rendered views of it on the same path as cwiki's own
write, via the external-change detection required by §Data safety.

**R1.12.8** Block images in the render cache are keyed by content hash and shared
across windows and across notes. Rendered *layout* is per window, since it
depends on window width. Two windows of different widths share every compiled
image and compute their own layout.

**R1.12.9 (stale indicator)** While a buffer differs from the file on disk, every
rendered view of that note shows it in the window's status line, with the count
of blocks whose source has changed — `[stale: 3 blocks]`. The editing window
keeps the ordinary modified flag. Optionally, and off by default, the sync
sidebar additionally marks the specific rendered rows whose source has changed,
which needs per-block dirty tracking mapped back to rendered rows.

**R1.12.10 (save policy)** When the note is written is configurable, with three
policies:

| Policy | Writes on |
|---|---|
| `insert-leave` (default) | leaving insert mode, plus an idle period while still in insert mode with a dirty buffer |
| `manual` | `:w` only, vim semantics |
| `idle` | an idle timer, in any mode, whenever the buffer is dirty |

`:w` always works under every policy. `insert-leave` is the default because it
makes the write boundary coincide with the undo-step boundary (R1.2.3), so "a
change" means one thing throughout the program, and because it keeps a rendered
view fresh at every natural pause without anything re-rendering while typing.

**R1.12.11 (startup)** Startup behaviour is configurable, with all three of:

| Setting | Behaviour |
|---|---|
| `startup=restore` (default) | reopen the previous session's tab pages, splits, buffers, per-window modes and cursor positions |
| `startup=dashboard` | open a configurable dashboard of recent notes, due flashcards and today's tasks (FEATURES.md note 14) |
| named sessions | additionally save, load and delete named sessions, so a study layout and a writing layout can be kept side by side |

A note given on the command line overrides the setting and opens in a single
window. The dashboard is also what appears under `startup=restore` when there is
no previous session. There is one implicit session, saved on exit and on layout
change, so a crash costs text but never layout.

Rejected:

- **Fixed layout presets.** FEATURES.md already drops calcurse's eight-preset
  approach as more complexity than a personal tool needs, and it departs from the
  nvim semantics the brief asks for.
- **Splits without tab pages.** Would drop the kept `switchbuf=usetab` row and
  make it impossible to keep a study layout and a writing layout and flip between
  them.
- **Refreshing a rendered view from *unwritten* buffer state** — on leaving
  insert mode or on an idle timer, independently of whether a write happened.
  Rejected because what you read would then not be what is on disk or in git.
  (Under the default save policy the write itself happens on leaving insert mode,
  so the refresh does occur there — but as a consequence of the write, which is
  the distinction that matters.)
- **Manual-refresh-only rendering**, decoupled from writing. Predictable, but
  leaves the split view stale by default, which is the workflow that makes people
  abandon split view. The `manual` save policy produces the same effect for
  anyone who wants it, without a second mechanism.
- **Continuous re-render while typing.** Live preview, ruled out by the brief,
  and it puts TeX runs on the typing path during a lecture.
- **Buffer-local wrap, conceal and folds.** Simpler and one measurement cache,
  but two windows on one buffer cannot then have different widths, and the case
  of one window showing concealed glyphs for reading while another shows raw
  source for editing — close to the split view actually requested — becomes
  impossible.

### 1.13 Crash recovery

**R1.13.1** cwiki keeps no swapfile in the vault. Crash recovery uses an
append-only **recovery journal** per dirty buffer, in the OS state directory
alongside the undo files (R1.2.5), never in the vault and never in git. This
resolves the `?` on the FEATURES.md "no swapfile" row: no swapfile, but not
nothing.

**R1.13.2** A journal record is a length-prefixed edit delta — offset, bytes
removed, bytes inserted — so flushing is incremental and proportional to what
changed rather than to buffer size. The header records the note's vault-relative
path, the content hash of the last written state, a timestamp and the writing
process's pid.

**R1.13.3** Records are buffered and flushed on a short timer; the journal is
`fsync`ed when insert mode is left, not per record. A truncated final record from
a crash mid-flush is detected by its length prefix and discarded.

**R1.13.4** On startup, a journal whose owning pid is no longer live and whose
header content hash matches the file currently on disk offers recovery. Applying
it is pure data application and cannot re-execute whatever crashed. A journal
whose header hash does not match the file — the note changed underneath, for
instance by a git pull — is reported and kept, never applied.

**R1.13.5** The journal is deleted on a clean write and on clean exit.

**R1.13.6** Recovery restores **text only**. After recovery the buffer no longer
matches the content hash in its persistent undo file, so undo history is
discarded per R1.2.6. Journalling undo-tree deltas is out of scope.

**R1.13.7** The crash key log (R1.5.9) is not a recovery mechanism. Division of
labour: the key log reproduces the bug, the journal recovers the work. Neither
depends on the other.

Rejected:

- **No recovery at all**, relying on the default save policy to bound loss to one
  insert session. Free, and strictly safer than the user's current nvim setup
  (`swapfile = false` with no autosave) — but it leaves the `manual` save policy
  with unbounded exposure.
- **Key-replay recovery**: replay the recorded key stream since the last write to
  re-derive the lost state. Recovers strictly more (text, undo tree, marks) and
  R1.2.4's determinism guarantee makes it sound in principle. Rejected because it
  re-executes the code path that crashed, so a deterministic bug reproduces the
  crash and recovery fails exactly when it is needed; because R1.7.5 makes `j`/`k`
  display-relative, so replay is faithful only at the original window size; and
  because making it safe needs a subprocess plus per-key checkpointing. It remains
  the right mechanism for *bug reproduction*, which R1.5.9 already provides.
- **Rewriting the whole dirty buffer periodically** instead of journalling
  deltas. Simpler, but turns every flush into a write proportional to note size.

### 1.14 Cross-cutting architecture

These come from the vimtex rows and are listed separately because they are not
features: each one is a shape the whole program has to be built in, and every one
of them is expensive to retrofit and nearly free to design in.

**R1.14.1 (named actions behind an indirection)** Every user-visible command is a
**named action** in a table, and key bindings map keys to action names. Nothing
handles a key inline. Consequences, all required: every binding is individually
rebindable or removable; the clue popup is generated from the same table rather
than maintained separately (note 20's "clues for any and all keys"); the command
palette enumerates it; macros, replay and the command line all dispatch through
one path.

**R1.14.2 (behaviour as user-editable tables, not booleans)** Where vimtex offers a
table, cwiki offers a table: environment toggle cycles, delimiter pair lists,
delimiter size-modifier cycles, inline↔display math cycles, fold types, conceal
categories, zone region rules, indexer matcher rules, snippet contexts and bodies.
Not booleans and not hard-coded pairs. This is the single most copyable thing about
vimtex's design and it must be decided before the features are written, because a
feature written against a boolean does not become table-driven later without a
rewrite.

**R1.14.3 (per-subsystem enable/disable)** Each subsystem — conceal, delimiter match
highlighting, folding, the outline indexer, diagnostics, background rendering — has
an enable switch. vimtex documents delimiter match highlighting and expression-based
folding as its slowest features and ships off switches for both; cwiki's equivalents
get the same treatment from the start.

**R1.14.4 (on-demand recomputation as the default for expensive derived state)** Fold
levels, the outline index and package detection are computed on demand with an
explicit refresh, not recomputed on every change. Refresh policy is itself
configurable (always / on write / on demand) rather than implicit. vimtex's
documentation states plainly that always-refresh does not scale.

**R1.14.5 (internal message log)** A timestamped internal log records each message
with its origin, viewable on demand and **separate from user-facing
notifications**. Given a workflow where behaviour is reviewed through demos rather
than code, this is the only sane way to debug a background render pipeline.

**R1.14.6 (state dump)** A command dumps what cwiki believes about the current note:
detected packages, render status per block, index state, resolved paths, zone stack
at the cursor, active snippet contexts. The equivalent of `:VimtexInfo`, and
directly useful for bug reports.

### 1.15 LaTeX-aware objects and commands

**R1.15.1 (section text object)** `iP` selects a section's body, `aP` the heading
plus its body, always linewise. Extent comes from the Markdown heading level (the
`#` count), and a count grows the object to the enclosing level — `2daP` from
inside a `###` takes the whole `##` it sits in. This is the operator-level
counterpart to the heading motions: motions get you *to* a section, this operates
*on* one, which is what makes promoting a subsection from class notes into a
detailed topic page a single `daP`/`p`.

**R1.15.2 (command object greediness)** `ic` selects the command name alone; `ac`
selects the name plus its argument groups, under **one rule with two clauses**:

1. If the command's arity is known, take exactly that many groups.
2. Otherwise take every consecutive `[…]` and `{…}` group that follows with no
   blank line between.

Exactness is impossible without macro expansion, which vimtex documents; this rule
is correct for every case where arity is knowable and degrades to one predictable
behaviour elsewhere. It is a single documented rule, not a configurable policy.

**R1.15.3 (arity table)** Arity is known from three sources, in order:

| Source | Covers |
|---|---|
| Shipped table | LaTeX core, amsmath, mhchem, TikZ/PGF — the packages the workflow names |
| User table | any command, added as data per R1.14.2 |
| Preamble scan | `\newcommand`, `\renewcommand`, `\providecommand`, `\NewDocumentCommand`, `\DeclareMathOperator` in the vault-wide preamble and in a note's own preamble additions — these *declare* arity exactly, so the user's own macros need no table entry |

The scan is the reason this is worth doing rather than guessing: every macro the
user defines states its own arity, so the common case is exact for free.

**R1.15.4 (line-break toggle)** One action toggles a trailing `\\` on the current
line, count-aware so it can apply to a range. `\\` is the row separator in
`align`, `matrix`, `pmatrix`, `cases` and `tabular`, so a multi-row matrix or a
multi-line derivation ends nearly every line with it.

**R1.15.5 (surround with environment)** One action wraps the current line, a
motion's range or a visual selection in an environment prompted for with
completion, then re-indents the region. The natural "wrap these three lines in
`align`" action; cheap once surround and the input prompt exist, both of which are
already required.

### 1.16 Cost of the kept `vim` and `nvim` rows

Re-estimated against the architecture decided above rather than the initial
guesses, which predate it. Scale as in `FEATURES.md`: **S** under one focused
implementation task, **M** 1–3, **L** 4–10, **XL** over 10. "Δ" marks rows
whose cost moved.

| Row | Initial inventory | Now | Why it moved |
|---|---|---|---|
| Operators + `{op}{count}{motion｜textobject}` grammar | L | **L, 7–10** | Δ up. Motions are now display-relative (R1.7.5) so the operator×motion matrix must handle display-row ranges, and conceal means char-wise operators go through the source↔display mapping (R1.6.4). |
| Visual modes: char, line, **block**, `gv`, `o` | M–L | **L, 5–6** | Δ up, and splits: char+line ≈ 2, block-wise ≈ 3–4. Block-wise over wrapped and partly concealed lines with `virtualedit=block` is the genuinely hard part. |
| Undo tree + persistent undo + recovery journal | M + M | **L, 5** | Δ up. Tree, on-disk format, hash validation, plus §1.13's journal. |
| Soft wrap for prose | M | **M–L, 3–4** | Δ up. Now carries display-row `j`/`k`, the per-window view cache, conceal reflow, breakindent and continuation markers. |
| Splits / windows / tab pages / float layer | M | **M–L, 3–4** | Δ up. Tree plus tab pages plus floats plus per-window view caches plus `switchbuf=usetab` and non-destructive buffer delete. |
| Text objects: word/WORD, sentence, paragraph, quotes, brackets, tag | M | **M, 2–3** | Sentence boundaries must be zone-aware — a `.` inside `$…$` is not a sentence end — which the zone stack already answers. |
| Dot-repeat | M | **M, 2–3** + tax | See the multiplier note below. |
| Fuzzy picker (one component, reused everywhere) | M | **M, 2–3** | Fuzzy matcher + live-filtered list on the float layer. Highest leverage row in the table. |
| Folding | M | **M, 2–3** | Heading and environment folds, levels from zone data, open/closed per window, generated fold text. |
| Substitute `:s` + `:g`/`:v` + ranges | M | **M, 2** | Δ down. PCRE2 and the dialect translator already exist by then. |
| Search motions `/ ? n N * #`, `f F t T ; ,`, offsets | M–L (`?`) | **M, 2** | Δ down, and no longer open: §1.11 settles the pattern language. |
| Leader/all-key clue popups (note 20: every key, configurable) | M | **M, 2** | Keymap-tree data structure plus popup on the float layer. |
| Marks, manual and automatic | M | **S–M, 1–2** | Δ down. Position fixup already exists (R1.1.4). |
| Registers (note 2 scope: no expression register, OSC 52 for clipboard) | M | **S–M, 1–2** | Δ down by scope. |
| Macros: record, replay, counts | M | **S–M, 1** | Δ down sharply. R1.5.9 already records and replays the key stream; macros are that machinery with a register attached. |
| Jump list (+ change list) | M | **S, 1** (+S) | Δ down. Load-bearing anyway for sync jumps (R1.8.7). |
| Command-line UX | M | **S–M, 1–2** | — |
| Extended `a`/`i` objects (note 5) | L | **S** | Δ down hard. No tree-sitter (R1.4.1), so whole-buffer `aB`/`iB` is trivial and the function objects stay dropped. |
| ~30 remaining S rows (toggles, statusline, tabline, yank highlight, cursorword, line numbers, indent config, alignment, trailspace, `%`, surround, autopairs, f/t enhancement, window-focus keys, prompts, icons, notifications, …) | S each | **4–6 total** | Individually trivial, collectively a working week. Should be batched as polish tasks, never costed one by one. |
| Deferred by your own notes: 2D label jump (note 9), split/join (note 6) | M each | **M, 1–2 each** | Out of early milestones by your note. |

**Rows that dominate the schedule**, in order: the **operator–motion grammar**
(7–10), **visual modes** (5–6), **undo** (5). Together 17–21 focused
implementation tasks — before a single formula has been typeset.

**One hidden multiplier, not a row.** Dot-repeat is not a feature that finishes;
it is a requirement every future command must satisfy, including the vimtex
manipulation commands (the "dot-repeatability of every manipulation command"
row). Its 2–3 focused implementation tasks buy the mechanism; each later
command then pays a little.
Budget it as a standing tax on the editor, not a line item.

**Total for the kept `vim`/`nvim` rows: roughly 45–60 focused implementation
tasks.** This excludes the renderer, the separately costed vimtex rows,
flashcards, the calendar, tasks and projects. It is the single largest block in
the project and it is all in front of the first milestone that is useful daily.

Mitigations, to be carried into ROADMAP:

1. **Split the operator grammar across milestones.** A core of `d`/`c`/`y` with
   char, word and line motions is enough to take notes; text objects, the
   case/format/filter operators and the full count grammar follow later. The L
   row becomes three M-ish slices with a usable editor after the first.
2. **Defer block-wise visual entirely.** It is the expensive half of visual modes
   and the least used for prose.
3. **Batch the S rows** into two or three polish tasks rather than thirty.
4. **Build the fuzzy picker early.** One component that every later screen reuses
   — notes, headings, commands, flashcards, tasks — so its cost amortises across
   the roadmap instead of being paid again per screen.

---

## 2. Data model and file format

### 2.1 Vault

**R2.1.1** The vault is an ordinary directory tree. Notes are plain Markdown
files; there is no database holding note content. Everything cwiki keeps that is
*not* authored by the user — the index, render cache, undo trees, recovery
journals, render cache — lives outside the vault (R1.2.5, §1.13) and is
rebuildable.

**R2.1.2** Per-vault configuration lives in a hidden directory at the vault root,
kept separate from note content so the vault stays clean in git. Cross-vault
application settings live in the OS config location.

### 2.2 Flashcards

**R2.2.1** A card file is one Anki-style **note**: frontmatter declaring its note
type and holding its named fields. Cards are *derived* from the note type's
templates, so one file can generate a forward and a reverse card. The
FEATURES.md decision to drop flashcards-in-notes syntax is what puts these in
their own files.

**R2.2.2** Decks are **directories**; subdecks are subdirectories. Deck identity is
the path, so reorganising decks is moving files and needs no rewrite anywhere else.

**R2.2.3** One file per note, never one file per deck. Two machines adding cards
then touch different files and never the same region of one — which is the normal
case, since cards get added wherever studying happens.

**R2.2.4** Frontmatter carries a **stable id**, the note type, the named fields,
an optional `source` wikilink back to the note the material came from, and a
**subject** classification. The subject is a separate field rather than being
inferred from the deck path, because topics overlap between subjects —
thermodynamics, electromagnetism and spectroscopy each appear in both physics
and chemistry — so deck placement cannot carry it. Standalone cards need no
fake source note.

**R2.2.5** The subject field is the same one that selects layered snippet sets
(R1.10.11), so a card and the note it came from agree about their subject without a
second mechanism.

```
cards/Calculus/Derivatives/chain-rule.md
---
id: 01J9F2
type: basic-reversed
subject: [calculus]
source: "[[Derivatives#Chain rule]]"
fields:
  Front: "$\\frac{d}{dx}f(g(x))$"
  Back:  "$f'(g(x))g'(x)$"
---
```

Rejected:

- **One file per deck.** Fewer files and a deck readable in one buffer, but adding
  cards to the same deck on two machines before syncing conflicts in git, and that
  is the normal case.
- **Cards as blocks inside notes.** Keeps a card next to the material that
  produced it and makes drift impossible, but it was decided against in
  FEATURES.md, and it makes note files carry card identity so editing prose around
  a card risks disturbing it.
- **Inferring subject from the deck path.** Fails exactly on the overlapping
  topics, which are the ones most worth cross-referencing.

### 2.3 Embedding LaTeX in Markdown

**R2.3.1** `$…$` is inline math and `$$…$$` display math. Every unescaped `$`
delimits math — there is **no disambiguation heuristic**. A literal dollar is
written `\$`.

Rationale: in chemistry, physics, calculus and computer-science notes a currency
figure essentially never appears, and the rule is then one sentence with no
lookaround, no corner cases and nothing that behaves differently depending on
surrounding whitespace. Accepted cost: pasting text containing prices turns part of
a paragraph into a math zone until the dollars are escaped, and notes are less
portable to other Markdown tools than the Pandoc rule would make them.

**R2.3.2** `\(…\)` and `\[…\]` are also accepted, as unambiguous alternatives.

**R2.3.3** A `$` inside a code fence or a comment zone is literal, since zone
nesting (R1.9.5) already decides that and math is not a valid inner zone of either.

Rejected:

- **Pandoc-style constraints** (opening `$` not followed by whitespace, closing `$`
  not preceded by whitespace, no blank line inside). More portable and handles
  prices, but it makes math delimiting depend on invisible whitespace, so
  `$ x^2 $` silently is not math.
- **`\(…\)` and `\[…\]` only.** Zero ambiguity, but `$` is what the snippets,
  the muscle memory and every LaTeX habit produce, at twice the keystrokes per
  inline formula.

### 2.4 Review history

**R2.4.1** Review history lives in **one strictly append-only file**,
`review/history.jsonl`, inside the vault and therefore in git. Records are never
modified or removed; the file is only ever appended to.

**R2.4.2** `.gitattributes` in the vault declares `review/*.jsonl merge=union`.
`union` is a built-in git merge driver, so it needs no per-machine configuration and
travels with the vault. On a divergent merge git keeps both sides' lines with no
conflict markers, which is what satisfies the brief's requirement that review
history merge without conflicts. Strict append-only (R2.4.1) is what makes union
merge safe: union merge on a rewritten file garbles it.

**R2.4.3** A record carries a unique id (machine identifier plus a monotonic
counter), a timestamp, the card it applies to, the grade given, the scheduling
algorithm version, and the pre- and post-state as computed at the time.

**R2.4.4** Scheduling state is **never read from a record**. It is derived by
folding every record for that card in `(timestamp, machine id)` order, applying each
record's *grade* to the state the previous fold step produced. `post` is stored for
auditing and divergence detection only. Records with a duplicate id are ignored.

**R2.4.5** Consequence, worked through and accepted: if a card is reviewed on two
machines before syncing, both reviews count and the later grade applies to the state
the earlier one produced. A morning lapse followed by an afternoon *Easy* leaves the
card due in days, not weeks — the evidence is not discarded, and the error direction
is toward re-showing a card that was failed.

**R2.4.6** The folded state is cached in the index with a watermark recording the
last record folded. Normal use folds one new record. A merge that lands a record
older than the watermark refolds that card alone from scratch.

**R2.4.7** The scheduling function must be **pure** — `(state, grade, elapsed) →
state` — since R2.4.4 depends on replaying it. Each record names the algorithm
version it was produced under; changing the algorithm therefore rescores history on
the next fold, which is the same behaviour Anki calls "reschedule on change".

**R2.4.8** The full log is retained permanently. It is required for true-retention
statistics and for any later FSRS parameter optimisation, which trains on raw review
history.

**R2.4.9** cwiki syncing git around a review session is good hygiene but is **not**
load-bearing for correctness. Reviewing offline is fully supported.

Rejected:

- **Plain single file with sync before and after each session.** Reduces conflict
  frequency but does not satisfy "merge without conflicts": it fails on offline
  review, on a failed push, on a crash before the post-session push, and on two
  sessions before one push — and the conflict lands as markers inside the history
  file, where resolving by picking a side silently destroys one machine's reviews.
- **Trusting the latest record's stored state.** Order-insensitive and simpler, but
  it erases the earlier review's effect on ease and lapse count.
- **Discarding same-day duplicate reviews.** Faithful to what a working sync would
  have done, but throws away review work actually performed, and "same day" needs a
  day-cutoff definition that has to agree across time zones.
- **One append-only log per machine.** Conflict-freedom would be structural rather
  than driver-dependent, but it means several files where one suffices, plus a
  machine id stored outside the vault.
- **Per-card state files.** Conflict whenever the same card is reviewed on two
  machines, in a file whose correct resolution git cannot infer.

### 2.5 Note names and link resolution

**R2.5.1** A note's link name is its filename stem. Resolution is **case-sensitive
and normalization-insensitive**: `[[chain rule]]` does not resolve
`Chain Rule.md`, but canonically equivalent NFC and NFD spellings resolve the
same note.

**R2.5.2** Resolution compares NFC-normalized, case-preserving keys against
cwiki's own **index** and never asks the filesystem. This makes R2.5.1
platform-consistent: the same link resolves, or fails, identically on macOS,
Linux and FreeBSD. Resolving by attempting to open a path would make behaviour
depend on the filesystem's case and normalization behavior, so a link written
on macOS could be dead on Linux — which the brief rules out.

**R2.5.3** **Collision guard.** cwiki refuses to create, and reports when scanning,
any two notes whose names differ only by case or by Unicode normal form. Such a
vault cannot be checked out on macOS at all — the second file clobbers the first —
so a collision means a vault already broken on one of the three target machines.

**R2.5.4** Because link text must match case exactly, **completion is the primary
way links are written**: typing a wikilink opens a completion menu over note names,
aliases and headings, driven by the link index. This is the same completion
mechanism as the label/anchor row, not a second one.

**R2.5.5** Frontmatter `aliases` add further exact keys for a note. `[[Note#Heading]]`
resolves headings through the same index and the same case-sensitive,
normalization-insensitive rule.

Rejected:

- **Folding case for resolution.** Makes `[[chain rule]]`, `[[Chain Rule]]` and
  `[[CHAIN RULE]]` all work, identically everywhere, but hides mistakes and
  weakens the predictable filename-based naming rule. Rejected in favor of
  case-sensitive matching plus completion.
- **Exact-byte, normalization-sensitive resolution.** Preserves every byte-level
  distinction, but makes canonically equivalent Unicode spellings fail to match
  and conflicts with R1.3.4's cross-platform comparison-key rule.
- **Case-sensitive resolution via the filesystem.** Simplest, needs no index for
  resolution, but behaviour then differs per machine, which the brief rules out.
- **Folding with a tie-break rule on collision.** Silently sends a link to one of
  two notes and hides the fact that the vault cannot be cloned onto macOS.

### 2.6 Tasks

**R2.6.1** Inline checkbox tasks live in notes, with extended states: `- [ ]` open,
`- [x]` done, `- [/]` in progress, `- [-]` cancelled, `- [>]` forwarded.

**R2.6.2** Metadata is trailing `key:value` tokens on the checkbox line, plus
`!high`/`!med`/`!low` for priority and `@key:value` for anything custom. Keys:
`due`, `scheduled`, `start`, `done`, `every` (recurrence). Chosen over emoji markers
because tokens are typeable one-handed in a terminal, greppable, and readable as
plain text in any tool — whereas every emoji marker would need a snippet or a picker
to enter, and each is one grapheme cluster occupying two cells, putting constant
traffic through the width rules (R1.3.2).

```markdown
- [ ] Write lab report due:2026-09-20 !high
- [ ] Read ch. 7 scheduled:2026-09-18T14:00
- [/] Problem set 3 due:2026-09-15 every:week
- [x] Titration prep done:2026-09-11
- [>] Ask about problem 4 @to:[[Office hours]]
```

**R2.6.3** A **whole-note task** is a note whose frontmatter declares it one, using
the same key names. This is for a task big enough to deserve its own note — a lab
report, a project deliverable — with body text, links and subtasks inside it. Inline
checkboxes and whole-note tasks are complementary, not alternatives, and share one
vocabulary.

Rejected:

- **Emoji markers (Obsidian Tasks convention).** Compatible with the largest
  existing ecosystem and instantly recognisable, but hostile to terminal entry and
  to the cell-width rules.
- **Frontmatter only.** One place for structured data, but a lecture note with eight
  follow-ups would need eight note files to give any of them a due date.

### 2.7 Calendar events

**R2.7.1** One-off and timed events live in one file per day, `calendar/YYYY/MM/DD.md`.
Days with nothing scheduled have no file.

**R2.7.2** Events within a day file are always written **sorted by start time**, each
as a discrete record. This is a merge property, not cosmetics: two machines adding
events at different times of day then edit different regions of the file and git
merges them with no conflict. The conflict unit becomes "the same time on the same
day" rather than "the same day", and a conflict that does occur is a handful of
readable lines.

**R2.7.3** Recurring definitions live separately, one file per definition under
`calendar/recurring/`, because a recurring event belongs to no single day.

**R2.7.4** A day file is the unit of time-blocking: planning tomorrow is editing one
small file, a week view reads seven, and quick-add from a time slot writes into the
right place without the user naming anything.

Rejected:

- **One file per event.** Absolute conflict isolation, but thousands of tiny files a
  year with generated names, rearranging a day means opening many of them, and every
  view costs a directory walk instead of one read.
- **One file per month or per week.** Fewer files and browsable in one buffer, but a
  proportionally larger conflict region and enough length that editing means
  scrolling.
- **Events as notes.** One storage model for everything, but heavy for a
  thirty-minute study block, and building a view would mean scanning every note in
  the vault.

### 2.8 Per-note LaTeX configuration

**R2.8.1** Notes add to the vault-wide preamble through frontmatter, never through
first-line magic comments in the vimtex style (`%! TeX program`, `%! TeX root`):

| Key | Purpose |
|---|---|
| `packages:` | a list of package names; cwiki emits the `\usepackage` lines |
| `preamble:` | a literal block of raw LaTeX for macro definitions and package options |

```yaml
---
packages: [tikz-cd, siunitx]
preamble: |
  \usetikzlibrary{arrows.meta}
  \newcommand{\R}{\mathbb{R}}
---
```

**R2.8.2** Both keys feed the render cache key for that note, so changing a note's
preamble invalidates only that note's blocks and never the vault.

**R2.8.3** Per-block additions are carried in the block itself (R2.9.2), not in
frontmatter.

Rejected:

- **`preamble:` alone.** One mechanism and nothing to translate, but every note
  would spell out `\usepackage` lines and cwiki could not tell which packages a note
  uses without parsing raw LaTeX.
- **`packages:` alone.** Trivial package detection, but a macro needed by one note
  would have to go vault-wide, invalidating every note's render cache.

### 2.9 LaTeX blocks in Markdown

**R2.9.1** LaTeX is written **bare**: `\begin{env}` … `\end{env}` is recognised
directly wherever it appears, with no fence. This is how LaTeX is actually written,
and the zone engine handles it as a literal-token region with a named terminator —
the escape-aware idiom (R1.9.6a) keeps `\\begin` out of it, and nesting comes from
the zone stack (R1.1.3).

**R2.9.2** Per-block options use LaTeX's own argument syntax —
`\begin{tikzpicture}[scale=1.5]` — since there is no fence info string to carry them.
Anything cwiki-specific, such as suppressing a render, is a `% cwiki:` directive line
inside the environment, which is already a comment zone inside LaTeX (R1.9.6c).

**R2.9.3** Fences therefore **invert meaning**: a ```` ```latex ```` fence *displays*
LaTeX as source code rather than compiling it. That is how a note about LaTeX syntax
is written.

**R2.9.4** Only environments cwiki knows to be **available** are compiled. An
unrecognised `\begin{foo}` is highlighted as LaTeX, never sent to TeX, and reported
once. Rationale: an environment from an unloaded package, or one that is a typo,
would produce a compile error anyway, so compiling it buys nothing and costs a worse
error message.

**R2.9.5** The available set is derived from what is actually loaded, not from a
fixed list, which is what makes R2.9.4 exact:

| Source | Provides |
|---|---|
| Shipped per-package environment tables | environments of the packages cwiki knows — LaTeX core, amsmath, mhchem, TikZ/PGF, pgfplots, tikz-cd — each keyed by package and activated only when that package is loaded |
| Vault-wide preamble and a note's `packages:` (R2.8.1) | which of those tables are in force here |
| Preamble scan | `\newenvironment` and `\RenewEnvironment` in the vault-wide or per-note preamble, registered automatically — the same scan that populates the arity table (R1.15.3) |
| User table | environments of a package cwiki does not ship a table for, declared once as data per R1.14.2 |

**R2.9.6** When an environment is unrecognised, the report names it, so the user
knows exactly what to add to the user table or which package declaration is missing.
A typo is therefore distinguishable from an unshipped package without cwiki guessing.

Rejected:

- **Compiling every environment and letting TeX complain.** Means a new package works
  with no table entry, but an unloaded package or a typo produces a raw TeX error
  where cwiki could give a precise message for free, and stray prose containing
  `\begin{` would start a compile.
- **Compiling unknown environments with edit-distance typo detection.** Gets both
  "just works" and a good message, but it compiles things known to be unavailable,
  which is work guaranteed to fail.
- **Gating on a fixed shipped table alone.** Would make every new package a
  maintenance task on cwiki rather than a one-line user declaration.

---

## 3. Data safety and Git synchronization

### 3.1 Durable writes

**R3.1.1** Saving a note writes a temporary file in the note's directory,
preserves the relevant permissions, flushes and `fsync`s the file, atomically
renames it over the destination, then `fsync`s the containing directory. The
same-directory temporary file keeps the rename on one filesystem.

**R3.1.2** A failure before the atomic rename leaves the original file and
dirty buffer intact and reports the failed operation. If the rename succeeds
but the containing-directory `fsync` fails, cwiki reports **durability
uncertain** and keeps the buffer dirty; the pathname contains either the
complete old file or the complete new file, never a partial file. cwiki never
truncates the destination before a complete replacement has been written and
file-synced.

Rejected:

- **Atomic rename without `fsync`.** Protects against partial content but can
  report success for data that a power loss removes.
- **Configurable durability.** Avoids synchronization cost for users willing to
  lose recent saves, but makes the meaning of a successful save conditional.
- **Requiring the old pathname after a post-rename directory-sync failure.**
  Once atomic rename has succeeded, restoring the old pathname requires another
  rename and directory sync that can fail for the same reason; no portable
  implementation can guarantee rollback under continuing filesystem failure.
- **Treating successful rename as save success when directory sync fails.**
  Reports durability the system did not establish and permits the user to close
  a buffer whose latest pathname update may disappear after power loss.
- **Retaining a backup and attempting rollback.** Adds a second replacement
  transaction but cannot strengthen the guarantee beyond complete-old-or-new,
  because rollback and its directory sync may also fail.

### 3.2 External changes and conflicts

**R3.2.1** cwiki detects when Git or another process changes an open note. A
clean buffer reloads automatically while preserving each window's cursor and
view as closely as the changed text permits.

**R3.2.2** An external reload starts a new active undo tree under R1.2.7. The
archived old tree is recovery material only, so normal undo cannot restore and
later overwrite pre-pull content.

**R3.2.3** If the buffer is dirty, cwiki never overwrites it or the new disk
content. It starts a three-way merge using the last-read content as the base,
the in-memory buffer as ours, and the changed disk file as theirs.

**R3.2.4** Conflicts open in a dedicated merge view with base/ours/theirs hunks
and explicit choose or edit actions. Conflicted regions are not parsed or
rendered as note content. Existing Git conflict markers on disk enter this same
view rather than the normal editor.

**R3.2.5** cwiki does not write a partially resolved note. Every conflict hunk
must be resolved before the durable save in §3.1 replaces the conflicted file.

Rejected:

- **Always prompt before reloading a clean buffer.** Safer-looking but adds a
  modal interruption when there is no local work to protect.
- **Treat conflict markers as ordinary note text.** Simple, but lets invalid
  Markdown/LaTeX enter rendering and makes an accidental partial resolution
  easy to save.
- **Make the external reload an ordinary undo step.** Convenient, but normal
  undo could silently restore stale pre-pull content and later overwrite the
  integrated file.

### 3.3 Explicit Git synchronization

**R3.3.1** cwiki provides an explicit user-invoked Sync action. It does not
silently synchronize in the background.

**R3.3.2** Sync durably saves eligible buffers, commits authored vault changes,
fetches the configured remote, rebases the new unpushed local sync commit onto
the fetched branch, then pushes. It never rewrites a commit already pushed.

**R3.3.3** A dirty conflicted buffer, failed save, commit failure, fetch failure,
rebase conflict, or push failure stops Sync and remains visibly actionable.
Rebase conflicts use the §3.2 merge view; continuing Sync requires every hunk
to be resolved. No failure discards local work.

Rejected:

- **Automatic background synchronization.** Reduces manual work but introduces
  network, commit, and merge transitions while the user may be taking notes.
- **External Git only.** Keeps cwiki smaller but cannot integrate dirty-buffer
  protection and the conflict UI into the synchronization workflow.
- **Merge commits for divergence.** Preserve topology without rewriting local
  commits, but add routine merge noise to a personal vault. Rebase is limited
  to the new unpushed sync commit.

---

## 4. Index and search

### 4.1 Index storage and lifecycle

**R4.1.1** Each vault has one SQLite index outside the vault and Git, keyed by
a stable hash of the vault's canonical path:

| Platform | Location |
|---|---|
| Linux, FreeBSD | `$XDG_CACHE_HOME/cwiki/index/`, default `~/.cache/cwiki/index/` |
| macOS | `~/Library/Caches/cwiki/index/` |

Moving a vault may create a new cache entry; the index is disposable and fully
rebuildable, so this loses no authored state.

**R4.1.2** SQLite is a packaged dependency on macOS, Arch Linux, and FreeBSD.
cwiki uses its C library in-process with ordinary tables and transactions; index
correctness does not depend on an optional SQLite extension or a server.

**R4.1.3** The database stores an index schema version and, for each source
file, its path, size, modification time, and content hash. Startup compares
cheap metadata first, hashes changed candidates, and updates changed and
deleted records in one transaction.

**R4.1.4** cwiki watches the vault while running and applies external changes
transactionally. Queries see the last complete snapshot while an update is in
progress, never a partly updated index.

**R4.1.5** A schema change, failed integrity check, impossible snapshot, or
other validation failure triggers a full rebuild. The old database may be kept
for diagnostics, but cwiki never treats a known-invalid index as authoritative.
The user may also request a full rebuild.

Rejected:

- **Custom binary index.** Can reduce installed footprint, but makes cwiki own
  transactions, crash recovery, schema migration, and corruption handling.
- **In-memory rebuild on every launch.** Removes a dependency and stale-index
  states, but makes startup scale with the entire vault and discards useful
  derived state after every exit.
- **Trusting timestamps alone.** Fast, but misses content changes when metadata
  is preserved or has insufficient resolution.

### 4.2 Indexed content

**R4.2.1** The shared index contains searchable note text and structured records
for note titles, aliases, headings, links and backlinks, frontmatter properties,
comment TODOs, tasks, flashcards and derived review state, and events.

**R4.2.2** Code and LaTeX source remain full-text searchable, but tokens inside
their zones do not create false headings, links, tasks, or other structured
records. Comment TODOs follow the zone rule in R1.9.8.

**R4.2.3** Binary attachments contribute filename, path, media type, and authored
metadata only. Content extraction and OCR are separate deferred features, not
implicit indexing behavior.

**R4.2.4** Rebuildable values such as folded flashcard state retain their source
watermark, as required by R2.4.6. The authored files remain authoritative when
an indexed value disagrees.

### 4.3 Query language

**R4.3.1** One query language is shared by note search, task aggregation,
flashcard browsing, event search, and later project views. A view may add an
implicit type filter but does not define a second language.

**R4.3.2** Plain terms use implicit `AND`. The language supports quoted phrases,
parentheses, explicit `OR`, unary `-` negation, and typed filters such as
`type:task`, `path:physics`, `due:<2026-10-01`, and `field:Deck`.

**R4.3.3** Regex is opt-in through an explicit `re:/…/` form and uses the same
bounded PCRE2 execution policy as R1.11.8–R1.11.10. Invalid, timed-out, or
resource-limited regexes produce a visible query error rather than partial or
silently wrong results.

**R4.3.4** Plain-text search uses smart case: it is case-insensitive when the
query has no uppercase letter and case-sensitive when it does. Filter names are
case-insensitive; filter values use the semantics of their fields. Link
resolution remains separately case-sensitive under R2.5.

Rejected:

- **Text and phrase search only.** Simple, but cannot express the task, card,
  event, and property views already required.
- **SQL-like user queries.** Flexible, but exposes storage details, is noisy for
  interactive use, and couples saved queries to the index schema.
- **Implicit regex interpretation.** Makes punctuation-heavy class notes
  surprising and exposes every search to regex cost and failure modes.
- **Extracting every attachment.** Adds format-specific dependencies and OCR
  policy before a demonstrated need.

---

## 5. Time, recurrence, and reminders

### 5.1 Recurrence model

**R5.1.1** Calendar recurrence is stored using RFC 5545 `RRULE`, `RDATE`, and
`EXDATE`. The required initial `RRULE` fields are `FREQ`, `INTERVAL`, `BYDAY`,
`BYMONTHDAY`, `COUNT`, and `UNTIL`. cwiki provides a friendly editor over these
fields rather than inventing a second storage language.

**R5.1.2** Calendar recurrence is schedule-relative. Overrides and exclusions
identify individual generated occurrences without copying the whole recurring
definition into each day file.

**R5.1.3** Recurring tasks have an explicit `repeat-from:schedule` or
`repeat-from:completion` mode. Schedule-relative tasks remain anchored to their
rule; completion-relative tasks calculate the next due time from actual
completion.

**R5.1.4** Multiple missed schedule-relative task occurrences collapse into one
overdue occurrence that shows how many were missed. Completing it records that
count and advances to the first future occurrence. cwiki does not generate an
unbounded backlog of overdue copies.

Rejected:

- **A custom recurrence grammar.** Could be smaller, but creates a conversion
  boundary for calendar interoperability and duplicates a mature standard.
- **A Remind-style expression language.** More expressive, but adds an embedded
  language for needs covered by RRULE plus explicit task modes.
- **Generating every missed task occurrence.** Faithful to the schedule, but a
  dormant daily task can flood the active list with hundreds of copies.

### 5.2 Time zones and daylight saving

**R5.2.1** Timed events and tasks store local wall time with an IANA time-zone
identifier. A vault default may fill the field during creation, but authored
items do not depend on the current machine's local zone. All-day items are
date-only.

**R5.2.2** Recurrence preserves local wall time across offset changes. “Tuesday
09:00 America/Toronto” remains at 09:00 rather than preserving a UTC instant
and drifting by an hour.

**R5.2.3** If a generated wall time does not exist during a DST gap, cwiki moves
it forward by the size of the gap and marks that occurrence adjusted. If the
wall time occurs twice during a fold, cwiki selects the earlier occurrence
unless that instance has an explicit override.

Rejected:

- **UTC-instant recurrence.** Simple arithmetic, but recurring local events
  drift after daylight-saving transitions.
- **Prompting on every DST anomaly.** Gives control but makes unattended
  expansion, reminders, and calendar views nondeterministic.

### 5.3 Reminders

**R5.3.1** While cwiki runs, due reminders use OSC 99. Advance-warning deltas
and repeat intervals are properties of the event or task.

**R5.3.2** Reminders while cwiki is closed are provided by an optional companion
daemon. It is enabled per machine in non-synchronized configuration and is off
by default. The normal multi-machine setup enables one notifier to prevent
duplicate desktop notifications.

**R5.3.3** The TUI and local daemon coordinate so exactly one local notification
fires when both are running. Notification state is machine-local and
rebuildable; it does not become authored vault data.

Rejected:

- **Notifications only while the TUI runs.** Cannot cover scheduled reminders
  when cwiki is closed.
- **A daemon enabled automatically on every machine.** Requires distributed
  notification claiming or produces duplicates from a Git-synchronized vault.

### 5.4 Calendar interoperability

**R5.4.1** Core calendar interoperability is standards-based iCalendar export,
with stable UIDs and mappings for `VEVENT` and `VTODO`.

**R5.4.2** Two-way CalDAV is a much later optional companion feature. It is not
part of the first calendar implementation and does not shape the core vault or
sync architecture beyond the stable iCalendar identities in R5.4.1.

Rejected:

- **CalDAV in the first calendar release.** Phone integration is useful, but
  remote authentication, conflict handling, and provider differences would
  delay the local time-blocking workflow.
- **No interoperability.** Would unnecessarily trap events and tasks in cwiki
  despite a standard export boundary.

---

## 6. Terminal rendering

### 6.1 TeX and image pipeline

**R6.1.1** LuaLaTeX is the default engine for native Unicode, modern font
handling, and broad package compatibility. A note may explicitly select
pdfLaTeX in frontmatter for compatibility; cwiki never guesses or silently
switches engines.

**R6.1.2** Math, mhchem, and TikZ all use one PDF pipeline. A note render
produces tightly bounded PDF pages for its changed blocks; cwiki does not
maintain a separate DVI/dvipng path for simpler formulas.

**R6.1.3** Packaged Poppler `pdftocairo` renders each page directly to a
transparent PNG at the terminal cell's pixel density. cwiki transfers the PNG
through kitty shared memory and places it with Unicode placeholders and the
required z-index.

**R6.1.4** A render-cache key includes source content, engine, effective
preamble and packages, terminal foreground/background colors, cell pixel
metrics, and render scale. A changed input cannot reuse a visually incompatible
artifact.

Rejected:

- **pdfLaTeX as the default.** Faster startup, but weaker native Unicode and
  font handling for a multilingual plain-text vault.
- **Automatic engine detection.** Convenient until a heuristic changes or an
  ambiguous package makes the same note compile differently across machines.
- **dvipng for simple formulas plus PDF for TikZ.** Can make some formulas
  faster, but doubles compilation, sizing, diagnostics, and cache paths.
- **MuPDF `mutool` as the rasterizer.** Viable, but Poppler's direct transparent
  PDF-to-PNG path is the selected packaged dependency.

### 6.2 Render timing and editing feedback

**R6.2.1** cwiki renders only saved on-disk content, immediately after a write.
It never compiles unwritten buffer content on an idle timer. This preserves
R1.12.6: a rendered view shows what Git can synchronize.

**R6.2.2** Switching to rendered mode is always immediate. It shows the newest
valid prior artifact or a placeholder while the saved version renders in the
background; it never waits for TeX or displays a half-written artifact.

**R6.2.3** Editing mode retains the conceal behavior in §1.6 and may show gutter
and status diagnostics from the latest saved render. If the buffer no longer
matches that render's content hash, those diagnostics are visibly stale. No
formula image is embedded in editing mode.

Rejected:

- **Debounced rendering of unwritten content.** Makes switching more likely to
  be warm, but creates a live-preview pipeline and an artifact that differs
  from the file Git will synchronize.
- **Manual rendering only.** Predictable, but makes every ordinary write require
  another action before the rendered view catches up.
- **Inline formula images in editing mode.** Blurs the source/rendered-mode
  boundary and complicates source-accurate cursor motion.

### 6.3 Layout and navigation

**R6.3.1** An inline formula taller than one terminal row expands its rendered
line to enough whole cell rows for the image. Its TeX baseline aligns with the
surrounding text baseline; extra rows above and below belong to that line box.
cwiki neither shrinks it to one row nor silently promotes it to display math.

**R6.3.2** Rendered Markdown headings use kitty OSC 66 with a small fixed scale
table by heading level. Layout measures the scaled text. Editing-mode headings
remain normal-sized source text.

**R6.3.3** Rendered mode provides viewer-style Vim row and page scrolling, `/`
search over rendered text with `n`/`N`, Tab and Shift-Tab link focus, Enter to
follow, and a back stack. It remains a viewer without an insertion cursor;
source navigation uses the inverse-sync action in §1.8.

Rejected:

- **Scaling every inline formula to one row.** Preserves a simple grid but makes
  fractions, matrices, and chemistry unreadably small.
- **Promoting tall inline formulas to display blocks.** Changes authored flow
  and can substantially alter paragraph layout.
- **Color or weight only for headings.** Simpler, but leaves an already-required
  kitty text-sizing capability unused in the view intended to be typeset.
- **A normal-mode cursor over rendered content.** Adds editing semantics to a
  viewer and conflicts with per-window editing/rendered separation.

### 6.4 Shell-escape policy

**R6.4.1** TeX runs with `-no-shell-escape` by default. Full shell escape may be
enabled only for an explicitly trusted vault in machine-local, non-synchronized
configuration after a warning.

**R6.4.2** Note frontmatter and synchronized vault configuration cannot enable
shell escape. A note received through Git must never gain command-execution
authority merely by being opened or rendered.

Rejected:

- **No override.** Safest, but permanently excludes packages whose legitimate
  workflows require an external command.
- **A per-note or synced-vault switch.** Convenient, but turns pulled note text
  into arbitrary command execution.

### 6.5 TeX diagnostics

**R6.5.1** The initial TeX-log parser is stateful and structures errors and
warnings with included-file attribution, source line and context, and
continuation lines. Configurable regex filters may hide known-noisy diagnostics.

**R6.5.2** Unclassified output remains available in the raw log. cwiki does not
need exhaustive package-specific classification before shipping useful inline
errors.

**R6.5.3** A failed block displays its structured error in place of the formula;
other successfully rendered blocks from the same note remain usable.

Rejected:

- **Fatal errors only.** Misses actionable warnings and the context needed to
  locate many TeX failures.
- **Exhaustive package-specific parsing.** Unbounded scope; raw-log access is the
  fallback for messages the structured parser does not know.

---

## 7. Configuration and extension boundary

### 7.1 Format and validation

**R7.1.1** cwiki configuration is declarative YAML using the same packaged YAML
parser as note frontmatter. Configuration contains data only; it does not
execute code.

**R7.1.2** Every configuration object has a strict schema. Unknown keys,
duplicate keys, wrong types, invalid action names, and conflicting keybindings
produce source-located errors rather than being ignored.

**R7.1.3** Configuration loading is transactional. cwiki validates and composes
the complete candidate configuration before replacing the active one. A failed
reload leaves the previous valid configuration active.

Rejected:

- **TOML.** A viable declarative format, but would add a second parser beside
  the YAML already required for note frontmatter.
- **Embedded Lua.** Provides Neovim-level programmable configuration only by
  committing cwiki to a scripting runtime and stable public API across every
  subsystem.

### 7.2 Scopes and precedence

**R7.2.1** Configuration composes in this order, with later permitted values
overriding earlier ones:

1. built-in defaults;
2. global user configuration in the OS configuration directory;
3. synchronized vault configuration under the vault's hidden cwiki directory;
4. machine-local vault overrides in the OS configuration directory, keyed by
   the vault identity;
5. note frontmatter for note-scoped keys;
6. ephemeral per-window runtime toggles.

**R7.2.2** Each schema key declares which scopes may set it. Security-sensitive
values, including trusted-vault shell escape and local reminder-daemon state,
are machine-local and cannot be enabled by synchronized files or note
frontmatter.

**R7.2.3** The state-dump/config-inspection command shows every effective value
and the scope and source location that supplied it.

Rejected:

- **One global file plus frontmatter.** Cannot share vault behavior through Git
  or express machine-local exceptions safely.
- **One self-contained synchronized vault configuration.** Lets a pulled file
  alter machine trust and host integrations.

### 7.3 Declarative surface

**R7.3.1** The schema covers keymaps and clue groups, snippets and named
transforms, themes, conceal and display options, parser and index rule tables,
package and environment tables, save/render/index policies, and workflow
defaults.

**R7.3.2** Every bindable operation remains a named action under R1.14.1.
Configuration may bind, unbind, group, label, or parameterize exposed actions;
it cannot inject a new implementation.

**R7.3.3** Core file-format meanings, synchronization and data-safety rules,
resource limits, and requirement semantics are not configurable. A setting
cannot weaken the guarantees in §§3–6.

### 7.4 Extensions and integrations

**R7.4.1** cwiki initially has no embedded scripting language, public plugin
API, ABI, or community plugin ecosystem. New behavior is implemented in cwiki;
user customization composes the declarative primitives in §7.3.

**R7.4.2** The action/event dispatch used internally is not a public lifecycle
hook API. Configuration does not attach arbitrary commands to initialization,
render, mode-switch, save, or quit events.

**R7.4.3** External commands are exposed only through specific reviewed
integration slots required by an approved feature, such as the notification
command. Each slot defines its arguments, environment, failure behavior, and
security boundary; there is no generic shell-hook escape hatch.

Rejected:

- **Generic lifecycle shell hooks.** Appear simpler than scripting, but create
  an unstable and unsafe shell-based plugin API with poor data exchange.
- **A public event API without scripting.** Commits cwiki to compatibility
  constraints before there is an extension consumer or execution model.
- **A community plugin ecosystem.** Requires API stability, distribution,
  compatibility, and security work outside the product priorities.

---

## 8. Study and planning workflows

### 8.1 Flashcard authoring and review

**R8.1.1** Flashcards are created and maintained only through a dedicated deck
browser. Ordinary note editing has no create-card action and no embedded card
syntax. The browser creates the separate card-note files specified by §2.2.

**R8.1.2** Card creation selects a deck, note type, subject, and optional source
note, then edits named fields with the normal modal editor and render pipeline.
The source picker writes the optional R2.2.4 wikilink.

**R8.1.3** Note types are synchronized declarative YAML definitions with named
fields and one or more Markdown front/back templates. Templates support
`{{Field}}` substitution, simple field-presence conditionals, and built-in
cloze expansion. They use cwiki's Markdown/TeX renderer and theme; there is no
HTML/CSS or template scripting runtime.

**R8.1.4** The deck browser shows the deck/subdeck hierarchy with new, learning,
and due counts. Reviewing a deck includes its subdecks by default.

**R8.1.5** The default review queue presents learning and relearning cards,
then due reviews, with new cards interleaved according to the deck setting.
Again, Hard, Good, and Easy show their next intervals. Daily new and review
limits apply per deck, and sibling cards from one note are buried until the
next day by default.

**R8.1.6** Review remains keyboard-driven and supports reveal, grade, undo the
last grade, edit and return, suspend, bury, and mark. Due cards are pre-rendered
under the existing rendering requirement.

**R8.1.7** Custom Study creates a temporary query-backed queue for cram,
catch-up, or other saved-query criteria. It neither moves cards nor changes
their home decks.

Rejected:

- **Creating cards from a note selection.** Faster while reading notes, but
  mixes card authoring into the note workflow; all creation belongs in the deck
  browser.
- **Cards embedded in notes.** Conflicts with the separate-file data model and
  couples prose edits to card identity.
- **Anki-compatible HTML/CSS templates.** Would require a second renderer and a
  web styling model in the terminal application.
- **One global queue with no deck selection.** Removes deck navigation but loses
  course-specific review limits and intentional study sessions.

### 8.2 Calendar interaction

**R8.2.1** The primary calendar planning surface is a keyboard-driven week time
grid with day columns, a movable time-slot cursor, duration-aware event and task
blocks, and overlap layout. Month view navigates dates; day view provides a
wider detailed schedule.

**R8.2.2** Enter on a grid slot opens the shared structured event form prefilled
with that start time. The form covers title, date and time, duration, zone,
location, recurrence, and reminders. Keyboard actions move and resize existing
blocks.

**R8.2.3** Every view uses the same event form. A compact deterministic
quick-add syntax may prefill it; free-form natural-language date guessing is a
later optional feature, not part of the first calendar workflow.

**R8.2.4** Editing or deleting a recurring occurrence offers: this occurrence,
this and future occurrences, or the whole series. A single-instance change uses
an override or exclusion; this-and-future splits the series while retaining its
stable relationship for export.

Rejected:

- **Month or agenda view as the primary surface.** Useful for overview, but not
  the time-blocking workflow that calendar priority requires.
- **Free-form natural language in the first release.** Convenient when guessed
  correctly, but inherently ambiguous and unnecessary beside a slot-prefilled
  form and deterministic shorthand.
- **Editing day Markdown for every event.** Keeps the plain-file representation
  visible but makes routine time-block manipulation unnecessarily textual.

### 8.3 Task dashboard and time blocks

**R8.3.1** The main task screen is a query-backed dashboard with Inbox, Overdue,
Today, Upcoming, and Someday sections. Each item shows its source note, status,
priority, due date, and scheduled blocks.

**R8.3.2** Completing, editing, or rescheduling an inline task updates its source
note through the durable-write path. A dashboard result is a view of the source
task, not a second task record.

**R8.3.3** A task may have zero or more calendar work blocks. Each block
references a stable task ID; an inline task receives an ID only when another
record first needs to reference it. A due date remains independent of planned
work times.

**R8.3.4** Task blocks appear in calendar views but are projections of the task,
not copied events. Moving or resizing one updates that block's schedule.
Completing a task asks before cancelling any future incomplete work blocks.

Rejected:

- **A flat priority list.** Hides the distinction between overdue, actionable
  today, and unscheduled inbox work.
- **Tasks managed only in source notes.** Cannot provide the cross-vault daily
  planning workflow.
- **At most one block per task.** Prevents splitting substantial work across
  several study sessions.
- **Copied calendar events.** Creates two independently editable records that
  can drift apart.

### 8.4 Projects

**R8.4.1** A project is a project note with a stable ID, status, intended
outcome, dates, and optional parent. Notes, tasks, events, and flashcard decks
join it through an explicit `project` property. Links and backlinks provide
context but do not imply project membership.

**R8.4.2** A project home combines the project note's prose with configurable
declarative sections for outcome, next actions, task groups, upcoming events
and work blocks, linked notes, and deck/study status. Sections are queries over
the shared index rather than duplicated data.

**R8.4.3** List and calendar sections ship before an optional Kanban view.
Kanban is a later presentation over the same project/task records and does not
define project storage or membership.

Rejected:

- **Folder equals project.** Simple, but a note, task, event, or deck may belong
  to a project without sharing one directory hierarchy.
- **Backlinks imply membership.** Conflates reference context with an explicit
  planning relationship.
- **Kanban as the project model.** Makes one optional view dictate storage and
  delays the more important list and calendar workflows.

---

## 9. Interaction details

### 9.1 Vault and note browsing

**R9.1.1** cwiki has one indexed Notes browser rather than a generic filesystem
explorer. It supports fuzzy and list navigation by title, path, and properties,
an optional note preview, and directory grouping. It reuses the shared picker
and index instead of implementing a second file-navigation system.

**R9.1.2** Vault selection follows this precedence: an explicit CLI path; the
nearest ancestor containing the `.cwiki/` vault marker; a configured default;
then a vault picker. An arbitrary Git repository is not automatically a vault.

Rejected:

- **Generic filesystem explorer.** Duplicates picker behavior and exposes file
  operations unrelated to a note vault.
- **Fuzzy picker only.** Fast when the target is known, but lacks the browsable
  hierarchy and preview useful for discovery.
- **Nearest Git repository as vault.** Can mistake source repositories or nested
  projects for cwiki data.

### 9.2 Git surfaces

**R9.2.1** In addition to Sync, cwiki shows vault Git status, diffs of authored
files, recent history, and conflict or retry details. These surfaces explain
and support the §3.3 workflow.

**R9.2.2** cwiki is not a general Git client. It does not expose arbitrary
staging, branch management, rebases, or hand-authored commits outside the
defined vault Sync operation.

Rejected:

- **A full in-app Git client.** Adds broad porcelain UI unrelated to the
  constrained personal-vault synchronization workflow.
- **An opaque Sync command.** Leaves the user unable to inspect pending changes
  or understand a stopped operation.

### 9.3 Terminal theme and input

**R9.3.1** A color theme may define light and dark variants. cwiki queries
kitty's background at startup and when terminal focus returns, and switches the
complete variant transactionally when it changes. A machine-local setting may
override automatic selection.

**R9.3.2** cwiki is keyboard-only and does not enable or interpret terminal
mouse reporting. Every action is available through keymaps, clue popups,
pickers, or commands. Kitty may still provide terminal-owned behavior such as
text selection and opening OSC 8 links.

**R9.3.3** Editing mode does not draw an active indentation-scope guide. Syntax
highlighting, delimiter matching, and contextual zones provide structure
without adding a code-oriented vertical guide to prose and LaTeX.

Rejected:

- **Startup-only color detection.** Does not follow a terminal theme changed
  while cwiki remains open.
- **Basic or full mouse handling.** Creates a second interaction path contrary
  to the established keyboard-first workflow.
- **Indent guides only in code fences.** A bounded variant, but still low value
  for the short embedded code that cwiki prioritizes.

### 9.4 Defaults and excluded interaction models

**R9.4.1** cwiki ships a curated, fully overridable default snippet library:
inline and display math shells; fractions; superscript and subscript patterns;
Greek letters and common operators; postfix accents and decorations; limits,
sums, integrals, matrices, and environments; plus separate mhchem and TikZ
sets. Riskier auto-expansions are context-gated and individually disableable.

**R9.4.2** cwiki has no note-level threaded annotation, reply, or resolve data
model. Private annotations use ordinary note content or comment blocks.

Rejected:

- **No default snippets.** Makes the priority class-note workflow require a
  large configuration effort before first use.
- **Only delimiters and Greek letters.** Avoids opinionated defaults but omits
  the high-value structural snippets that motivated the engine.
- **Threaded annotation workflow.** Adds collaboration state to a solo personal
  vault where comment blocks already cover private notes.

---

## Open questions

All implementation-blocking architecture and first-milestone questions from
the interview are settled. The remaining questions below are intentionally
deferred to the milestone that first needs them.

### Deferred, each tagged to the milestone that must answer it

| Question | Milestone |
|---|---|
| Whether SyncTeX sidecars ship with the first render milestone or the second (R1.8.11 makes this scheduling, not architecture) | rendering |
| Per-row stale-render marking in the sync sidebar (R1.12.9, off by default) | rendering |
| Named sessions beyond the implicit one (R1.12.11) | after the editor core is stable |
| Change list `g;`/`g,` alongside the jump list — kept only if cheap once the jump list exists | editor core |
