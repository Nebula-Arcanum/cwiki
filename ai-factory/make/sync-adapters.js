#!/usr/bin/env node
// External integration files are an explicit opt-in, never workspace state.
const fs = require("node:fs");
const path = require("node:path");
const models = require("./models.js");
const root = fs.realpathSync(process.cwd());
const argument = process.argv[2] || "--adapters=none";
if (
	process.argv.length > 3 ||
	!/^--adapters=(none|all|(?:claude|codex|cursor|routing)(?:,(?:claude|codex|cursor|routing))*)$/.test(
		argument,
	)
) {
	console.error(
		"usage: sync-adapters.sh [--adapters=none|claude,codex,cursor,routing|all]",
	);
	process.exit(1);
}
const tasksDir = path.join(root, "ai-factory/tasks");
if (!fs.existsSync(tasksDir)) {
	console.error("sync-adapters: no ai-factory/tasks directory");
	process.exit(1);
}
if (argument === "--adapters=none") {
	console.log(
		"Strict workspace mode: no external files changed. Invoke a task with node ai-factory/make/runner.js ai, or explicitly request host adapters.",
	);
	process.exit(0);
}
const selected = new Set(
	argument === "--adapters=all"
		? ["claude", "codex", "cursor"]
		: argument.slice(11).split(","),
);
// Task names the t4 plugin already registers as native Claude Code commands. A project pointer
// for one of these puts a second /t4:<name> in the menu, with a different description and no way
// for the user to tell which one runs. The plugin command wins: it works in strict mode, where no
// pointer exists at all. Codex plugins ship no commands, so their skills are still generated.
const nativeClaudeCommands = new Set([
	"adopt-sdlc",
	"doctor",
	"quick",
	"setup-knowledge",
	"setup-tracker",
	"state",
	"start",
	"continue",
	"sync-sdlc",
	"adr",
	"analyse",
	"check",
	"chore",
	"design",
	"explore",
	"fix",
	"fleet",
	"plan",
	"report",
	"run",
	"spec",
	"test",
]);
// Agent names the plugin registers as `t4:<name>`, for the same reason: a pointer adds a second
// agent with the same role and description to the picker. An agent this project added to
// ai-factory/agents/ is not in this set and still gets its pointer, which is the only way it
// becomes selectable. Codex plugins ship no subagents; their task skills carry the procedure
// inline instead.
const nativeClaudeAgents = new Set([
	"analyst",
	"architect",
	"explorer",
	"implementer",
	"planner",
	"reviewer",
	"specifier",
	"tester",
]);
function safe(file) {
	const absolute = path.resolve(root, file);
	if (!absolute.startsWith(`${root}${path.sep}`))
		throw new Error("Adapter path escapes the project");
	let current = root;
	for (const part of path
		.relative(root, absolute)
		.split(path.sep)
		.slice(0, -1)) {
		current = path.join(current, part);
		try {
			if (fs.lstatSync(current).isSymbolicLink())
				throw new Error(`Refusing symlinked adapter directory: ${current}`);
		} catch (error) {
			if (error.code !== "ENOENT") throw error;
		}
	}
	return absolute;
}
const marker = models.MARKER;
const managed = (body) =>
	body.includes(marker) ||
	body.startsWith(models.TOML_MARKER) ||
	/^\.\.\/\.\.\/(?:\.\.\/)?ai-factory\/(?:agents|skills)\//.test(body) ||
	/^---\nname: (?:t4-|ai-)[a-z0-9-]+\n(?:[\s\S]*?\n)?---\nRead `(?:ai-factory|ai)\/tasks\/[a-z0-9-]+\.md`/.test(
		body,
	) ||
	/^---[\s\S]*?\n---\n@(?:\.\.\/)+(?:ai-factory|ai)\/tasks\/[a-z0-9-]+\.md\s*$/.test(
		body,
	) ||
	body ===
		"---\ndescription: Project AI instructions\nalwaysApply: true\n---\nRead ai-factory/AGENTS.md first and follow it. Task procedures are in ai-factory/tasks/.\n";
const writes = [],
	removals = new Set(),
	emptyDirs = new Set();
