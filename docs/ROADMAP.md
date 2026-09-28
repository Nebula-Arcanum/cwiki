# cwiki roadmap

Status: **not yet planned; implementation is blocked pending explicit
specification approval.**

The follow-up interview is complete, implementation-blocking architecture gaps
are settled, and `docs/FEATURES.md` has no undecided rows. No product code or
milestone implementation has started. Next:

1. Ask: **Do you approve `docs/SPEC.md` for implementation?**
2. After approval, add a milestone column to `docs/FEATURES.md` and assign every
   kept or changed feature exactly once.

## Fixed sequencing constraints

### Milestone 0 — verification harness

This milestone contains the harness, not product features:

- Clean strict-warning builds on Arch Linux, macOS with Homebrew, and FreeBSD
  with packaged tools.
- ASan and UBSan test runs on all three; leak detection and clang-tidy on
  Linux.
- GitHub Actions on pushes and pull requests, with superseded runs cancelled
  and package downloads cached.
- Key recording and replay, screen snapshots, fixture vaults, and automated
  change review.
- Model-based randomized editor tests against a simple reference buffer.
- Parser, SyncTeX, and Vim-regex-translation fuzz tests where their components
  are introduced.

### Milestone 1 — daily class-note skeleton

The first usable vertical slice is editing mode with snippets and source
highlighting. Rendered mode begins in the following milestone.

## Later milestone rules

- Order work by dependency, then by the priorities in `PRODUCT.md`.
- Each milestone is a vertical slice with testable acceptance criteria, a
  user-runnable demo, dependencies, risks, and effort.
- Sequence essential features before optional ones and isolate expensive
  optional work in later milestones.
- Attach each deferred question in `SPEC.md` to the milestone that must answer
  it.
- Install only test-required TeX packages in CI and cache them.
- Treat estimates as focused implementation-task ranges, not model-specific
  sessions.
