#!/usr/bin/env node
// Configuration is data: no user-controlled value is interpolated into a shell.
const fs = require("node:fs");
const path = require("node:path");
const crypto = require("node:crypto");
const { spawn, execFileSync } = require("node:child_process");
const { boundary, withLogLock } = require("./safe-files.js");
const { digest, selectedPreset } = require("./gate.js");
const models = require("./models.js");
const lifecycleStore = require("./lifecycle-events.js")({ boundary });
// Preserve the invoked workspace when its entry point is a source-repo symlink.
// Node resolves __dirname to the template's location, but argv[1] retains the entry.
const ENTRY_DIRECTORY =
	require.main === module ? path.dirname(process.argv[1]) : __dirname;
const ROOT = fs.realpathSync(path.resolve(ENTRY_DIRECTORY, "../.."));
const WORKSPACE = path.join(ROOT, "ai-factory");
const RUNS = path.join(WORKSPACE, "runs");
const safe = boundary(WORKSPACE);

function readModels(file = path.join(WORKSPACE, "models.yaml")) {
	return models.readModels(file);
}

// Kept for callers of the pre-routing signature; run() uses the structured selection.
function resolveModel(tool, review, env = process.env, task) {
	return models.selectModel({
		root: ROOT,
		tool,
		task: review ? "check" : task,
		env,
	}).model;
}

function executable(tool, env) {
	const value = env.CMD || tool;
	if (/^[A-Za-z0-9_.-]+$/.test(value)) return value;
	if (!value.includes(path.sep) || /[\r\n\0]/.test(value)) {
		throw new Error(
			"CMD must be an executable name or path, not a shell command",
		);
	}
	const file = path.resolve(ROOT, value);
	if (!fs.statSync(file).isFile())
		throw new Error("CMD must select a regular executable file");
	fs.accessSync(file, fs.constants.X_OK);
	return file;
}

function launch(command, args, input, output, env = process.env) {
	return new Promise((resolve, reject) => {
		const child = spawn(command, args, {
			cwd: ROOT,
			env,
			shell: false,
			stdio: ["pipe", output, "inherit"],
		});
		let interrupted;
		let timer;
		const stop = (signal) => {
			interrupted = signal;
			child.kill(signal);
			timer = setTimeout(() => child.kill("SIGKILL"), 1000);
			timer.unref();
		};
		const interrupt = () => stop("SIGINT");
		const terminate = () => stop("SIGTERM");
		process.on("SIGINT", interrupt);
		process.on("SIGTERM", terminate);
		const cleanup = () => {
			clearTimeout(timer);
			process.removeListener("SIGINT", interrupt);
			process.removeListener("SIGTERM", terminate);
		};
		child.once("error", (error) => {
			cleanup();
			reject(error);
		});
		child.once("close", (code, signal) => {
			cleanup();
			if (interrupted || signal || code !== 0)
				reject(new Error(`CLI failed (${interrupted || signal || code})`));
			else resolve();
		});
		child.stdin.on("error", (error) => {
			if (error.code !== "EPIPE") {
				child.kill();
				reject(error);
			}
		});
		child.stdin.end(input);
	});
}