function put(file, body) {
	const absolute = safe(file);
	if (
		fs.existsSync(absolute) ||
		(() => {
			try {
				return fs.lstatSync(absolute).isSymbolicLink();
			} catch {
				return false;
			}
		})()
	) {
		const stat = fs.lstatSync(absolute);
		if (!stat.isSymbolicLink() && (!stat.isFile() || stat.nlink !== 1))
			throw new Error(`Refusing non-regular or shared adapter file: ${file}`);
		const old = stat.isSymbolicLink()
			? fs.readlinkSync(absolute)
			: fs.readFileSync(absolute, "utf8");
		if (!managed(old))
			throw new Error(`Preserving hand-authored file: ${file}`);
	}
	writes.push({
		absolute,
		body: body.includes(marker)
			? body
			: body.replace(/^(---\n[\s\S]*?\n---\n)/, `$1${marker}\n`),
	});
}
function cleanup(dir) {
	if (!fs.existsSync(safe(`${dir}/placeholder`))) {
		const absolute = safe(dir);
		if (!fs.existsSync(absolute)) return;
	}
	for (const entry of fs.readdirSync(path.join(root, dir), {
		withFileTypes: true,
	})) {
		if (!entry.isFile() || !entry.name.endsWith(".md")) continue;
		const file = safe(`${dir}/${entry.name}`);
		const body = fs.readFileSync(file, "utf8");
		if (managed(body)) removals.add(file);
	}
}
try {
	if (selected.has("claude")) {
		cleanup(".claude/commands");
		cleanup(".claude/commands/t4");
		// Sweeps the pointers an older sync generated for the plugin's own agents; the ones this
		// project added are written back below. A hand-authored agent carries no marker and stays.
		cleanup(".claude/agents");
	}
	// Route agents (ai-factory/make/models.js) are swept and rewritten from models.yaml on every
	// sync that covers their host, so a changed or removed mapping leaves no old agent behind.
	const routeHosts = models.TOOLS.filter(
		(host) => selected.has(host) || selected.has("routing"),
	);
	for (const host of routeHosts) {
		safe(`${models.ADAPTER_DIRS[host]}/placeholder`);
		for (const file of models.presentAdapters(root, host))
			removals.add(safe(file));
	}
	if (
		selected.has("codex") &&
		fs.existsSync(path.join(root, ".codex/skills"))
	) {
		safe(".codex/skills/placeholder");
		for (const entry of fs.readdirSync(path.join(root, ".codex/skills"), {
			withFileTypes: true,
		})) {
			if (!entry.isDirectory()) continue;
			const file = safe(`.codex/skills/${entry.name}/SKILL.md`);
			if (fs.existsSync(file) && managed(fs.readFileSync(file, "utf8"))) {
				// Delete only the generated entry, never arbitrary assets in the directory.
				removals.add(file);
				emptyDirs.add(path.dirname(file));
			}
		}
	}
	for (const entry of fs
		.readdirSync(tasksDir)
		.filter((name) => name.endsWith(".md"))
		.sort()) {
		const name = path.basename(entry, ".md");
		const body = fs.readFileSync(path.join(tasksDir, entry), "utf8");
		const description = /^description: (.*)$/m.exec(body)?.[1] || name;
		const hint = /^argument-hint: (.*)$/m.exec(body)?.[1];
		// The routing step runs before the task file is read, so the pointer names the file
		// instead of importing it.
		if (selected.has("claude") && !nativeClaudeCommands.has(name))
			put(
				`.claude/commands/t4/${name}.md`,
				`---\ndescription: ${description}\n${hint ? `argument-hint: ${hint}\n` : ""}---\n${models.entryPreamble("claude", name)}\n\nUnless the directive routed the task to a worker, read \`ai-factory/tasks/${name}.md\` and follow it with the input below.\n\nTask input: $ARGUMENTS\n`,
			);
		if (selected.has("codex")) {
			const agent = /Delegate to the `([a-z]+)` subagent/.exec(body)?.[1];
			put(
				`.codex/skills/t4-${name}/SKILL.md`,
				`---\nname: t4-${name}\ndescription: ${description}\n---\n${models.entryPreamble("codex", name)}\n\nUnless the directive routed the task to a worker, read \`ai-factory/tasks/${name}.md\` and use the user's request as its input.\n${agent ? `Read the complete procedure in \`ai-factory/agents/${agent}.md\`. When native delegation is available, pass that procedure and task input to a delegate. Otherwise run it in this session and label review as a self-check rather than an independent review. This fallback applies only to the legacy and inherit directives.\n` : ""}`,
			);
		}
	}
	if (selected.has("claude")) {
		for (const name of fs
			.readdirSync(path.join(root, "ai-factory/agents"))
			.filter(
				(n) =>
					n.endsWith(".md") && !nativeClaudeAgents.has(path.basename(n, ".md")),
			)) {
			const source = fs.readFileSync(
				path.join(root, "ai-factory/agents", name),
				"utf8",
			);
			const metadata = /^(---\n[\s\S]*?\n---\n)/.exec(source)?.[1];
			if (!metadata)
				throw new Error(`Agent has no registration metadata: ${name}`);
			put(
				`.claude/agents/${name}`,
				`${metadata}Read ai-factory/agents/${name} and follow it.\n`,
			);
		}
		const skills = path.join(root, "ai-factory/skills");
		if (fs.existsSync(skills))
			for (const entry of fs.readdirSync(skills, { withFileTypes: true })) {
				if (
					entry.isDirectory() &&
					fs.existsSync(path.join(skills, entry.name, "SKILL.md"))
				)
					put(
						`.claude/skills/${entry.name}/SKILL.md`,
						`---\nname: ${entry.name}\ndescription: Follow ai-factory/skills/${entry.name}/SKILL.md\n---\nRead ai-factory/skills/${entry.name}/SKILL.md and follow it.\n`,
					);
			}
	}
	for (const host of routeHosts)
		for (const route of models.expectedAdapters(root, host))
			put(route.file, route.body);
	if (selected.has("cursor"))
		put(
			".cursor/rules/ai.mdc",
			"---\ndescription: Project AI instructions\nalwaysApply: true\n---\nRead ai-factory/AGENTS.md first and follow it. Task procedures are in ai-factory/tasks/.\n",
		);
	// Preflight above completes before any mutation; collisions preserve the previous set.
	for (const file of removals) fs.unlinkSync(file);
	for (const { absolute, body } of writes) {
		try {
			if (fs.lstatSync(absolute).isSymbolicLink()) fs.unlinkSync(absolute);
		} catch (error) {
			if (error.code !== "ENOENT") throw error;
		}
		fs.mkdirSync(path.dirname(absolute), { recursive: true });
		fs.writeFileSync(absolute, body);
	}
	for (const dir of emptyDirs)
		if (fs.existsSync(dir) && !fs.readdirSync(dir).length) fs.rmdirSync(dir);
	console.log(
		`Adapters regenerated for ${[...selected].join(", ")}; canonical work remains in ai-factory/.`,
	);
} catch (error) {
	console.error(`sync-adapters: ${error.message}`);
	process.exitCode = 1;
}
