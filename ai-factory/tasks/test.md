---
description: Write acceptance tests from the spec's acceptance criteria, independently of the implementation
argument-hint: <spec path> red|gaps
---
Delegate to the `tester` subagent with this instruction: write the acceptance tests for the
spec and plan named below, in the mode named below (`red` before implementation, `gaps`
after). Read the spec's ACs, the plan, and the project's existing tests; read the
implementation's public surface only. In `red`, target the next implementation step unless
the user requests whole-plan coverage; group any future-step failures by test identity and
expected cause. A red step succeeds only on its documented missing behavior. In `gaps`, run
the relevant regressions and report any uncovered criteria. Write test files only — never production code.
Return its AC → test table and failure messages unchanged, then add one line: whether the
red tests fail for the right reason, or which ACs are still uncovered.

Do not implement anything in this task, and do not edit any file yourself.

If nothing follows the command name, ask the user which spec and which mode, red or gaps, and stop.

Lifecycle telemetry, only when `ai-factory/contracts/config.json` sets `"lifecycle": {"enabled": true}`:
first run `node ai-factory/make/lifecycle.js start --phase test --delivery <id> --step S<N>` and keep the run ID it prints;
bracket any wait for the developer with `lifecycle.js wait-start --run <run>` and `wait-end --run <run>
--wait <wait>`; at the end run `lifecycle.js end --run <run> --outcome succeeded|failed|interrupted`.
A lifecycle message never changes this task's outcome; report it and carry on.

Spec, plan and mode: $ARGUMENTS
