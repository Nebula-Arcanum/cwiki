# Workspace boundary

Keep all plugin-owned project instructions, configuration, plans, specs, decisions, reports,
logs and caches in `ai-factory/`. Put scratch work under `ai-factory/runs/tmp/`; retain evidence
under `ai-factory/runs/`. Opt-in contract evidence (see `contracts/README.md`) is committed
under `ai-factory/evidence/`; its command logs stay in ignored `ai-factory/runs/evidence/`.
Completion reports (`/t4:report`) are committed snapshots under `ai-factory/reports/<id>/`.
Opt-in lifecycle events and telemetry exports stay local, in the ignored
`ai-factory/runs/lifecycle/` and `ai-factory/runs/telemetry/`. Model selection records stay local
too: `<run>.json.model.json` beside each headless output and `ai-factory/runs/routing.jsonl`.
Workspace Git rules live in this directory's own dotfiles.

Strict containment is the default. Preserve existing root files and tool configuration. Do not
create external integration files without a separate explicit request. Any authorized adapter
contains registration metadata and references to canonical workspace instructions only. A
model-routing worker agent adds only its pinned model and a short note on how it runs its task.

Application source and tests remain in their normal locations, edited only within the task's
authorized scope. Host credentials, transcripts, settings and Git metadata are not work files
to copy into this directory. Installed plugin source stays in the plugin package.

Core operation uses the installed plugin plus Node, Bash, Git and the configured AI CLI. It
does not require another repo, overlay, tracker, knowledge source or package download.
Optional integrations remain optional. Use explicit invocation when the host cannot discover
a task without external project files; never invent a discovery mechanism.

Directory names do not enforce security. Validate resolved paths, and rely on host sandbox
permissions for arbitrary shell execution. The file-edit guard is not a shell sandbox.

Writer protection rejects static symlinks, shared files and unsafe identifiers. Hooks and
runner accounting share a workspace lock. The host must still constrain a hostile process
that swaps filesystem ancestors after validation. Accounting is not a transactional database:
a crash or disk exhaustion between claiming usage and appending its row can need manual
reconciliation. Successful normal/concurrent cases are covered by fixtures; crash recovery
is not claimed.
