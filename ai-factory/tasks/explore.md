---
description: Explore a feature request — read the code, present 2–4 implementation approaches with trade-offs and a recommendation, before any spec
argument-hint: <feature request>
---
Delegate to the `explorer` subagent with this instruction: explore the feature request below.
Read ai-factory/AGENTS.md, ai-factory/docs/architecture.md, ai-factory/docs/coding-standards.md, the relevant
ai-factory/skills/ and the code the request would touch; write ai-factory/explorations/<NNNN>-<slug>.md with
the request restated, what exists today, two to four genuinely different options, a comparison
table, one recommendation, open questions and the /t4:spec line to run next; change no code.
Return its report unchanged: the file path and the recommended option in one sentence.

If it returns a question instead of a file, ask the developer that question and stop. In a
headless run there is nobody to ask: report the question, say nothing was written, and stop.

Do not explore anything yourself in this task, and do not edit any file yourself.

If nothing follows the command name, ask the user what to explore, and stop. Do not guess from the branch name or the last commit.

Feature request: $ARGUMENTS
