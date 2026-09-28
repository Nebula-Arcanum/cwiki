---
name: delivering-cwiki-features
description: "Orchestrates implementation of approved cwiki features through isolated Git-worktree workers, integration, verification, and demos. Use when implementing, fixing, or completing a cwiki roadmap task after specification approval."
---

# Delivering cwiki features

The top-level agent is the orchestrator and integration owner.

## Approval gate

Before changing product code, verify that `docs/SPEC.md` says it is approved
and that the user explicitly answered yes to:

> Do you approve `docs/SPEC.md` for implementation?

If either condition is missing, load the discovery workflow and resolve it.

## Plan the task

Read the relevant product requirement, feature row, roadmap milestone, and
existing implementation. Define acceptance criteria and the smallest coherent
vertical slice. Keep shared interfaces and tightly coupled edits with the
orchestrator.

## Isolate workers

Use a separate Git worktree and branch for every independent worker task.
Stabilize shared interfaces first, then assign disjoint file ownership. In each
brief include:

- exact goal and acceptance criteria;
- relevant requirement and file paths;
- files the worker owns and must not touch;
- checks to run;
- instruction to commit locally and not push.

Workers do not share a checkout and should not depend on uncommitted work from
another worker. Use sequential work instead when changes overlap or require
continuous coordination.

## Integrate and verify

Review each worker's diff and evidence before integrating its commit. Resolve
integration issues in the orchestrator checkout. Run targeted checks followed
by the milestone's combined checks. For UI appearance changes, render and
inspect representative states. A summary or screenshot alone is not proof that
behavior works.

After a verified task, update its planning status, stage explicit paths, and
make a local checkpoint commit. Do not push, merge, tag, deploy, or publish
without explicit approval.

Report the behavior delivered, actual verification, demo instructions, known
limitations, and whether the changes are only local or have been delivered
farther.
