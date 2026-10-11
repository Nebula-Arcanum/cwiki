---
description: Reproduce a bug with a failing test, then fix it minimally
argument-hint: <bug: symptom and how to reproduce>
---
Read ai-factory/AGENTS.md and ai-factory/docs/coding-standards.md. Find the spec that covers the behaviour.

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

1. Reproduce with a failing test (unit or end-to-end, per the project's test setup) BEFORE changing any code.
2. Fix with the smallest change that makes the test pass. No unrelated refactoring.
3. Run affected checks and the project's required completion checks (see ai-factory/AGENTS.md); run the full required suite once at completion. Prose-only changes need applicable document checks, not invented runtime tests. Investigate failures with new evidence; after two repeated attempts with no new hypothesis or evidence, preserve work and report the blocker. Never weaken tests.
4. Report: root cause · files changed · whether the spec was wrong or incomplete.

If nothing follows the command name, ask the user what the bug is and how to reproduce it, and stop. Do not go hunting for something that looks broken.

Bug: $ARGUMENTS
