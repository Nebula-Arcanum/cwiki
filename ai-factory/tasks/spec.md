---
description: Draft a feature spec with Given/When/Then acceptance criteria
argument-hint: <feature request>
---
Where the request comes from. If `ai-factory/docs/tracker.md` exists AND this repo is configured as
that file defines AND the whole argument is a key in the form it gives, follow that file to
resolve the key; the description it returns is the request, and the key is handed on with it.
If it does not resolve, stop and ask — never spec the key itself. If one spec already records
that key, report its path and stop; if two do, report both and stop. In every other case — not
configured, no such file, or an argument that is not wholly a key — the argument IS the
request, exactly as typed, and no key is resolved. When this repo is not configured, say
nothing about keys, configuration or that file in what you report: describe the spec exactly
as you would have before this paragraph existed. A repo that has configured nothing must not
learn from your report that the mechanism is there.

Delegate to the `specifier` subagent with this instruction: write the spec for the request
below (with the resolved key and description, when there is one). Read ai-factory/AGENTS.md,
ai-factory/docs/architecture.md, and the exploration and analysis for this feature if either exists;
write ai-factory/specs/<NNNN>-<slug>.md with a title and summary, `Ticket: <key>` only when a key was
handed over, the user story, Given/When/Then criteria numbered AC1… that each stand on their
own, out of scope, open questions, and data, routes and components touched; what a description
leaves implicit is an open question, never a criterion; write no code and no plan. Return its
report unchanged: the file path and the open questions.

If it returns a question instead of a file, ask the developer that question and stop. In a
headless run there is nobody to ask: report the question, say nothing was written, and stop.

Do not write the spec yourself in this task, and do not edit any file yourself.

If nothing follows the command name, ask the user which feature to specify, and stop. Do not invent one, and do not take it from the branch name.

Lifecycle telemetry, only when `ai-factory/contracts/config.json` sets `"lifecycle": {"enabled": true}`:
first run `node ai-factory/make/lifecycle.js start --phase spec` and keep the run ID it prints;
bracket any wait for the developer with `lifecycle.js wait-start --run <run>` and `wait-end --run <run>
--wait <wait>`; at the end run `lifecycle.js end --run <run> --outcome succeeded|failed|interrupted --delivery <id from the new sidecar>`.
A lifecycle message never changes this task's outcome; report it and carry on.

Feature: $ARGUMENTS