// Lifecycle telemetry for a headless run (make/lifecycle.js). Opt-in, best effort: a refused or
// failed event is one stderr line and never changes the run's own result or its run-log row.
function lifecycleRun(env, task, tool, model) {
	let config;
	try {
		config = lifecycleStore.config(WORKSPACE);
	} catch (error) {
		process.stderr.write(`lifecycle: config ignored — ${error.message}\n`);
		return null;
	}
	if (!config.enabled) return null;
	const valid = (pattern, value) => (pattern.test(value || "") ? value : null);
	const P = lifecycleStore.PATTERNS;
	const common = {
		host: "headless",
		run_id: lifecycleStore.newId("r"),
		delivery_id: valid(P.delivery_id, env.DELIVERY),
		parent_run_id: valid(P.run_id, env.T4_LIFECYCLE_RUN),
		phase: valid(P.phase, task),
		task: valid(P.task, task),
		step: valid(P.step, env.STEP),
	};
	const attempt = lifecycleStore.newId("a");
	const emit = (fields) => {
		try {
			lifecycleStore.emit(WORKSPACE, { ...common, ...fields });
		} catch (error) {
			process.stderr.write(`lifecycle: event not recorded — ${error.message}\n`);
		}
	};
	emit({ type: "run_started", tool, model: model || null });
	emit({ type: "phase_started", attempt_id: attempt });
	return {
		id: common.run_id,
		// Usage from the run's own log row. The child's hooks may also see this session; the
		// aggregate then keeps their per-record links and drops this envelope (child_session).
		usage(row) {
			const f = lifecycleStore.csvFields(row);
			const number = (value) => (/^\d+$/.test(value) ? Number(value) : 0);
			const price = f[15] === "" || !Number.isFinite(Number(f[15])) ? null : Number(f[15]);
			emit({
				event_id: lifecycleStore.hashId("usage", "headless", common.run_id),
				type: "usage_linked",
				session_id: valid(P.session_id, f[1]),
				usage: {
					source: "headless",
					agent: null,
					model: f[8] || null,
					turns: number(f[9]),
					input_tokens: number(f[10]),
					output_tokens: number(f[11]),
					cache_read_tokens: number(f[12]),
					cache_write_tokens: number(f[13]),
					cost_usd: price,
					cost_provenance: price === null ? null : "host-reported",
					record_key: require("node:crypto").createHash("sha256").update(`headless:${common.run_id}`).digest("hex"),
					dedupable: true,
					child_session: valid(P.session_id, f[1]),
				},
			});
		},
		end(outcome, reason) {
			emit({ type: "phase_ended", attempt_id: attempt, outcome, reason: reason ? String(reason).slice(0, 200) : null });
			emit({ type: "run_ended", outcome, reason: reason ? String(reason).slice(0, 200) : null });
		},
	};
}
// `<run>.json.model.json`: the requested model and why it was chosen, next to the output it
// produced. A host-reported identity is kept apart from the request and is null when the output
// names none; a gateway alias may still resolve to another model behind the provider.
function selectionRecord(output, selection, { command, args, outcome, reason }) {
	let reported = null;
	try {
		const text = fs.readFileSync(output, "utf8");
		const found = new Set();
		for (const line of text.split("\n")) {
			if (!line.trim()) continue;
			let value;
			try {
				value = JSON.parse(line);
			} catch {
				continue;
			}
			for (const id of Object.keys(value?.modelUsage || {})) found.add(id);
			if (typeof value?.model === "string") found.add(value.model);
		}
		if (found.size) reported = [...found].sort();
	} catch {}
	const record = {
		...selection,
		requested_model: selection.model || null,
		reported_models: reported,
		model_argument: args ? (args.includes("--model") || args.includes("-m") ? "passed" : "omitted") : null,
		executable: command ? path.basename(command) : null,
		outcome,
		reason: reason ? String(reason).slice(0, 200) : null,
	};
	delete record.model;
	safe.write(`${output}.model.json`, `${JSON.stringify(record, null, 2)}\n`, { exclusive: true });
}
// With a preset selected, its effective requirements are shown before any host runs, and a
// conflicting configuration (strict with GATE_ENFORCE=0) or a malformed selection stops here.
// Without a selection nothing is read or printed: existing behavior is unchanged.
function assurancePreflight(env = process.env) {
	const preset = selectedPreset(ROOT);
	if (preset === null) return null;
	const assurance = require("./assurance.js");
	const policy = assurance.show({ root: ROOT, env });
	process.stderr.write(`${assurance.describe(policy)}\n`);
	for (const item of policy.unmet)
		process.stderr.write(`assurance: unmet ${item.code}: ${item.message}\n  ${item.hint}\n`);
	if (policy.conflicts.length)
		throw new Error(
			`assurance: preset ${preset} cannot run with this configuration; nothing was launched\n${policy.conflicts.map((item) => `  ${item.code}: ${item.message}\n  ${item.hint}`).join("\n")}`,
		);
	return policy;
}
async function run(options = {}) {
	const env = options.env || process.env;
	const tool = options.tool || env.TOOL || "claude";
	const task = options.task || env.TASK || "chore";
	if (task === "start")
		throw new Error("TASK=start requires an interactive session in v1; CI must name a destination task such as quick, fix, chore, analyse, design, explore or spec");
	if (task === "continue")
		throw new Error("TASK=continue requires an interactive session in v1: it may need answers from a developer and never waits for or guesses them. Run /t4:continue <delivery> interactively, or have CI name the destination task directly: plan, test, run, check or report");
	if (!["claude", "codex"].includes(tool))
		throw new Error(`Unsupported tool: ${tool}`);
	if (!/^[a-z][a-z0-9-]*$/.test(task))
		throw new Error("TASK must name an existing workspace task");
	const taskFile = fs.realpathSync(path.join(WORKSPACE, "tasks", `${task}.md`));
	const relativeTask = path.relative(ROOT, taskFile);
	if (
		relativeTask === ".." ||
		relativeTask.startsWith(`..${path.sep}`) ||
		path.isAbsolute(relativeTask)
	) {
		throw new Error("TASK must resolve inside the current repository");
	}
	if (!fs.statSync(taskFile).isFile())
		throw new Error("TASK must select a regular task file");
	let input = options.input;
	if (input === undefined) {
		if (env.INPUT_FILE) {
			const inputFile = path.resolve(ROOT, env.INPUT_FILE);
			if (!fs.statSync(inputFile).isFile())
				throw new Error("INPUT_FILE must select a regular file");
			input = fs.readFileSync(inputFile);
		} else input = Buffer.from(env.INPUT || "", "utf8");
	}
	// Resolved once, before anything launches; a configuration error stops the run here.
	assurancePreflight(env);
	const selection = models.selectModel({
		root: ROOT,
		tool,
		task: options.review ? "check" : task,
		env,
	});
	const model = selection.model;
	if (selection.routing_enabled && model && tool === "claude" && env.CLAUDE_CODE_SUBAGENT_MODEL)
		throw new Error(
			`CLAUDE_CODE_SUBAGENT_MODEL overrides the subagents of this ${selection.task} run, so ${model} would not apply to them; unset it or disable routing`,
		);
	const command = executable(tool, env);
	process.stderr.write(`model: ${models.describe(selection)}\n`);
	safe.mkdir(RUNS);
	for (const name of ["log.csv", "log.previous.csv"])
		safe.assertPath(path.join(RUNS, name));
	const scratch = safe.scratch(path.join(RUNS, "tmp"));
	const lifecycle = lifecycleRun(env, task, tool, model);
	const identity = `${new Date().toISOString().replace(/[:.]/g, "-")}-${crypto.randomUUID()}-${tool}-${task}`;
	const output = path.join(RUNS, `${identity}.json`);
	const sidecar = `${output}.last.txt`;
	let fd;
	let launched = false;
	let args;
	try {
		const prompt = Buffer.concat([
			fs.readFileSync(taskFile),
			Buffer.from("\n## Input\n"),
			Buffer.from(input),
		]);
		safe.write(path.join(scratch, "prompt.txt"), prompt, { exclusive: true });
		fd = safe.open(
			output,
			fs.constants.O_WRONLY | fs.constants.O_CREAT | fs.constants.O_EXCL,
		);
		args =
			tool === "codex" ? ["exec", "--json", "--skip-git-repo-check"] : ["-p"];
		if (model) args.push(tool === "codex" ? "-m" : "--model", model);
		if (tool === "codex") {
			safe.write(sidecar, "", { exclusive: true });
			args.push("-o", sidecar, "-");
		} else args.push("--output-format", "json");
		// The child's hooks read T4_LIFECYCLE_RUN, so its usage links name this run explicitly.
		// One launch only: a rejected model, failed login or refused request is the result.
		launched = true;
		await launch(command, args, prompt, fd, lifecycle ? { ...process.env, T4_LIFECYCLE_RUN: lifecycle.id } : process.env);
		fs.closeSync(fd);
		fd = undefined;
		safe.assertPath(output, { allowMissing: false });
		if (!fs.statSync(output).size)
			throw new Error("CLI produced empty output; no successful run logged");
		if (tool === "codex") safe.assertPath(sidecar, { allowMissing: false });
		const outputHash = digest(fs.readFileSync(output));
		const sidecarHash =
			tool === "codex" ? digest(fs.readFileSync(sidecar)) : undefined;
		if (options.scope)
			safe.write(
				`${output}.scope.json`,
				`${JSON.stringify(options.scope, null, 2)}\n`,
				{ exclusive: true },
			);
		withLogLock(WORKSPACE, () => {
			for (const name of ["log.csv", "log.previous.csv"])
				safe.assertPath(path.join(RUNS, name));
			const row = execFileSync(
				process.execPath,
				[path.join(__dirname, "log.js"), output, task, tool, model],
				{
					cwd: ROOT,
					shell: false,
					encoding: "utf8",
					stdio: ["ignore", "pipe", "inherit"],
				},
			);
			safe.append(path.join(RUNS, "log.csv"), row);
			lifecycle?.usage(row);
		});
		selectionRecord(output, selection, { command, args, outcome: "succeeded" });
		lifecycle?.end("succeeded");
		return {
			output,
			sidecar: tool === "codex" ? sidecar : undefined,
			outputHash,
			sidecarHash,
			model,
			selection,
			tool,
			task,
			scope: options.scope,
		};
	} catch (error) {
		if (launched) {
			try {
				selectionRecord(output, selection, { command, args, outcome: "failed", reason: error.message });
			} catch (recordError) {
				process.stderr.write(`runner: selection not recorded — ${recordError.message}\n`);
			}
		}
		lifecycle?.end(/SIGINT|SIGTERM|SIGKILL/.test(error.message) ? "interrupted" : "failed", error.message);
		throw error;
	} finally {
		if (fd !== undefined) fs.closeSync(fd);
		safe.removeScratch(scratch);
	}
}

