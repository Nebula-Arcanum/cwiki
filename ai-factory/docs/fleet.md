# Fleet map — cwiki

The services the `architect` agent reads before it decides where a capability belongs.
Scoped to this project: by default the only row is this repo. Add a row for every other
service this one talks to, in this project or beyond it.

**Run `/t4:fleet` to fill this in** — it detects what it can from the repo and asks you the
rest. Run it again whenever a service, contract or owner changes; it refreshes rather than
overwrites.

Status: **unfilled default** — the architect will say so and work without a map until
`/t4:fleet` has run. Delete this line once it has.

| Service | Repo | Owns | Exposes | Consumes | Owner |
|---|---|---|---|---|---|
| cwiki | this repo | TBD | TBD | TBD | TBD |

- **Owns** — the data and the capability this service is the source of truth for.
- **Exposes** — the contracts others may depend on: HTTP routes, queues, topics, events.
  Anything not listed here is internal and may change without notice.
- **Consumes** — the contracts this service depends on, and therefore may not break.

## Boundaries
What must never call what, and why. One line per rule; the architect treats these as binding
and `/t4:design review` flags a plan that crosses one.

## Environments
Which of these services exist in local, dev, uat and prod, and what is stubbed where.

## Known gaps
Rows the map is unsure of — `unverified` entries and anything `/t4:fleet` could not confirm.
