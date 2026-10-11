---
description: Fill in ai-factory/docs/fleet.md — the service map the architect reads — by detecting what it can and asking you the rest
argument-hint: [focus, or blank for the whole map]
---
Run this in the current session. Do NOT delegate to a subagent: this task asks the developer
questions, and a subagent cannot. Follow the `architect` agent's `fleet` mode rules.

1. **Read** ai-factory/docs/fleet.md. If it still holds the shipped default you are filling it in;
   if it is already filled you are refreshing — propose changes, keep every column and row
   order, and never delete a row you cannot prove is gone. Mark such a row `unverified`.

2. **Detect** what the repo already tells you, and note where each fact came from:
   - `git remote -v` and the repo name · ai-factory/docs/architecture.md · README
   - manifests: package.json, go.mod, pyproject.toml, Cargo.toml, *.csproj, pom.xml
   - deployables: docker-compose*.yml, Dockerfile(s), k8s or helm manifests, Procfile,
     apps/ services/ packages/ directories in a monorepo
   - contracts exposed: route definitions, queue, topic and event names, GraphQL schemas,
     OpenAPI documents
   - contracts consumed: base URLs, service names and queue names in .env.example and config
   - owners: CODEOWNERS, ai-factory/AGENTS.md

3. **Ask** the developer only for what you could not detect, in batches of at most four
   questions, most consequential first. Typically:
   - which detected components are separately deployed services and which are one service
   - what data each service is the source of truth for
   - which exposed contracts are public — others may depend on them — and which are internal
   - the services this one talks to that are not in this repo, and their repos
   - the boundary rules: what must never call what, and why
   - the owner of each service
   Offer your detected answer as the default so the developer confirms rather than types.
   If the session is not interactive — `make ai TASK=fleet` in CI — ask nothing: write what
   you detected, mark every unknown `TBD`, and list them under **Known gaps**.

4. **Write** ai-factory/docs/fleet.md. Keep the table's columns. Remove the `Status: unfilled
   default` line once at least one row is real. Record which facts were detected and which
   the developer supplied, so the next refresh knows the difference. Fill the Boundaries,
   Environments and Known gaps sections; leave a section empty rather than inventing it.

5. **Report** the rows added, changed and left `TBD`, and what the architect still cannot
   decide without them. Change no other file.

Focus, or blank for the whole map: $ARGUMENTS