function git(args, options = {}) {
	return execFileSync("git", args, {
		cwd: ROOT,
		shell: false,
		encoding: "utf8",
		stdio: ["pipe", "pipe", "pipe"],
		env: { ...process.env, GIT_OPTIONAL_LOCKS: "0" },
		...options,
	});
}

function reviewInput(env = process.env) {
	const requested = env.REVIEW_SCOPE || "working-tree";
	if (!["working-tree", "branch", "supplied"].includes(requested))
		throw new Error("REVIEW_SCOPE must be working-tree, branch, or supplied");
	const splitPaths = (text) => text.split("\0").filter(Boolean);
	const diffFlags = [
		"--no-ext-diff",
		"--no-textconv",
		"--no-renames",
		"--binary",
	];
	let input;
	let paths;
	let scope = requested;
	if (env.INPUT_FILE || scope === "supplied") {
		scope = "supplied";
		if (env.INPUT_FILE) {
			const file = path.resolve(ROOT, env.INPUT_FILE);
			if (!fs.statSync(file).isFile())
				throw new Error("INPUT_FILE must select a regular diff file");
			input = fs.readFileSync(file);
		} else input = Buffer.from(env.INPUT || "");
		if (input.length) {
			try {
				paths = splitPaths(
					git(["apply", "--numstat", "-z", "--"], { input }),
				).map((row) => row.split("\t").slice(2).join("\t"));
			} catch {
				throw new Error(
					"Supplied review input must be a unified diff (git apply --numstat could not read it)",
				);
			}
		} else paths = [];
	} else if (scope === "branch") {
		let base;
		try {
			base = git(["merge-base", "HEAD", "main"]).trim();
		} catch {
			base = git(["merge-base", "HEAD", "develop"]).trim();
		}
		const range = `${base}...HEAD`;
		input = git(["diff", ...diffFlags, range, "--"]);
		paths = splitPaths(
			git(["diff", "--no-renames", "--name-only", "-z", range, "--"]),
		);
	} else {
		const staged = git(["diff", ...diffFlags, "--cached", "--"]);
		const unstaged = git(["diff", ...diffFlags, "--"]);
		const untracked = splitPaths(
			git(["ls-files", "--others", "--exclude-standard", "-z", "--"]),
		);
		paths = [
			...new Set([
				...splitPaths(
					git(["diff", "--no-renames", "--name-only", "-z", "--cached", "--"]),
				),
				...splitPaths(git(["diff", "--no-renames", "--name-only", "-z", "--"])),
				...untracked,
			]),
		];
		const additions = untracked.map((file) => {
			try {
				return git([
					"diff",
					...diffFlags,
					"--no-index",
					"--",
					"/dev/null",
					file,
				]);
			} catch (error) {
				if (error.status === 1 && error.stdout) return error.stdout;
				throw error;
			}
		});
		input = `## Staged changes\n${staged}\n## Unstaged changes\n${unstaged}\n## Untracked changes\n${additions.join("\n")}`;
	}
	const selection = { scope, paths: [...new Set(paths)].sort() };
	return { selection, input, empty: selection.paths.length === 0 };
}

