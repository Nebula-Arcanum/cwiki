---
description: Generate the local completion report for one delivery — status, evidence, review and follow-up
argument-hint: <delivery id>
---
If nothing follows the command name, run `make -f ai-factory/make/ai.mk contracts JSON=1`, list
the delivery IDs it names with their state, ask which one to report on, and stop. Never guess one.

Run `make -f ai-factory/make/ai.mk delivery-report DELIVERY=<id>` from the project root. It reads
only that delivery's contract sidecars, Markdown, evidence and optional telemetry, runs no check,
edits no source artifact, and writes `ai-factory/reports/<id>/completion.md` and `completion.json`.
Exit 0 means `ready`, 1 means another status (the report is still written), 2 means it could
not be generated. Report the status, every listed reason with its evidence reference, and the
two file paths.

Assurance, only when `ai-factory/assurance.json` exists: first run `node ai-factory/make/assurance.js show`
and report the preset and effective requirements. After the report, run
`node ai-factory/make/assurance.js complete <id>` and report whether completion may be claimed (exit 0)
and each open reason. It evaluates current evidence: under strict it regenerates the report, and
only that fresh report, `ready`, permits completion; an earlier saved report never counts. Under
standard a saved report is optional. Never claim completion while it exits non-zero.

Do not rerun checks to change the result, do not edit the report, and publish nothing: no tracker
comment, no MR, no commit. The MR description inside the report is a draft for the developer.
When the status is not `ready`, name the command that addresses each reason (`make … verify`,
`make … review DELIVERY=<id>`, `contracts.js init …`) and stop. If
`ai-factory/make/delivery-report.js` is missing, report that `/t4:sync-sdlc` shows the template
drift, and stop.

Delivery: $ARGUMENTS
