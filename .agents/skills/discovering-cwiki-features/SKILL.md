---
name: discovering-cwiki-features
description: "Researches and interviews for cwiki features, updates product planning documents, and prepares the specification for approval. Use for feature discovery, architecture decisions, unresolved questions, feature triage, or roadmap planning."
---

# Discovering cwiki features

Run discovery without implementing product code.

## Start from existing decisions

1. Read `AGENTS.md`, `docs/PRODUCT.md`, `docs/SPEC.md`, and the relevant rows in
   `docs/FEATURES.md`.
2. Treat settled requirements and rejected alternatives as authoritative.
3. Ask only about a recorded gap, contradiction, stale feature status, or new
   requirement. Never restart the broad interview.
4. Call out contradictions directly instead of selecting an answer silently.

## Research only what is needed

Use targeted repository, documentation, or web research to answer a concrete
question. Prefer primary sources. Read the summaries in `research/` before
repeating research. Clone into ignored `research/sources/` only when repeated,
deep source inspection is necessary; record why the clone is needed and keep
the authored result in `research/<topic>.md`.

Independent research workers must use separate Git worktrees and branches.
Give each worker a disjoint question and requested evidence. Integrate only the
authored summary or committed planning change after reviewing it.

## Interview

- Keep the interview in the top-level thread, not in workers.
- Ask up to four related questions at a time.
- Put the recommended choice first with a one-line tradeoff.
- Use concrete options when there are two to five real choices; otherwise ask
  in plain text.
- Point out conflicts with prior answers or platform constraints.
- Record each accepted answer as a requirement and record rejected alternatives
  with reasons.

After each coherent topic, validate document references, stage only the files
changed for that topic, and make a local checkpoint commit. Do not push.

## Finish discovery

Prune resolved questions, reconcile feature-table statuses that the user has
settled, and keep deferred questions attached to the milestone that must answer
them. When no implementation-blocking question remains, ask exactly:

> Do you approve `docs/SPEC.md` for implementation?

Do not infer approval. If the user says yes, mark the specification approved
and checkpoint that change. Stop without implementing unless the user also
requests implementation.