async function review() {
	if (![undefined, "", "0", "1"].includes(process.env.GATE_ENFORCE)) {
		throw new Error("GATE_ENFORCE must be 0, 1, or unset");
	}
	const selected = reviewInput();
	const summary = `Review scope: ${selected.selection.scope}\nSelected paths: ${JSON.stringify(selected.selection.paths)}\n`;
	process.stdout.write(summary);
	if (selected.empty) return { empty: true, scope: selected.selection };
	const input = Buffer.concat([
		Buffer.from(`${summary}\n`),
		Buffer.from(selected.input),
	]);
	const result = await run({
		task: "check",
		review: true,
		input,
		scope: selected.selection,
	});
	// The workspace's own entry point, so the gate reads this workspace's assurance selection.
	const args = [
		path.join(WORKSPACE, "make", "gate.js"),
		result.output,
		"--tool",
		result.tool,
		"--output-sha256",
		result.outputHash,
	];
	if (result.sidecar)
		args.push(
			"--sidecar",
			result.sidecar,
			"--sidecar-sha256",
			result.sidecarHash,
		);
	let gateError;
	try {
		execFileSync(process.execPath, args, {
			cwd: ROOT,
			shell: false,
			stdio: "inherit",
		});
	} catch (error) {
		gateError = error;
	}
	if (process.env.DELIVERY) reviewEvidence(result);
	if (gateError) throw gateError;
	return result;
}
// With DELIVERY set, bind this exact gated output to the code it reviewed (make/contracts.js).
// Approval is re-derived from the same hashed output the gate read, never from its exit code.
function reviewEvidence(result) {
	const { readReview } = require("./gate.js");
	const { recordReview } = require("./contracts.js");
	let verdict;
	let reason;
	try {
		verdict = readReview({
			file: result.output,
			tool: result.tool,
			sidecar: result.sidecar,
			outputHash: result.outputHash,
			sidecarHash: result.sidecarHash,
		});
	} catch (error) {
		reason = `invalid review: ${error.message}`;
	}
	const approved =
		verdict?.verdict === "approve" &&
		!verdict.findings.some((finding) => finding.severity === "blocker");
	const recorded = recordReview({
		root: ROOT,
		delivery: process.env.DELIVERY,
		approved,
		verdict: verdict?.verdict,
		findings: verdict?.findings,
		tool: result.tool,
		outputHash: result.outputHash,
		reason,
		// A separate headless check run that saw only the selected diff: the independent boundary.
		independence: "independent",
		boundary: "make review",
	});
	process.stdout.write(
		`review evidence: ${recorded.evidence.status} → ${path.relative(ROOT, recorded.file)}\n`,
	);
}

