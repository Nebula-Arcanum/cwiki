---
description: Small maintenance change with tests and no behaviour drift
argument-hint: <what to change>
---
Read ai-factory/AGENTS.md. Make the change described below with the smallest diff.
Do not touch behaviour beyond the request. Run affected checks and the project's required completion checks (see ai-factory/AGENTS.md); run the full required suite once at completion. Prose-only changes need applicable document checks, not invented runtime tests.

Assurance, only when `ai-factory/assurance.json` exists: first run `node ai-factory/make/assurance.js show`
and show the developer the preset and effective requirements it prints, with any conflict or unmet
requirement. A preset only adds requirements; it never weakens this procedure or project rules.
Under `standard` or `strict`, record the acceptance checklist as a quick delivery in the format
`ai-factory/tasks/quick.md` step 2 describes, tick it and record `make -f ai-factory/make/ai.mk verify DELIVERY=<id>`,
so review and completion have a delivery ID. There, self-review never satisfies review: only
`make -f ai-factory/make/ai.mk review DELIVERY=<id>` records independent review. Run it only in a session the
developer invoked directly. A routed worker or a headless run never runs it: it reports review as an
unmet requirement naming that command, as does a session where it fails. Never record review
evidence another way. With a preset, claim completion only when
`node ai-factory/make/assurance.js complete <id>` exits 0; otherwise report each reason it lists. Under
strict it writes the fresh report, which must be `ready`; an earlier report never counts.

Report files changed and anything you noticed that should become a spec or an ADR.

If nothing follows the command name, ask the user what to change, and stop. Do not go looking for something that seems untidy.

Chore: $ARGUMENTS
