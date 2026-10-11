# 0001 — SyncTeX sidecars ship in Milestone 2

Date: 2026-10-11 · Status: accepted

## Context
`docs/SPEC.md` settles the SyncTeX architecture (R1.8.11–R1.8.15) but defers its scheduling: the
Open questions table and `docs/ROADMAP.md` ("Decisions due before coding", Milestone 2) ask
whether sidecars ship with the first render milestone or with Milestone 5. Milestone 2 already
owns line-granular source↔render synchronization and the sync marker (R1.8.9, R1.8.10); without
sidecars a multi-line block maps only as a whole, so on a long derivation the marker names a line
up to a block's length away from what is on screen — the case SPEC's rejected "No SyncTeX at all"
alternative calls out. Against that, Milestone 2 is already XL (18–25 tasks) with asynchronous
ownership, TeX log attribution and image layout as its listed risks.

## Decision
SyncTeX sidecars ship in Milestone 2, exactly as R1.8.11–R1.8.15 specify: multi-line blocks only,
`-synctex=-1` uncompressed, cwiki's own minimal reader, sidecar stored in the render cache beside
the image.

## Consequences
Easier:
- The render-cache entry format is designed once, with the sidecar and the temp-line→note-line
  table (R1.8.14) in it from the start; deferring to Milestone 5 would mean a cache-format change
  and a cache invalidation after the fact.
- Milestone 2's acceptance criterion "source and inverse jumps land on the specified
  line/fallback" is met at line granularity inside multi-line blocks, not only at block granularity.
- No new dependency: the uncompressed side file needs no zlib, and the reader is cwiki's own code.

Harder, and what must now be done:
- Milestone 2 grows by the sidecar reader, its bounded in-memory parse cache (R1.8.13) and its
  fuzz target (R1.8.12), on the milestone that is already the largest risk after Milestone 1.
  Its effort range in `docs/ROADMAP.md` should be re-estimated rather than assumed unchanged.
- A second, untrusted-format parser enters the build one milestone early: it must pass the strict
  build, clang-tidy and the sanitizers, and joins every later combined check.
- The "SyncTeX fuzz targets pass where applicable" clause of Milestone 2's acceptance criteria
  becomes unconditional.
- The degrade-to-scrolling path (R1.8.18) must exist in Milestone 2 for an unparseable or missing
  sidecar; it cannot be postponed with the feature.
- Render-cache entries for multi-line blocks carry an extra uncompressed file, and the compile
  invocation differs by block line count.
- Milestone 5 loses this as schedule slack; anything cut from Milestone 2 under pressure has to
  be something else.

Accepted by the user on 2026-10-11 and recorded in `docs/SPEC.md` (R1.8.11a; Open questions row
removed) and `docs/ROADMAP.md` (Milestone 2 scope, acceptance criteria, effort and decisions due).
