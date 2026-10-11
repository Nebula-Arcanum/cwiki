#!/usr/bin/env node
// Review validation is deterministic. Codex's separate final message is accepted
// only with the current runner's exact path and content hashes, never by discovery.
const fs = require("node:fs");
const path = require("node:path");
const crypto = require("node:crypto");
// Preserve the invoked workspace when its entry point is a source-repo symlink (see runner.js).
const ENTRY_DIRECTORY =
	require.main === module ? path.dirname(process.argv[1]) : __dirname;

const digest = (bytes) =>
	crypto.createHash("sha256").update(bytes).digest("hex");
function parseVerdict(text) {
	const trimmed = text.trim();
	const fenced = trimmed.match(/^```(?:json)?\s*\n([\s\S]*?)\n```$/);
	const value = JSON.parse(fenced ? fenced[1] : trimmed);
	if (!value || typeof value !== "object" || Array.isArray(value))
		throw new Error("review must be an object");
	if (!["approve", "request_changes"].includes(value.verdict))
		throw new Error("unknown or missing verdict");
	if (typeof value.summary !== "string")
		throw new Error("summary must be a string");
	if (!Array.isArray(value.findings))
		throw new Error("findings must be an array");
	for (const finding of value.findings) {
		if (!finding || typeof finding !== "object" || Array.isArray(finding))
			throw new Error("each finding must be an object");
		if (!["blocker", "major", "minor"].includes(finding.severity))
			throw new Error("unknown or missing finding severity");
		if (
			!["file", "issue", "suggestion"].every(
				(key) => typeof finding[key] === "string",
			)
		) {
			throw new Error("finding file, issue, and suggestion must be strings");
		}
		if (!Number.isInteger(finding.line) || finding.line < 0)
			throw new Error("finding line must be a nonnegative integer");
	}
	return value;
}

function readReview(options) {
	if (!options.file) throw new Error("missing run output");
	const file = path.resolve(options.file);
	const bytes = fs.readFileSync(file);
	if (!bytes.length) throw new Error("empty run output");
	if (options.outputHash && digest(bytes) !== options.outputHash)
		throw new Error("run output does not match this invocation");
	const tool = options.tool || "claude";
	if (tool === "codex") {
		if (
			!options.sidecar ||
			path.resolve(options.sidecar) !== `${file}.last.txt`
		)
			throw new Error("wrong or missing Codex sidecar identity");
		if (!options.outputHash || !options.sidecarHash)
			throw new Error("Codex review requires both current-run content hashes");
		// A sidecar cannot convert an unrelated or failed stream into a successful run.
		const events = bytes
			.toString("utf8")
			.trim()
			.split(/\r?\n/)
			.map((line) => JSON.parse(line));
		if (
			!events.some((event) => event && event.type === "turn.completed") ||
			events.some(
				(event) => event && ["turn.failed", "error"].includes(event.type),
			)
		) {
			throw new Error("Codex stream has no successful final turn");
		}
		const final = fs.readFileSync(options.sidecar);
		if (digest(final) !== options.sidecarHash)
			throw new Error("Codex sidecar does not match this invocation");
		return parseVerdict(final.toString("utf8"));
	}
	if (tool !== "claude") throw new Error("unsupported review tool");
	if (options.sidecar || options.sidecarHash)
		throw new Error("Claude reviews cannot use a sidecar");
	const envelope = JSON.parse(bytes.toString("utf8"));
	if (envelope?.is_error) throw new Error("Claude reported an error");
	const result =
		envelope && Object.hasOwn(envelope, "result") ? envelope.result : envelope;
	if (typeof result === "string") return parseVerdict(result);
	return parseVerdict(JSON.stringify(result));
}

