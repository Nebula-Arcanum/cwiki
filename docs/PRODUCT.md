# cwiki product context

## Product

cwiki is a personal wiki and productivity TUI written in C. It has its own
modal text editor; it does not embed Neovim or hand editing to `$EDITOR`.
Editing mode shows source and rendered mode shows the typeset note. Rendering
does not update continuously while the user types.

The user makes product decisions and reviews behavior through demos and test
output. The agent writes the implementation.

## Workflow priorities

In priority order, cwiki supports:

1. Fast class notes for chemistry, physics, calculus, and computer science,
   including code blocks and quickly entered LaTeX and mhchem through snippets.
2. Detailed linked topic pages, including TikZ diagrams.
3. Anki-like flashcards and spaced-repetition study.
4. Event planning in a time-blocking calendar.
5. Tasks with due dates, priorities, recurrence, and scheduled time blocks.
6. Projects built from notes, tasks, and events.

## Platform boundary

- Supported systems are macOS, Linux, and FreeBSD.
- Kitty is the sole terminal target. cwiki does not run inside tmux or over
  SSH, although Git may use SSH remotes.
- cwiki uses the kitty graphics and keyboard protocols, synchronized updates,
  OSC 52 clipboard, OSC 8 hyperlinks, and OSC 99 notifications.
- Required terminal capabilities are queried at startup rather than inferred
  from `$TERM`. Missing required capabilities produce a clear exit message.
- `docs/SPEC.md` pins the minimum kitty version. The floor must remain available
  in FreeBSD packages, Arch repositories, and Homebrew; runtime capability
  queries remain authoritative.
- Dependencies must be packaged for all three systems. Document any dependency
  requiring FreeBSD ports, the AUR, or a non-core Homebrew tap.
- Builds use packaged tools and must not assume GNU make.

Package availability and version floors are time-sensitive and must be
revalidated before implementation depends on them.

## Data and synchronization

The user's notes, flashcards, review history, tasks, and events live in a vault
that is separate from this source repository and synchronized through Git.
Authored data stays in plain files. Rebuildable indexes, caches, undo data, and
recovery journals stay outside the vault.

Tests and early demos use fixture vaults or a copy of the user's vault. The real
vault remains off limits until the data-safety milestone has passed its demo.

## TeX rendering commitments

- A vault-wide preamble is synchronized through Git and precompiled per
  machine.
- Notes may add packages or preamble content for uncommon needs.
- Render caches and compiled preambles live in the OS cache directory and warm
  in the background after a pull.
- Rendering is asynchronous and groups changed blocks into one TeX run per
  note. Switching to rendered mode never waits for it.
- Due flashcards are pre-rendered before review.
- LaTeX errors appear in place of failed formulas.
- Images match the terminal font size and colors.
- Images use kitty z-index, Unicode placeholders, and shared-memory transfer.

Rendered mode should appear immediately. A typical note's formulas should fill
in within about one second; a first render of a TikZ-heavy note may take a few
seconds.

## Scope boundaries

The accepted inventory in `docs/FEATURES.md` excludes GUI and mobile clients,
embedded Neovim, external-editor handoff, a community plugin ecosystem,
proprietary synchronization, a hosted web backend, terminal panes, LSP and
formatter integration, graph view, and tags as the primary organization model.
Deferred ideas remain deferred unless the user explicitly promotes them.

## Provenance

The initial research, feature triage, and completed interview were migrated
from `Nebula-Arcanum/oldwiki`. `docs/SPEC.md`, `docs/FEATURES.md`, and
`research/` preserve that work. Tool-specific phase commands, transcript
helpers, hooks, model choices, and slash commands from the previous workflow
are intentionally not part of cwiki's workflow.
