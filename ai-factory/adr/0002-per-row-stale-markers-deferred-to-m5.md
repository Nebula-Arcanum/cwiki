# 0002 — Per-row stale-render markers are deferred to Milestone 5

Date: 2026-10-11 · Status: accepted

## Context
`docs/SPEC.md` R1.12.9 has two parts: a status-line indicator on every rendered view of a note
whose buffer differs from disk (`[stale: 3 blocks]`), and an optional, off-by-default extension in
which the sync sidebar marks the specific rendered rows whose source changed, "which needs
per-block dirty tracking mapped back to rendered rows". The spec's Open questions table and
`docs/ROADMAP.md` ("Decisions due before coding", Milestone 2) leave the schedule of the second
part open. Milestone 2 is XL (20–28 tasks) and grew by the SyncTeX sidecar work in ADR 0001; the
per-row markers are off by default, so no default workflow depends on them, while the status-line
count already tells the user that a rendered view is stale.

## Decision
The per-row stale-render markers of R1.12.9 are out of Milestone 2 and deferred to Milestone 5,
where they remain optional and off by default; the status-line stale indicator stays in
Milestone 2.

## Consequences
Easier:
- Milestone 2 carries no dirty-block → rendered-row projection and no second kind of mark in the
  sync sidebar gutter beside the R1.8.9 reticle; its sidebar has one job.
- Milestone 2's acceptance criteria and demo are unchanged ("non-blocking stale/placeholder
  behavior" is met by the status-line count).
- R1.12.9 itself is unchanged: the behaviour, its optionality and its default are as specified.
  Only the delivery milestone is settled.

Harder, and what must now be done:
- From Milestone 2 until Milestone 5 a stale split view says how many blocks changed but not
  which rows; the user finds them by reading, or by writing the note so the view re-renders.
- Milestone 5, already the largest milestone (30–42 tasks), takes on a rendering-side item that
  touches Milestone 2 code: the sidebar gutter and the per-window layout tables (R1.8.10) are
  reopened three milestones after they were written, by a milestone otherwise about editing.
- Milestone 2 must therefore not close the door: the per-block change detection behind the
  status-line count and the rendered row → source line range table have to stay addressable per
  block, so Milestone 5 adds a projection rather than reworking the layout pass. That is a design
  constraint on Milestone 2 with no Milestone 2 test to enforce it.
- The sidebar's configuration (R1.8.9, presence configurable) gains its stale-marker switch only
  in Milestone 5, so the configuration schema for the sidebar changes after first release.

Decided by the user on 2026-10-11 and recorded in `docs/SPEC.md` (R1.12.9a; Open questions row
removed) and `docs/ROADMAP.md` (Milestone 2 decisions, Milestone 5 scope).
