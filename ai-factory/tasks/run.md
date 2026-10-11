---
description: Implement one step of a plan
argument-hint: <plan path> <step>
---
Delegate to the `implementer` subagent: implement ONLY the named step in the named plan.
Read ai-factory/AGENTS.md, applicable coding/protection rules, the plan and its spec. Use the
verification phase and exact commands in the step: `red` requires the documented missing-behavior
failures; `step` requires this step and regressions to pass with only recorded future-step failures
remaining; `final` requires every completion check to pass. Never claim the whole suite green
while expected future failures remain. Add meaningful coverage, preserve user work and tick only
the named step once its phase is satisfied. Missing commands or unexpected failures leave it
unticked. Return files changed, phase/results, and any plan/spec problem. Do not start another step.

Assurance, only when `ai-factory/assurance.json` exists: run the read-only
`node ai-factory/make/assurance.js show` and show the developer the preset and effective requirements
before delegating. They add to the phase rules and never relax them. A ticked final step is not a
completed delivery: independent review and completion are decided afterwards, by
`make -f ai-factory/make/ai.mk review DELIVERY=<id>` and `node ai-factory/make/assurance.js complete <id>`.

If it stopped with an explanation instead of a ticked step, relay the explanation and ask the
developer how to proceed. In a headless run there is nobody to ask: report it and stop.

Do not implement anything yourself in this task, and do not edit any file yourself.

If nothing follows the command name, ask the user which plan and which step, and stop. Name the unticked steps if the plan is obvious — do not start one on your own.

Lifecycle telemetry, only when `ai-factory/contracts/config.json` sets `"lifecycle": {"enabled": true}`:
first run `node ai-factory/make/lifecycle.js start --phase run --delivery <id> --step S<N>` and keep the run ID it prints;
bracket any wait for the developer with `lifecycle.js wait-start --run <run>` and `wait-end --run <run>
--wait <wait>`; at the end run `lifecycle.js end --run <run> --outcome succeeded|failed|interrupted`.
A lifecycle message never changes this task's outcome; report it and carry on.

Plan and step: $ARGUMENTS