// The assurance helper ships beside this file. Where it is absent (a partial copy) behavior is
// legacy, but a selection it cannot read is never silently ignored.
function assuranceModule(root) {
	const file = path.join(__dirname, "assurance.js");
	if (fs.existsSync(file)) return require(file);
	const selection = path.join(root, "ai-factory", "assurance.json");
	let present = false;
	try {
		fs.lstatSync(selection);
		present = true;
	} catch {}
	if (present)
		throw new Error("ai-factory/assurance.json selects a preset, but make/assurance.js is missing; run /t4:sync-sdlc to receive it");
	return null;
}
// The selected preset, or null when none is selected. A malformed selection throws.
function selectedPreset(root) {
	const assurance = assuranceModule(root);
	return assurance ? assurance.readSelection(root).preset : null;
}
// Shared with runner.js through assurance.enforcement(): one rule decides advisory or enforced.
function gateMode(enforce, preset = null) {
	if (preset === null) {
		if (enforce !== undefined && enforce !== "" && enforce !== "0" && enforce !== "1")
			return { error: "GATE_ENFORCE must be 0, 1, or unset" };
		return { mode: enforce === "1" ? "enforced" : "advisory", preset };
	}
	const resolved = require("./assurance.js").enforcement({ preset, env: { GATE_ENFORCE: enforce } });
	if (resolved.conflicts.length)
		return { error: resolved.conflicts.map((item) => `${item.code} ${item.message}. ${item.hint}`).join("\n") };
	return { mode: resolved.mode, preset };
}

function evaluate(options, enforce = process.env.GATE_ENFORCE, preset = null) {
	const resolved = gateMode(enforce, preset);
	if (resolved.error) return { code: 2, message: `gate: ${resolved.error}` };
	const strict = resolved.mode === "enforced";
	const label = `${strict ? "enforced" : "advisory"}${preset ? ` (preset ${preset})` : ""}`;
	// Under a preset an advisory exit is never mistaken for approval.
	const advisoryNote =
		preset && !strict && require("./assurance.js").PRESETS[preset].review_required
			? "; an advisory exit is not approval, and completion stays non-ready until an approving independent review is recorded"
			: "";
	let verdict;
	try {
		verdict = readReview(options);
	} catch (error) {
		return {
			code: strict ? 1 : 0,
			message: `gate: ${label} — invalid review, no approval: ${error.message}${advisoryNote}`,
		};
	}
	const blockers = verdict.findings.filter(
		(finding) => finding.severity === "blocker",
	).length;
	const approved = verdict.verdict === "approve" && blockers === 0;
	const details = verdict.findings.map(
		(finding) =>
			`[${finding.severity}] ${finding.file}:${finding.line} ${finding.issue}\n  → ${finding.suggestion}`,
	);
	details.push(`verdict: ${verdict.verdict} — ${verdict.summary}`);
	details.push(
		`gate: ${label} — ${approved ? "valid approval" : `approval requirements not met${advisoryNote}`}`,
	);
	return {
		code: strict && !approved ? 1 : 0,
		message: details.join("\n"),
		verdict,
	};
}

function main(argv = process.argv.slice(2)) {
	const [file, ...args] = argv;
	const options = { file };
	const names = {
		"--tool": "tool",
		"--sidecar": "sidecar",
		"--output-sha256": "outputHash",
		"--sidecar-sha256": "sidecarHash",
	};
	for (let index = 0; index < args.length; index += 2) {
		const key = names[args[index]];
		if (!key || !args[index + 1] || Object.hasOwn(options, key)) {
			process.stderr.write("gate: invalid arguments\n");
			return 2;
		}
		options[key] = args[index + 1];
	}
	let preset;
	try {
		preset = selectedPreset(fs.realpathSync(path.resolve(ENTRY_DIRECTORY, "../..")));
	} catch (error) {
		process.stderr.write(`gate: ${error.message}\n`);
		return 2;
	}
	const result = evaluate(options, process.env.GATE_ENFORCE, preset);
	(result.code || !result.verdict ? process.stderr : process.stdout).write(
		`${result.message}\n`,
	);
	return result.code;
}
if (require.main === module) process.exitCode = main();
module.exports = { digest, parseVerdict, readReview, evaluate, gateMode, assuranceModule, selectedPreset, main };
