---
description: Record one architectural decision as an ADR in ai-factory/adr/
argument-hint: <decision in one sentence>
---
Every rule that says "no new dependency without an ADR" is served by this task. Use it for a
decision that outlives the change that prompted it: a dependency, a boundary, a protocol, a
storage choice, a convention the whole repo must follow.

Delegate to the `architect` subagent in its `adr` mode. It writes
`ai-factory/adr/<NNNN>-<slug>.md` from `ai-factory/adr/0000-template.md` — Context, Decision,
Consequences, with the date and a Status.

If the decision came from a design document, name that file so the ADR links to it. If the
input carries more than one decision, the architect writes one ADR per decision and says so.
If it supersedes an earlier ADR, the architect names it and updates that file's Status line.

Do not change any code in this task. Report the file path, the decision in one sentence, and
the cost it names in Consequences.

If nothing follows the command name, ask the user which decision to record, and stop. Do not infer one from the diff or the last commit.

Decision: $ARGUMENTS
