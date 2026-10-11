---
description: Review the selected change, including uncommitted work by default
argument-hint: [working-tree|branch|supplied diff and context]
---
Delegate to the `reviewer` subagent. Default to the working tree: include staged and unstaged
changes plus relevant nonignored untracked files, without staging anything or reading ignored
secrets. If an explicit branch scope or supplied diff is given, honor that scope; never replace
supplied input with another diff. Review against the named spec/plan or the request's acceptance
checklist for a small change. Report exactly one JSON object using the reviewer's schema, with
scope, reviewed paths and any assessment in summary. Add no prose outside that object.
Do not fix files or change the index. An in-session review is a self-check, not an independent opinion.
Assurance, only when `ai-factory/assurance.json` exists: run the read-only
`node ai-factory/make/assurance.js show` and name the preset and its review requirement in summary.
Only the headless `make -f ai-factory/make/ai.mk review DELIVERY=<id>` run records independent review; when
the preset requires it, an interactive review says in summary that it does not satisfy that
requirement. Never run `make -f ai-factory/make/ai.mk review` from this task: a review never launches another.
After review, `/t4:report <delivery id>` assembles the completion report where artifact contracts are enabled.

Lifecycle telemetry, only when `ai-factory/contracts/config.json` sets `"lifecycle": {"enabled": true}`:
first run `node ai-factory/make/lifecycle.js start --phase check --delivery <id>` and keep the run ID it prints;
bracket any wait for the developer with `lifecycle.js wait-start --run <run>` and `wait-end --run <run>
--wait <wait>`; at the end run `lifecycle.js end --run <run> --outcome succeeded|failed|interrupted`.
A lifecycle message never changes this task's outcome; report it and carry on.

Context: $ARGUMENTS