function flush() {
	return withLogLock(WORKSPACE, flushLocked);
}
function flushLocked() {
	const pending = safe.assertPath(path.join(RUNS, "log.pending.csv"));
	if (!fs.existsSync(pending) || !fs.statSync(pending).size) return;
	const file = safe.assertPath(path.join(RUNS, "log.csv"));
	const contents = fs.readFileSync(pending, "utf8");
	const split = contents.indexOf("\n");
	if (split < 0) throw new Error("log-flush: pending header is incomplete");
	const header = contents.slice(0, split);
	if (!fs.existsSync(file) || !fs.statSync(file).size)
		safe.write(file, `${header}\n`);
	if (fs.readFileSync(file, "utf8").split("\n")[0] !== header)
		throw new Error("log-flush: schema mismatch");
	safe.append(file, contents.slice(split + 1));
	safe.unlink(pending);
}

function clean() {
	return withLogLock(WORKSPACE, cleanLocked);
}
function cleanLocked() {
	safe.assertPath(RUNS);
	if (!fs.existsSync(RUNS)) return;
	const cutoff = Date.now() - 30 * 86400000;
	for (const name of fs.readdirSync(RUNS)) {
		if (!(/\.json$/.test(name) || /^\.(counted|task)\./.test(name))) continue;
		const file = safe.assertPath(path.join(RUNS, name), {
			allowMissing: false,
		});
		const stat = fs.statSync(file);
		if (stat.isFile() && stat.mtimeMs < cutoff) safe.unlink(file);
	}
}

async function main(action = process.argv[2]) {
	if (action === "ai" || action === "review") {
		const result = await (action === "review" ? review() : run());
		process.stdout.write(
			result.empty
				? "No selected changes; no review model invoked.\n"
				: `run saved: ${path.relative(ROOT, result.output)}\n`,
		);
	} else if (action === "log-flush") flush();
	else if (action === "clean-runs") clean();
	else throw new Error(`Unknown runner action: ${action}`);
}

if (require.main === module)
	main().catch((error) => {
		process.stderr.write(`runner: ${error.message}\n`);
		process.exitCode = 1;
	});
module.exports = {
	run,
	review,
	reviewInput,
	readModels,
	resolveModel,
	launch,
	git,
	main,
	ROOT,
	WORKSPACE,
	RUNS,
	safe,
};
