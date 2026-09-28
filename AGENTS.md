# cwiki repository guidance

cwiki is a C terminal personal wiki. Read @docs/PRODUCT.md before product work
and @docs/SPEC.md before architecture or implementation. Use
@docs/FEATURES.md and @docs/ROADMAP.md when scoping or sequencing work.

## Sources of truth

- `docs/PRODUCT.md` records durable goals, priorities, and constraints.
- `docs/SPEC.md` records interviewed requirements, rejected alternatives, and
  open questions.
- `docs/FEATURES.md` preserves the user's feature decisions and their sources.
- `docs/ROADMAP.md` records milestone assignments and delivery status.
- `research/` contains supporting inventories, not requirements.
- Report contradictions between these sources. Do not silently choose one.

## Interview gate

Reuse the completed interview in `docs/SPEC.md`. Ask only targeted questions
needed to close a recorded gap, contradiction, or implementation-blocking
ambiguity; do not restart broad discovery.

Do not implement product features unless all relevant architecture questions
are settled and the user has answered **yes** to this exact question:

> Do you approve `docs/SPEC.md` for implementation?

Record accepted answers and rejected alternatives in `docs/SPEC.md`. Keep its
status accurate. Planning and interviews do not change product source code.

## Research

Prefer targeted repository, documentation, and web research. Put authored
summaries in `research/`. Clone an external source into ignored
`research/sources/` only when repeated deep inspection justifies keeping a
local copy.

## Orchestration

The top-level agent owns planning, shared interfaces, integration, and final
verification. Delegate only bounded, independent tasks. Every independent
worker edits a separate Git worktree and branch; workers never share a
checkout. Give each worker the exact requirements, files, ownership boundary,
and checks it needs. Workers commit locally and never push. The orchestrator
reviews and integrates each commit, then runs combined verification.

Keep overlapping or tightly coupled work in the top-level thread. Stabilize
shared interfaces before parallelizing dependent areas.

## Checkpoints and safety

- Automatically make a local checkpoint commit after each coherent planning or
  interview topic and after each verified implementation task.
- Stage explicit paths only. Never use `git add -A`, `git add .`, or
  `git commit -a`.
- Never bypass a failing hook or weaken a requirement or test to obtain a
  passing result.
- Do not push, merge, tag, deploy, publish, or modify the user's real vault
  without explicit approval.
- Use fixture vaults until the data-safety milestone has passed its user demo.

## Verification and review

The user reviews behavior through demos and test output rather than source
code. Each implementation milestone needs testable acceptance criteria, a
user-runnable demo, and actual check output. Inspect worker changes and run the
combined checks; a worker summary is not verification.
