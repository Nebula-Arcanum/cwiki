---
description: Turn a feature description into a business-analysis pack — objectives, scope, actors and permissions, use cases, requirements, rules, data, stories with acceptance criteria, test scenarios — facts, assumptions and open questions kept apart; before any spec
argument-hint: <feature description> | lean|questions <feature description> | review|update|explain <analysis path> […]
---
Use this when the request is a business description rather than a buildable feature: several
actors, permissions, business rules, a lifecycle, integrations, or policy nobody has decided
yet. A small, well-understood change goes straight to /t4:spec; where a capability lives is
/t4:design; how to build it is /t4:explore. This task settles WHAT the business needs, in
enough detail that a spec can be written without guessing.

Delegate to the `analyst` subagent.

- First word `lean` or `questions` — run that mode on the rest of the input.
- First word `review`, `update` or `explain` — run that mode on the `ai-factory/analyses/` file
  named next; for `update`, everything after the path is the change.
- Anything else — run `document` mode on the whole input. Output goes to
  `ai-factory/analyses/<NNNN>-<slug>.md` (create the directory if missing).

Do not change any code, spec or plan in this task, and do not edit any file yourself. Do not
create anything in an external system or send anything anywhere — documentation authorises
none of that.

End with the file path, the readiness assessment in one sentence, the open questions that
block a spec, and the exact `/t4:spec …` line(s) to run once they are answered.

If nothing follows the command name, ask the user which feature to analyse, and stop. Do not
take it from the branch name or the last commit.

Feature description, or `<mode> …`: $ARGUMENTS
