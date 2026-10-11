# Tracker seam

**This is the only file in the layout that may name a tracker vendor, a connector, a URL or a
field.** A task prompt may name *this file* and nothing else. If a task needs a vendor fact,
the fact belongs here and the task is told to read this — see ai-sdlc's ADR 0004 rule 3.

Every repo receives this file. **A repo that has not configured a tracker never reaches any
behaviour described here**, and must be unable to tell the feature shipped.

## Is this repo configured?

Configured means `ai-factory/jira.yaml` exists **and** parses to a mapping carrying `base_url`.

Anything else is **unconfigured**: the file absent, empty, comment-only, unparseable, or
parsing to something that is not a mapping with that key. A half-written config is
unconfigured, deliberately — a typo must fail closed into the repo's normal behaviour, never
into a session that stops and asks about a ticket.

When unconfigured, a task must behave exactly as it would if this file did not exist: the
argument is free text whatever it looks like, no key is resolved, no question about a ticket is
asked, and nothing about a tracker appears in any output. `PROJ-123`, `UTF-8` and
`a bug in the CSV export` are all just text.

## Resolving a key

Only in a configured repo, and only when the **whole** argument is a key. A key is
`[A-Z][A-Z0-9]+-[0-9]+` matched end to end. `PROJ-123 but only the CSV part` is free text, not
a key with a qualifier — do not split it.

Try in this order and stop at the first that applies:

1. **The Atlassian MCP connector**, if this session has it. Claude Code only.
2. **`ai-factory/make/jira.sh`**, if this repo has it. Not built yet; when it exists it is the arm that
   works in Codex and in headless `make ai`.
3. **Otherwise, stop and ask the developer how to proceed.** Report that the key could not be
   resolved and why. Outside Claude Code this is the normal path today, not an error.

In a non-interactive run — `make ai`, CI — there is nobody to ask. Say plainly that the key
did not resolve and that nothing was written, and stop there. Do not fall back to specifying
the key, and do not invent an answer to keep the run green: a run that produces nothing and
says so is recoverable, one that produces a wrong artefact is not.

Never fall back to treating the key as the feature request. A spec written from the string
`PROJ-123` is a confident document about nothing, which is worse than no spec. Never guess a
ticket from the branch name, the last commit, or another spec.

If the key resolves to nothing — no such ticket — stop and ask. Do not proceed with an empty
description.

## What to read

**The description field, and no other field.** Not a checklist field, not a tracker plugin's
own acceptance-criteria field, not comments, not linked issues.

A description is prose written for a human, so it will leave things implicit. Whatever it does
not state — an unnamed actor, an unstated error case, a threshold with no number — becomes an
Open question in the artefact being written. Never write it as an acceptance criterion. An
inferred criterion that looks agreed is worse than a missing one: it gets tested and reviewed
by people who do not know the ticket never said it.

`base_url` from `ai-factory/jira.yaml` is what builds a link to a ticket without fetching it.

## Not defined here yet

**Write-back** — commenting on a ticket, and moving one. Decided in ai-sdlc's ADR 0006 but not
yet specified or built; see `ai-factory/designs/0002-jira-integration.md` §9 spec 3. Until it is,
nothing in the layout writes to a tracker. Do not infer a contract for it from this file.
