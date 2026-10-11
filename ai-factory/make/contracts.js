#!/usr/bin/env node
// Artifact contracts are structural evidence, never a judgement of requirement quality.
// Validation only reads: sidecars, the Markdown beside them, evidence records and the
// configured code snapshot. Paths mentioned in artifact prose are never opened.
const fs = require("node:fs");
const path = require("node:path");
const crypto = require("node:crypto");
const { spawn, execFileSync } = require("node:child_process");
const { boundary } = require("./safe-files.js");
const { digest, assuranceModule } = require("./gate.js");
// Preserve the invoked workspace when its entry point is a source-repo symlink (see runner.js).
const ENTRY_DIRECTORY =
	require.main === module ? path.dirname(process.argv[1]) : __dirname;
const ROOT = fs.realpathSync(path.resolve(ENTRY_DIRECTORY, "../.."));

const VERSION = 1;
const SCHEMAS = {
	config: "t4-contracts-config",
	spec: "t4-contract/spec",
	plan: "t4-contract/plan",
	quick: "t4-contract/quick",
	evidence: "t4-contract/evidence",
	attestation: "t4-contract/attestation",
	diagnostics: "t4-contract-diagnostics",
	snapshot: "t4-contract/snapshot",
};
// Kept equal to the `required` arrays in ai-factory/contracts/schema/*.v1.json by check-contracts.sh.
const REQUIRED = {
	config: ["schema", "version", "code_scope"],
	spec: ["schema", "version", "delivery_id", "sha256", "criteria"],
	plan: ["schema", "version", "delivery_id", "sha256", "spec_sha256", "steps"],
	quick: ["schema", "version", "delivery_id", "sha256", "checks", "verify"],
	evidence: [
		"schema",
		"version",
		"delivery_id",
		"scope",
		"phase",
		"expected",
		"inputs",
		"code",
		"commands",
		"status",
		"started_at",
		"finished_at",
	],
	attestation: [
		"schema",
		"version",
		"delivery_id",
		"criterion",
		"actor",
		"attested_at",
		"rationale",
		"source",
		"inputs",
	],
};
// Completion policy defaults (delivery-report.js); config.json may override each key.
const COMPLETION = {
	require_review: true,
	require_review_quick: false,
	blocking_severities: ["blocker"],
	allow_attestation: false,
};
const NOTE =
	"Structural validation only: IDs, references, digests and recorded execution are consistent. It does not judge whether a requirement is correct or complete.";
const DELIVERY = /^d-\d{8}-[0-9a-f]{6}$/;
const SHA = /^[0-9a-f]{64}$/;
const STATUSES = ["passed", "failed", "not_run", "unavailable"];
const PHASES = ["red", "step", "final"];
const RANK = { valid: 0, legacy_unverified: 1, stale: 2, invalid: 3 };
const DIRECTORIES = { specs: "spec", plans: "plan", quick: "quick" };
const SIDECAR = ".contract.json";

class Invocation extends Error {}

function context(root = ROOT) {
	const workspace = path.join(root, "ai-factory");
	return {
		root,
		workspace,
		safe: boundary(workspace),
		code: boundary(root),
		rel: (file) => path.relative(root, file).split(path.sep).join("/"),
		snapshot: undefined,
	};
}
const diag = (ctx, code, file, message, hint) => ({
	code,
	path: ctx.rel(file),
	message,
	hint,
});
function stateOf(diagnostics) {
	if (diagnostics.some((item) => item.code.startsWith("E_"))) return "invalid";
	if (diagnostics.some((item) => item.code.startsWith("S_"))) return "stale";
	if (diagnostics.some((item) => item.code.startsWith("L_")))
		return "legacy_unverified";
	return "valid";
}
const worst = (states) =>
	states.reduce((a, b) => (RANK[b] > RANK[a] ? b : a), "valid");

// --- reading -------------------------------------------------------------------------------
// Reads go through the same lstat walk as workspace writes: no escape, no symlink, no hard link.
function guardPath(ctx, file, guard = ctx.safe) {
	try {
		guard.assertPath(file, { allowMissing: false });
		return null;
	} catch (error) {
		const message = String(error.message);
		if (/outside ai-factory/.test(message))
			return { code: "E_PATH_ESCAPE", message: "path is outside the project boundary" };
		if (/symlink/i.test(message))
			return { code: "E_SYMLINK", message: "symlinked paths are never followed" };
		if (error.code === "ENOENT")
			return { code: "E_MISSING_FILE", message: "file does not exist" };
		return { code: "E_UNREADABLE", message: message.replace(/: \/.*$/, "") };
	}
}
function readInside(ctx, file, guard = ctx.safe) {
	const problem = guardPath(ctx, file, guard);
	if (problem) return problem;
	try {
		return { bytes: fs.readFileSync(file) };
	} catch (error) {
		return { code: "E_UNREADABLE", message: error.code || error.message };
	}
}
function readConfig(ctx) {
	const file = path.join(ctx.workspace, "contracts", "config.json");
	if (!fs.existsSync(file) && !isLink(file))
		return { adopted: false, code_scope: { include: ["**"], exclude: [] }, completion: { ...COMPLETION } };
	const read = readInside(ctx, file);
	if (!read.bytes)
		throw new Invocation(`contracts: ${ctx.rel(file)}: ${read.message}`);
	let value;
	try {
		value = JSON.parse(read.bytes.toString("utf8"));
	} catch {
		throw new Invocation(`contracts: ${ctx.rel(file)}: not valid JSON`);
	}
	const problem = shapeProblem(value, "config");
	if (problem) throw new Invocation(`contracts: ${ctx.rel(file)}: ${problem.message}`);
	const scope = value.code_scope;
	for (const key of ["include", "exclude"]) {
		if (scope[key] === undefined) continue;
		if (
			!Array.isArray(scope[key]) ||
			!scope[key].every(
				(pattern) =>
					typeof pattern === "string" &&
					pattern &&
					!pattern.startsWith("/") &&
					!pattern.split("/").includes(".."),
			)
		)
			throw new Invocation(
				`contracts: ${ctx.rel(file)}: code_scope.${key} must be relative glob strings`,
			);
	}
	const completion = { ...COMPLETION };
	if (value.completion !== undefined) {
		const policy = value.completion;
		if (!policy || typeof policy !== "object" || Array.isArray(policy))
			throw new Invocation(`contracts: ${ctx.rel(file)}: completion must be an object`);
		for (const [key, fallback] of Object.entries(COMPLETION)) {
			if (policy[key] === undefined) continue;
			const ok = Array.isArray(fallback)
				? Array.isArray(policy[key]) && policy[key].every((item) => ["blocker", "major", "minor"].includes(item))
				: typeof policy[key] === "boolean";
			if (!ok) throw new Invocation(`contracts: ${ctx.rel(file)}: completion.${key} has the wrong type`);
			completion[key] = policy[key];
		}
	}
	return {
		adopted: true,
		code_scope: {
			include: scope.include?.length ? scope.include : ["**"],
			exclude: scope.exclude || [],
		},
		completion,
	};
}
// The assurance policy over this configuration and the completion policy it makes effective.
// The configuration itself is never rewritten. Without a selection the completion policy is the
// configured one, unchanged; with one, each requirement is the stronger of the two, so a preset
// can only add requirements. `policy` is null only where make/assurance.js is not installed.
function assurance(ctx, env = process.env) {
	ctx.config = ctx.config || readConfig(ctx);
	const helper = assuranceModule(ctx.root);
	if (!helper) return { policy: null, completion: ctx.config.completion };
	const selection = helper.readSelection(ctx.root);
	const policy = {
		...helper.resolve({ preset: selection.preset, contracts: ctx.config, env }),
		selection: { file: "ai-factory/assurance.json", selected: selection.selected },
	};
	if (selection.preset === null) return { policy, completion: ctx.config.completion };
	const base = ctx.config.completion;
	const settings = policy.settings;
	const severities = ["blocker", "major", "minor"];
	return {
		policy,
		completion: {
			require_review: base.require_review === true || settings.review_required.value === true,
			require_review_quick: base.require_review_quick === true || settings.review_required_quick.value === true,
			blocking_severities: severities.filter((item) => base.blocking_severities.includes(item) || settings.blocking_severities.value.includes(item)),
			allow_attestation: base.allow_attestation,
		},
	};
}
function isLink(file) {
	try {
		return fs.lstatSync(file).isSymbolicLink();
	} catch {
		return false;
	}
}

// --- Markdown ------------------------------------------------------------------------------
// Fenced code is example text, not a declaration: blank it but keep line numbers.
function unfenced(text) {
	let fence = 0;
	let char = "";
	return text.split(/\r?\n/).map((line) => {
		const open = line.trimStart().match(/^(`{3,}|~{3,})/);
		if (open) {
			if (!fence) {
				fence = open[1].length;
				char = open[1][0];
			} else if (
				open[1][0] === char &&
				open[1].length >= fence &&
				!line.trimStart().slice(open[1].length).trim()
			)
				fence = 0;
			return "";
		}
		return fence ? "" : line;
	});
}
// A declaration is a line whose first token is the ID: a table row, list item or paragraph.
function parseIds(text, prefix) {
	const declaration = new RegExp(
		`^\\s*(?:\\|\\s*)?(?:(?:[-*+]|\\d+[.)])\\s+)?(?:\\[[ xX~]\\]\\s+)?(?:\\*\\*|__)?(${prefix}\\d+)(?![0-9A-Za-z])`,
	);
	const ids = [];
	const duplicates = [];
	for (const line of unfenced(text)) {
		const match = line.match(declaration);
		if (!match) continue;
		if (ids.includes(match[1])) duplicates.push(match[1]);
		else ids.push(match[1]);
	}
	return { ids, duplicates: [...new Set(duplicates)] };
}
const parseCriteria = (text) => parseIds(text, "AC");
// Checklist items: `- [ ] QC1 — …`; ticked when every declared item carries [x].
function parseChecks(text) {
	const { ids, duplicates } = parseIds(text, "QC");
	const ticked = new Set();
	for (const line of unfenced(text)) {
		const match = line.match(/^\s*[-*+]\s+\[[xX]\]\s+(?:\*\*|__)?(QC\d+)(?![0-9A-Za-z])/);
		if (match) ticked.add(match[1]);
	}
	return { ids, duplicates, ticked: [...ticked] };
}
// The same step grammar as scripts/state.sh: `- [ ]`, `- [x]`, `- [~]` at column 0 with a
// `Step N` title, and `**Result — Step N withdrawn; …**` withdrawing it.
function parsePlanSteps(text) {
	const lines = unfenced(text);
	const withdrawn = new Set();
	for (const line of lines) {
		const match = line
			.trimStart()
			.match(/^\*\*Result — Step 0*(\d+) (?:is )?withdrawn(?:;|\.|\*\*)/);
		if (match) withdrawn.add(Number(match[1]));
	}
	const steps = [];
	lines.forEach((line, index) => {
		const match = line.match(/^- \[(.?)\](.*)$/);
		if (!match || !" xX~".includes(match[1] || "?")) return;
		const bold = match[2].match(/^[^*]*\*\*([^*]*)\*\*/);
		const title = (bold ? bold[1] : match[2]).trim();
		const step = title.match(/^Step\s+0*(\d+)(?![0-9])/);
		if (!step) return;
		const number = Number(step[1]);
		let body = match[2];
		for (let next = index + 1; next < lines.length; next++) {
			if (!/^\s+\S/.test(lines[next])) break;
			body += `\n${lines[next]}`;
		}
		steps.push({
			id: `S${number}`,
			number,
			mark: match[1],
			done: match[1] === "x" || match[1] === "X",
			withdrawn: withdrawn.has(number),
			text: body,
		});
	});
	return steps;
}
// `Verify (phase): `argv`` — the phase defaults to `step`. Only plain argv is drafted; anything
// that would need a shell is reported for the author to express as separate arguments.
function draftVerify(text) {
	const verify = [];
	const skipped = [];
	const marker = /Verify(?:\s*\((red|step|final)\))?\s*:\s*(?:\*\*)?/gi;
	const found = [...text.matchAll(marker)];
	found.forEach((match, index) => {
		const end = index + 1 < found.length ? found[index + 1].index : text.length;
		const segment = text.slice(match.index + match[0].length, end);
		for (const span of segment.matchAll(/`([^`\n]+)`/g)) {
			const command = span[1].trim();
			if (/[|&;<>()$\\"'*?{}[\]~]/.test(command) || !command) {
				skipped.push(command);
				continue;
			}
			verify.push({
				argv: command.split(/\s+/),
				phase: (match[1] || "step").toLowerCase(),
			});
		}
	});
	return { verify, skipped };
}
// A ticked box is progress, not a content change: marks are normalized before hashing.
function contentDigest(bytes, kind) {
	if (kind === "spec") return digest(bytes);
	return digest(
		Buffer.from(
			bytes.toString("utf8").replace(/^(\s*[-*+] )\[[xX~]\]/gm, "$1[ ]"),
			"utf8",
		),
	);
}

// --- sidecar shape -------------------------------------------------------------------------
function shapeProblem(value, kind) {
	if (!value || typeof value !== "object" || Array.isArray(value))
		return { code: "E_MALFORMED", message: "must be a JSON object" };
	if (value.schema !== SCHEMAS[kind])
		return {
			code: "E_MALFORMED",
			message: `schema must be "${SCHEMAS[kind]}", found ${JSON.stringify(value.schema)}`,
		};
	if (value.version !== VERSION)
		return {
			code: "E_SCHEMA_VERSION",
			message: `unsupported version ${JSON.stringify(value.version)}; this validator supports ${VERSION}`,
			hint:
				Number.isInteger(value.version) && value.version > VERSION
					? "Update the plugin (/t4:sync-sdlc) before trusting this artifact."
					: "Regenerate the sidecar with contracts.js init.",
		};
	const missing = REQUIRED[kind].filter((key) => !Object.hasOwn(value, key));
	if (missing.length)
		return { code: "E_MALFORMED", message: `missing ${missing.join(", ")}` };
	if (kind === "config") {
		if (!value.code_scope || typeof value.code_scope !== "object")
			return { code: "E_MALFORMED", message: "code_scope must be an object" };
		return null;
	}
	if (typeof value.delivery_id !== "string" || !DELIVERY.test(value.delivery_id))
		return { code: "E_MALFORMED", message: "delivery_id must match d-YYYYMMDD-xxxxxx" };
	for (const key of ["sha256", "spec_sha256"])
		if (Object.hasOwn(value, key) && !SHA.test(String(value[key])))
			return { code: "E_MALFORMED", message: `${key} must be a SHA-256 hex digest` };
	return null;
}
function idList(value, pattern, name) {
	if (!Array.isArray(value) || !value.every((id) => typeof id === "string" && pattern.test(id)))
		return { code: "E_MALFORMED", message: `${name} must be an array of IDs` };
	const duplicate = value.find((id, index) => value.indexOf(id) !== index);
	if (duplicate) return { code: "E_DUP_ID", message: `${name} lists ${duplicate} twice` };
	return null;
}
function verifyProblem(verify, where) {
	if (!Array.isArray(verify))
		return { code: "E_MALFORMED", message: `${where} verify must be an array` };
	if (!verify.length)
		return {
			code: "E_NO_VERIFY",
			message: `${where} declares no verification command`,
			hint: "Add `Verify (phase): `command`` to the Markdown and rerun contracts.js init.",
		};
	for (const entry of verify) {
		if (
			!entry ||
			!Array.isArray(entry.argv) ||
			!entry.argv.length ||
			!entry.argv.every((arg) => typeof arg === "string" && arg)
		)
			return { code: "E_MALFORMED", message: `${where} verify argv must be non-empty strings` };
		if (!PHASES.includes(entry.phase))
			return { code: "E_MALFORMED", message: `${where} verify phase must be red, step or final` };
	}
	return null;
}
// Returns { value } or { problem }; never throws on content.
function parseSidecar(bytes, kind) {
	let value;
	try {
		value = JSON.parse(bytes.toString("utf8"));
	} catch {
		return { problem: { code: "E_MALFORMED", message: "not valid JSON" } };
	}
	const problem = shapeProblem(value, kind);
	if (problem) return { problem };
	if (kind === "spec") {
		const listed = idList(value.criteria, /^AC\d+$/, "criteria");
		if (listed) return { problem: listed };
		if (value.tracker_key !== undefined && typeof value.tracker_key !== "string")
			return { problem: { code: "E_MALFORMED", message: "tracker_key must be a string" } };
	}
	if (kind === "quick") {
		const listed = idList(value.checks, /^QC\d+$/, "checks");
		if (listed) return { problem: listed };
		const verify = verifyProblem(value.verify, "quick checklist");
		if (verify) return { problem: verify };
	}
	if (kind === "plan") {
		if (!Array.isArray(value.steps))
			return { problem: { code: "E_MALFORMED", message: "steps must be an array" } };
		const ids = value.steps.map((step) => step?.id);
		const listed = idList(ids, /^S\d+$/, "steps");
		if (listed) return { problem: listed };
		for (const step of value.steps) {
			const criteria = idList(step.criteria, /^AC\d+$/, `${step.id} criteria`);
			if (criteria) return { problem: criteria };
			if (step.withdrawn === true) continue;
			const verify = verifyProblem(step.verify, step.id);
			if (verify) return { problem: verify };
		}
	}
	return { value };
}

// --- discovery -----------------------------------------------------------------------------
function scan(ctx) {
	const index = { sidecars: [], markdown: [], problems: [] };
	const visit = (directory, kind, depth) => {
		const problem = guardPath(ctx, directory);
		if (problem?.code === "E_MISSING_FILE") return;
		if (problem) {
			index.problems.push({ file: directory, kind, ...problem });
			return;
		}
		for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
			const file = path.join(directory, entry.name);
			const relevant = entry.name.endsWith(SIDECAR) || entry.name.endsWith(".md");
			if (entry.isSymbolicLink()) {
				if (relevant)
					index.problems.push({
						file,
						kind,
						code: "E_SYMLINK",
						message: "symlinked artifacts are never followed",
					});
				continue;
			}
			if (entry.isDirectory()) {
				if (depth === 0 && kind === "plan") visit(file, kind, 1);
				continue;
			}
			if (entry.name.endsWith(SIDECAR)) index.sidecars.push({ file, kind });
			else if (entry.name.endsWith(".md") && !/^0000-/.test(entry.name))
				index.markdown.push({ file, kind });
		}
	};
	for (const [directory, kind] of Object.entries(DIRECTORIES))
		visit(path.join(ctx.workspace, directory), kind, 0);
	for (const sidecar of index.sidecars) loadSidecar(ctx, sidecar);
	return index;
}
function loadSidecar(ctx, sidecar) {
	sidecar.markdownFile = sidecar.file.slice(0, -SIDECAR.length) + ".md";
	const read = readInside(ctx, sidecar.file);
	if (!read.bytes) {
		sidecar.problem = read;
		return sidecar;
	}
	const parsed = parseSidecar(read.bytes, sidecar.kind);
	if (parsed.problem) sidecar.problem = parsed.problem;
	else sidecar.value = parsed.value;
	return sidecar;
}

// --- code snapshot -------------------------------------------------------------------------
function globRegex(pattern) {
	let source = "";
	for (let index = 0; index < pattern.length; index++) {
		const char = pattern[index];
		if (char === "*" && pattern[index + 1] === "*") {
			index++;
			if (pattern[index + 1] === "/") {
				index++;
				source += "(?:.*/)?";
			} else source += ".*";
		} else if (char === "*") source += "[^/]*";
		else if (char === "?") source += "[^/]";
		else source += char.replace(/[.+^${}()|[\]\\]/g, "\\$&");
	}
	// A directory pattern also covers everything beneath it.
	return new RegExp(`^${source}(?:/.*)?$`);
}
function git(ctx, args) {
	return execFileSync("git", args, {
		cwd: ctx.root,
		encoding: "utf8",
		stdio: ["ignore", "pipe", "ignore"],
		env: { ...process.env, GIT_OPTIONAL_LOCKS: "0" },
		maxBuffer: 256 * 1024 * 1024,
	});
}
// Tracked, modified and untracked-but-not-ignored files within the configured scope. ai-factory/
// is never part of it, so writing evidence, reports or telemetry cannot invalidate evidence.
function snapshot(ctx, config = readConfig(ctx)) {
	let listed;
	try {
		listed = git(ctx, ["ls-files", "-z", "--cached", "--others", "--exclude-standard", "--"]);
	} catch {
		return null;
	}
	const include = config.code_scope.include.map(globRegex);
	const exclude = config.code_scope.exclude.map(globRegex);
	const files = [...new Set(listed.split("\0").filter(Boolean))]
		.filter(
			(file) =>
				file !== "ai-factory" &&
				!file.startsWith("ai-factory/") &&
				include.some((pattern) => pattern.test(file)) &&
				!exclude.some((pattern) => pattern.test(file)),
		)
		.sort();
	const lines = [];
	const checked = new Set([ctx.root]);
	for (const file of files) {
		const absolute = path.join(ctx.root, file);
		let entry;
		try {
			const parent = path.dirname(absolute);
			if (!checked.has(parent)) {
				ctx.code.assertPath(parent, { allowMissing: false });
				checked.add(parent);
			}
			const stat = fs.lstatSync(absolute);
			// A link is hashed by its target text and never followed.
			if (stat.isSymbolicLink()) entry = `link:${digest(fs.readlinkSync(absolute))}`;
			else if (stat.isFile()) entry = digest(fs.readFileSync(absolute));
			else entry = "other";
		} catch (error) {
			entry = error.code === "ENOENT" ? "deleted" : "unreadable";
		}
		lines.push(`${file}\0${entry}\n`);
	}
	let head = null;
	try {
		head = git(ctx, ["rev-parse", "--verify", "-q", "HEAD"]).trim() || null;
	} catch {}
	return { snapshot_sha256: digest(lines.join("")), head, files: files.length };
}
function currentSnapshot(ctx) {
	if (ctx.snapshot === undefined) ctx.snapshot = snapshot(ctx, ctx.config);
	return ctx.snapshot;
}

// --- validation ----------------------------------------------------------------------------
function markdownOf(ctx, sidecar, diagnostics) {
	const read = readInside(ctx, sidecar.markdownFile);
	if (!read.bytes) {
		diagnostics.push(
			diag(ctx, read.code, sidecar.markdownFile, `artifact: ${read.message}`, "Every sidecar sits beside the Markdown it describes; rename both together."),
		);
		return null;
	}
	return {
		bytes: read.bytes,
		text: read.bytes.toString("utf8"),
		sha256: contentDigest(read.bytes, sidecar.kind),
	};
}
function reviewed(ctx, sidecar, diagnostics) {
	if (sidecar.value.review_required === true)
		diagnostics.push(
			diag(ctx, "L_REVIEW_REQUIRED", sidecar.file, "drafted by migration and not yet reviewed", "Check the IDs and commands, then rerun contracts.js init on the Markdown."),
		);
}
function checkSpec(ctx, sidecar) {
	const diagnostics = [];
	const md = markdownOf(ctx, sidecar, diagnostics);
	if (md) {
		const { ids, duplicates } = parseCriteria(md.text);
		for (const id of duplicates)
			diagnostics.push(diag(ctx, "E_DUP_ID", sidecar.markdownFile, `${id} is declared more than once`, "Give each acceptance criterion a unique number."));
		if (!ids.length)
			diagnostics.push(diag(ctx, "E_MISSING_ID", sidecar.markdownFile, "no acceptance criteria IDs (AC1…) are declared", "Number every criterion AC1, AC2, …"));
		if (md.sha256 !== sidecar.value.sha256)
			diagnostics.push(diag(ctx, "S_ARTIFACT_CHANGED", sidecar.markdownFile, "the spec changed after its sidecar was written", "Rerun contracts.js init spec on it, then re-plan and re-verify."));
		else {
			for (const id of sidecar.value.criteria.filter((id) => !ids.includes(id)))
				diagnostics.push(diag(ctx, "E_MISSING_ID", sidecar.file, `${id} is listed but not declared in the spec`, "Rerun contracts.js init spec."));
			for (const id of ids.filter((id) => !sidecar.value.criteria.includes(id)))
				diagnostics.push(diag(ctx, "E_MISSING_ID", sidecar.file, `${id} is declared in the spec but missing from the sidecar`, "Rerun contracts.js init spec."));
		}
	}
	reviewed(ctx, sidecar, diagnostics);
	return { md, diagnostics };
}
function checkPlan(ctx, sidecar, spec) {
	const diagnostics = [];
	const md = markdownOf(ctx, sidecar, diagnostics);
	const value = sidecar.value;
	let steps = [];
	if (md) {
		steps = parsePlanSteps(md.text);
		const numbers = steps.map((step) => step.id);
		for (const id of numbers.filter((id, index) => numbers.indexOf(id) !== index))
			diagnostics.push(diag(ctx, "E_DUP_ID", sidecar.markdownFile, `Step ${id.slice(1)} appears more than once`, "Number plan steps uniquely."));
		if (!steps.length)
			diagnostics.push(diag(ctx, "E_MISSING_ID", sidecar.markdownFile, "no `- [ ] Step N — …` lines found", "Write each step as `- [ ] **Step N — title.**`."));
		if (md.sha256 !== value.sha256)
			diagnostics.push(diag(ctx, "S_ARTIFACT_CHANGED", sidecar.markdownFile, "the plan changed after its sidecar was written", "Rerun contracts.js init plan on it."));
		else {
			const declared = value.steps.map((step) => step.id);
			for (const id of declared.filter((id) => !numbers.includes(id)))
				diagnostics.push(diag(ctx, "E_UNKNOWN_REF", sidecar.file, `${id} is not a step in the plan`, "Rerun contracts.js init plan."));
			for (const id of numbers.filter((id) => !declared.includes(id)))
				diagnostics.push(diag(ctx, "E_MISSING_ID", sidecar.file, `${id} is missing from the sidecar`, "Rerun contracts.js init plan."));
		}
	}
	if (!spec)
		diagnostics.push(diag(ctx, "E_UNKNOWN_REF", sidecar.file, "no spec sidecar carries this delivery_id", "Run contracts.js init spec on the spec first."));
	else if (spec.md && spec.md.sha256 !== value.spec_sha256)
		diagnostics.push(diag(ctx, "S_SPEC_CHANGED", sidecar.file, "the spec changed after this plan was written", "Review the plan against the spec, then rerun contracts.js init plan."));
	if (spec?.sidecar.value) {
		const criteria = spec.sidecar.value.criteria;
		const active = value.steps.filter((step) => step.withdrawn !== true);
		for (const step of value.steps)
			for (const id of step.criteria.filter((id) => !criteria.includes(id)))
				diagnostics.push(diag(ctx, "E_UNKNOWN_REF", sidecar.file, `${step.id} references ${id}, which the spec does not declare`, "Fix the AC reference in the plan step."));
		for (const id of criteria.filter((id) => !active.some((step) => step.criteria.includes(id))))
			diagnostics.push(diag(ctx, "E_UNCOVERED_AC", sidecar.file, `${id} is covered by no plan step`, "Name the AC in the step that proves it."));
		if (!active.some((step) => step.verify.some((entry) => entry.phase === "final")))
			diagnostics.push(diag(ctx, "E_NO_VERIFY", sidecar.file, "the plan declares no final verification command", "Add `Verify (final): `command`` to the last step."));
	}
	reviewed(ctx, sidecar, diagnostics);
	return { md, steps, diagnostics };
}
function checkQuick(ctx, sidecar) {
	const diagnostics = [];
	const md = markdownOf(ctx, sidecar, diagnostics);
	let checks = { ids: [], ticked: [] };
	if (md) {
		checks = parseChecks(md.text);
		for (const id of checks.duplicates)
			diagnostics.push(diag(ctx, "E_DUP_ID", sidecar.markdownFile, `${id} is declared more than once`, "Number checklist items uniquely."));
		if (!checks.ids.length)
			diagnostics.push(diag(ctx, "E_MISSING_ID", sidecar.markdownFile, "no checklist items (QC1…) are declared", "Write each item as `- [ ] QC1 — …`."));
		if (md.sha256 !== sidecar.value.sha256)
			diagnostics.push(diag(ctx, "S_ARTIFACT_CHANGED", sidecar.markdownFile, "the checklist changed after its sidecar was written", "Rerun contracts.js init quick on it."));
		else
			for (const id of [
				...sidecar.value.checks.filter((id) => !checks.ids.includes(id)),
				...checks.ids.filter((id) => !sidecar.value.checks.includes(id)),
			])
				diagnostics.push(diag(ctx, "E_MISSING_ID", sidecar.file, `${id} differs between checklist and sidecar`, "Rerun contracts.js init quick."));
	}
	reviewed(ctx, sidecar, diagnostics);
	return { md, checks, diagnostics };
}
const EVIDENCE_NAME = /^(?:(S\d+)-(red|step)|(final|quick|review)|((?:AC|QC)\d+)-attest)\.json$/;
function evidenceShape(name) {
	const match = name.match(EVIDENCE_NAME);
	if (!match) return null;
	if (match[4]) return { kind: "attest", criterion: match[4] };
	if (match[1])
		return {
			kind: "step",
			step: match[1],
			phase: match[2],
			expected: match[2] === "red" ? "fail" : "pass",
		};
	return { kind: match[3], phase: "final", expected: "pass" };
}
function readEvidence(ctx, id) {
	const directory = path.join(ctx.workspace, "evidence", id);
	const records = new Map();
	const problems = [];
	const problem = guardPath(ctx, directory);
	if (problem?.code === "E_MISSING_FILE") return { records, problems };
	if (problem) {
		problems.push({ file: directory, ...problem });
		return { records, problems };
	}
	for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
		const file = path.join(directory, entry.name);
		const shape = evidenceShape(entry.name);
		if (!shape) {
			problems.push({ file, code: "E_MALFORMED", message: "unexpected evidence file name" });
			continue;
		}
		const bytes = readInside(ctx, file);
		if (!bytes.bytes) {
			problems.push({ file, code: bytes.code, message: bytes.message });
			continue;
		}
		let value;
		try {
			value = JSON.parse(bytes.bytes.toString("utf8"));
		} catch {
			problems.push({ file, code: "E_MALFORMED", message: "not valid JSON" });
			continue;
		}
		const problem = shapeProblem(value, shape.kind === "attest" ? "attestation" : "evidence");
		if (problem) {
			problems.push({ file, ...problem });
			continue;
		}
		records.set(entry.name, { file, shape, value });
	}
	return { records, problems };
}
// Evidence proves only what it recorded: its inputs and code must still be current.
function checkEvidence(ctx, record, current, id, { code = true } = {}) {
	const diagnostics = [];
	const { file, shape, value } = record;
	const at = (codeName, message, hint) => diagnostics.push(diag(ctx, codeName, file, message, hint));
	if (value.delivery_id !== id) at("E_MALFORMED", "delivery_id does not match its directory");
	if (shape.kind !== "step" && (value.phase === "red" || value.expected === "fail"))
		at("E_RED_NOT_FINAL", "a red (expected-failure) run cannot stand as final verification", "Run the final phase after the implementation.");
	else if (
		value.scope?.kind !== shape.kind ||
		(shape.step && value.scope?.step !== shape.step) ||
		value.phase !== shape.phase ||
		value.expected !== shape.expected
	)
		at("E_MALFORMED", "scope, phase or expectation does not match the file name");
	if (!STATUSES.includes(value.status)) at("E_MALFORMED", `unknown status ${JSON.stringify(value.status)}`);
	if (!Array.isArray(value.commands)) at("E_MALFORMED", "commands must be an array");
	if (
		value.output_ref !== undefined &&
		(typeof value.output_ref !== "string" ||
			!/^ai-factory\/runs\/evidence\/[^/]+\/[^/]+$/.test(value.output_ref) ||
			value.output_ref.split("/").includes(".."))
	)
		at("E_PATH_ESCAPE", "output_ref must name a file under ai-factory/runs/evidence/");
	if (diagnostics.length) return diagnostics;
	if (shape.expected === "fail") {
		if (value.status === "passed")
			at("E_RED_PASSED", "the red tests passed before the implementation existed", "A test that passes before the code exists is a broken test; fix the test.");
	} else if (value.status === "failed")
		at("E_EVIDENCE_FAILED", "the recorded verification failed", "Fix the failure and record verification again.");
	if (value.status === "not_run")
		at("E_EVIDENCE_NOT_RUN", `verification was not run${value.reason ? `: ${value.reason}` : ""}`, "Run make verify for this scope.");
	if (value.status === "unavailable")
		at("E_EVIDENCE_UNAVAILABLE", "a verification command was unavailable", "Install the command or change the declared verification; unavailable is never a pass.");
	if (shape.phase === "red") return diagnostics;
	const inputs = value.inputs || {};
	for (const [key, codeName, label] of [
		["spec_sha256", "S_SPEC_CHANGED", "spec"],
		["plan_sha256", "S_PLAN_CHANGED", "plan"],
		["quick_sha256", "S_ARTIFACT_CHANGED", "checklist"],
	]) {
		if (current[key] === undefined) continue;
		if (inputs[key] !== current[key])
			at(codeName, `the ${label} changed after this evidence was recorded`, "Record verification again.");
	}
	const now = currentSnapshot(ctx);
	if (!value.code || !SHA.test(String(value.code.snapshot_sha256)))
		at("E_MALFORMED", "code.snapshot_sha256 is missing");
	else if (!now) at("E_CODE_UNAVAILABLE", "the current code snapshot cannot be computed (is this a Git repository?)");
	else if (value.code.changed_during_run)
		at("E_EVIDENCE_UNSTABLE", "in-scope files changed while the verification ran", "Exclude generated output via code_scope.exclude in ai-factory/contracts/config.json.");
	else if (value.code.snapshot_sha256 !== now.snapshot_sha256) {
		if (code) at("S_CODE_CHANGED", "in-scope code changed after this evidence was recorded", "Record verification again for the current code.");
		else at("I_CODE_ADVANCED", "later changes advanced the code since this step was verified; final verification covers the current code");
	}
	return diagnostics;
}
// A human statement about one criterion. It records who, when, why and against which content;
// whether it may count toward completion is the report's policy decision, not the validator's.
function checkAttestation(ctx, record, declared, current, id) {
	const diagnostics = [];
	const { file, shape, value } = record;
	const at = (codeName, message, hint) => diagnostics.push(diag(ctx, codeName, file, message, hint));
	if (value.delivery_id !== id) at("E_MALFORMED", "delivery_id does not match its directory");
	if (value.criterion !== shape.criterion) at("E_MALFORMED", "criterion does not match the file name");
	for (const key of ["actor", "rationale", "source", "attested_at"])
		if (typeof value[key] !== "string" || !value[key].trim()) at("E_MALFORMED", `${key} must be a non-empty string`);
	if (!declared.includes(shape.criterion))
		at("E_UNKNOWN_REF", `${shape.criterion} is not declared by this delivery`, "Attest only a declared criterion.");
	const inputs = value.inputs || {};
	if (current.spec_sha256 !== undefined && inputs.spec_sha256 !== current.spec_sha256)
		at("S_SPEC_CHANGED", "the spec changed after this attestation", "Attest again against the current spec.");
	if (current.quick_sha256 !== undefined && inputs.quick_sha256 !== current.quick_sha256)
		at("S_ARTIFACT_CHANGED", "the checklist changed after this attestation", "Attest again against the current checklist.");
	return diagnostics;
}
function entry(ctx, file, kind, diagnostics) {
	return { path: ctx.rel(file), kind, state: stateOf(diagnostics), diagnostics };
}
function validateDelivery(ctx, index, id, require = []) {
	const artifacts = [];
	const mine = index.sidecars.filter((sidecar) => sidecar.value?.delivery_id === id);
	const byKind = (kind) => mine.filter((sidecar) => sidecar.kind === kind);
	for (const kind of ["spec", "plan", "quick"])
		if (byKind(kind).length > 1)
			for (const sidecar of byKind(kind))
				artifacts.push(entry(ctx, sidecar.file, kind, [diag(ctx, "E_DUP_ID", sidecar.file, `more than one ${kind} sidecar carries ${id}`, "Each delivery has one spec, one plan, or one quick checklist.")]));
	if (artifacts.length) return artifacts;
	const [specSidecar] = byKind("spec");
	const [planSidecar] = byKind("plan");
	const [quickSidecar] = byKind("quick");
	const evidence = readEvidence(ctx, id);
	for (const problem of evidence.problems)
		artifacts.push(entry(ctx, problem.file, "evidence", [diag(ctx, problem.code, problem.file, problem.message, problem.hint)]));
	if (!mine.length) {
		artifacts.push({
			path: `ai-factory/evidence/${id}`,
			kind: "delivery",
			state: "invalid",
			diagnostics: [{ code: "E_UNKNOWN_REF", path: `ai-factory/evidence/${id}`, message: `no sidecar carries delivery ${id}`, hint: "Check the delivery ID, or run contracts.js init on its artifact." }],
		});
		return artifacts;
	}
	if (quickSidecar && (specSidecar || planSidecar)) {
		artifacts.push(entry(ctx, quickSidecar.file, "quick", [diag(ctx, "E_MALFORMED", quickSidecar.file, "a delivery is either planned (spec + plan) or quick, not both")]));
		return artifacts;
	}
	const current = {};
	const want = new Set(require);
	if (quickSidecar) {
		const quick = checkQuick(ctx, quickSidecar);
		artifacts.push(entry(ctx, quickSidecar.file, "quick", quick.diagnostics));
		if (quick.md) current.quick_sha256 = quick.md.sha256;
		const all = quick.checks.ids.length && quick.checks.ids.every((qc) => quick.checks.ticked.includes(qc));
		if (all) want.add("final");
		const record = evidence.records.get("quick.json");
		if (record) artifacts.push(entry(ctx, record.file, "evidence", checkEvidence(ctx, record, current, id)));
		else if (want.has("final"))
			artifacts.push(missingEvidence(ctx, id, "quick.json", all ? "every checklist item is ticked but no quick verification was recorded" : "quick verification is required"));
	} else {
		let spec = null;
		if (specSidecar) {
			const checked = checkSpec(ctx, specSidecar);
			spec = { sidecar: specSidecar, md: checked.md };
			artifacts.push(entry(ctx, specSidecar.file, "spec", checked.diagnostics));
			if (checked.md) current.spec_sha256 = checked.md.sha256;
		}
		if (planSidecar) {
			const plan = checkPlan(ctx, planSidecar, spec);
			artifacts.push(entry(ctx, planSidecar.file, "plan", plan.diagnostics));
			if (plan.md) current.plan_sha256 = plan.md.sha256;
			const active = plan.steps.filter((step) => !step.withdrawn);
			// A ticked box is a claim; only passing step evidence supports it.
			for (const step of active) {
				const record = evidence.records.get(`${step.id}-step.json`);
				if (record)
					artifacts.push(entry(ctx, record.file, "evidence", step.done ? checkEvidence(ctx, record, current, id, { code: false }) : quiet(checkEvidence(ctx, record, current, id, { code: false }))));
				else if (step.done)
					artifacts.push(missingEvidence(ctx, id, `${step.id}-step.json`, `Step ${step.number} is ticked but has no recorded verification`));
				const red = evidence.records.get(`${step.id}-red.json`);
				if (red) artifacts.push(entry(ctx, red.file, "evidence", checkEvidence(ctx, red, current, id)));
			}
			if (active.length && active.every((step) => step.done)) want.add("final");
		}
		const record = evidence.records.get("final.json");
		if (record) artifacts.push(entry(ctx, record.file, "evidence", checkEvidence(ctx, record, current, id)));
		else if (want.has("final"))
			artifacts.push(missingEvidence(ctx, id, "final.json", "final verification is required but was not recorded"));
	}
	const declared = quickSidecar?.value?.checks || specSidecar?.value?.criteria || [];
	for (const record of evidence.records.values())
		if (record.shape.kind === "attest")
			artifacts.push(entry(ctx, record.file, "attestation", checkAttestation(ctx, record, declared, current, id)));
	const review = evidence.records.get("review.json");
	if (review) artifacts.push(entry(ctx, review.file, "evidence", checkEvidence(ctx, review, current, id)));
	else if (want.has("review"))
		artifacts.push(missingEvidence(ctx, id, "review.json", "an independent review was required but none was recorded"));
	// Evidence for a scope this delivery does not have proves nothing and is reported.
	const seen = new Set(artifacts.map((item) => item.path));
	for (const record of evidence.records.values())
		if (!seen.has(ctx.rel(record.file)))
			artifacts.push(entry(ctx, record.file, "evidence", [diag(ctx, "E_UNKNOWN_REF", record.file, "evidence for a step or scope this delivery does not declare", "Remove it, or restore the step it verified.")]));
	return artifacts;
}
// An unticked step's failed attempt is ordinary progress, not a broken chain.
function quiet(diagnostics) {
	return diagnostics.map((item) =>
		["E_EVIDENCE_FAILED", "E_EVIDENCE_NOT_RUN", "E_EVIDENCE_UNAVAILABLE"].includes(item.code)
			? { ...item, code: `I_${item.code.slice(2)}` }
			: item,
	);
}
function missingEvidence(ctx, id, name, message) {
	const file = path.join(ctx.workspace, "evidence", id, name);
	return entry(ctx, file, "evidence", [diag(ctx, "E_EVIDENCE_NOT_RUN", file, `not_run: ${message}`, "Run make verify for this scope; a checked box is not execution proof.")]);
}
function report(ctx, artifacts, delivery) {
	artifacts.sort((a, b) => (a.path < b.path ? -1 : a.path > b.path ? 1 : 0));
	return {
		schema: SCHEMAS.diagnostics,
		version: VERSION,
		status: worst(artifacts.map((item) => item.state)),
		delivery_id: delivery || null,
		adopted: ctx.config.adopted,
		note: NOTE,
		artifacts,
	};
}
function validate(options = {}) {
	const ctx = options.ctx || context(options.root);
	ctx.config = ctx.config || readConfig(ctx);
	const require = options.require || [];
	const index = scan(ctx);
	const artifacts = [];
	const broken = (sidecar) =>
		entry(ctx, sidecar.file, sidecar.kind, [diag(ctx, sidecar.problem.code, sidecar.file, sidecar.problem.message, sidecar.problem.hint)]);
	if (options.target && !DELIVERY.test(options.target)) {
		// A path: the Markdown or its sidecar. Legacy Markdown has no sidecar.
		const file = path.resolve(ctx.root, options.target);
		const sidecarFile = file.endsWith(SIDECAR) ? file : file.replace(/\.md$/, "") + SIDECAR;
		const markdownFile = sidecarFile.slice(0, -SIDECAR.length) + ".md";
		const guard = readInside(ctx, file);
		const relative = path.relative(ctx.workspace, file).split(path.sep);
		const kind = DIRECTORIES[relative[0]];
		if (!guard.bytes) artifacts.push(entry(ctx, file, kind || "unknown", [diag(ctx, guard.code, file, guard.message)]));
		else if (!kind || !/\.(md|contract\.json)$/.test(file))
			artifacts.push(entry(ctx, file, "unknown", [diag(ctx, "E_UNKNOWN_REF", file, "not a spec, plan or quick artifact", "Pass a file under ai-factory/specs/, plans/ or quick/, or a delivery ID.")]));
		else {
			const sidecar = index.sidecars.find((item) => item.file === sidecarFile);
			if (!sidecar)
				artifacts.push(entry(ctx, markdownFile, kind, [diag(ctx, "L_NO_SIDECAR", markdownFile, "no contract sidecar; this artifact is unverified", "Run contracts.js migrate, or contracts.js init on it.")]));
			else if (sidecar.problem) artifacts.push(broken(sidecar));
			else return report(ctx, validateDelivery(ctx, index, sidecar.value.delivery_id, require), sidecar.value.delivery_id);
		}
		return report(ctx, artifacts, null);
	}
	for (const problem of index.problems)
		artifacts.push(entry(ctx, problem.file, problem.kind, [diag(ctx, problem.code, problem.file, problem.message)]));
	if (options.target) return report(ctx, [...artifacts, ...validateDelivery(ctx, index, options.target, require)], options.target);
	for (const sidecar of index.sidecars.filter((item) => item.problem)) artifacts.push(broken(sidecar));
	const ids = new Set(index.sidecars.filter((item) => item.value).map((item) => item.value.delivery_id));
	const evidenceRoot = path.join(ctx.workspace, "evidence");
	const evidenceProblem = guardPath(ctx, evidenceRoot);
	if (evidenceProblem?.code !== "E_MISSING_FILE") {
		if (evidenceProblem)
			artifacts.push(entry(ctx, evidenceRoot, "evidence", [diag(ctx, evidenceProblem.code, evidenceRoot, evidenceProblem.message)]));
		else
			for (const item of fs.readdirSync(evidenceRoot, { withFileTypes: true })) {
				if (item.name.startsWith(".")) continue;
				if (item.isDirectory() && DELIVERY.test(item.name)) ids.add(item.name);
				else artifacts.push(entry(ctx, path.join(evidenceRoot, item.name), "evidence", [diag(ctx, item.isSymbolicLink() ? "E_SYMLINK" : "E_MALFORMED", path.join(evidenceRoot, item.name), "evidence entries are directories named by delivery ID")]));
			}
	}
	for (const id of [...ids].sort()) artifacts.push(...validateDelivery(ctx, index, id, require));
	for (const md of index.markdown) {
		const sidecar = md.file.replace(/\.md$/, "") + SIDECAR;
		if (!index.sidecars.some((item) => item.file === sidecar))
			artifacts.push(entry(ctx, md.file, md.kind, [diag(ctx, "L_NO_SIDECAR", md.file, "no contract sidecar; this artifact is unverified", "Run contracts.js migrate, or contracts.js init on it.")]));
	}
	return report(ctx, artifacts, null);
}
function summary(doc) {
	const lines = [`contracts: ${doc.status}${doc.delivery_id ? ` — ${doc.delivery_id}` : ""}`];
	if (!doc.adopted) lines.push("  (contracts are not adopted here; run contracts.js enable to opt in)");
	for (const artifact of doc.artifacts) {
		lines.push(`  [${artifact.state}] ${artifact.path}`);
		for (const item of artifact.diagnostics) {
			lines.push(`    ${item.code} ${item.path}: ${item.message}`);
			if (item.hint) lines.push(`      → ${item.hint}`);
		}
	}
	if (!doc.artifacts.length) lines.push("  no contract artifacts found");
	lines.push(`note: ${doc.note}`);
	return lines.join("\n");
}

// --- writing: init, record, enable, migrate --------------------------------------------------
function newDelivery() {
	const date = new Date().toISOString().slice(0, 10).replace(/-/g, "");
	return `d-${date}-${crypto.randomBytes(3).toString("hex")}`;
}
function artifactFile(ctx, target, kind) {
	if (!target) throw new Invocation(`contracts: init ${kind} needs a Markdown path`);
	const file = path.resolve(ctx.root, target);
	const relative = path.relative(ctx.workspace, file).split(path.sep);
	if (DIRECTORIES[relative[0]] !== kind || !file.endsWith(".md"))
		throw new Invocation(`contracts: ${ctx.rel(file)} is not a ${kind} Markdown file under ai-factory/${Object.keys(DIRECTORIES).find((key) => DIRECTORIES[key] === kind)}/`);
	const read = readInside(ctx, file);
	if (!read.bytes) throw new Invocation(`contracts: ${ctx.rel(file)}: ${read.message}`);
	return { file, bytes: read.bytes, text: read.bytes.toString("utf8"), sidecar: file.replace(/\.md$/, "") + SIDECAR };
}
function existing(ctx, file, kind) {
	if (!fs.existsSync(file) && !isLink(file)) return null;
	const loaded = loadSidecar(ctx, { file, kind });
	if (loaded.problem?.code === "E_PATH_ESCAPE" || loaded.problem?.code === "E_SYMLINK")
		throw new Invocation(`contracts: ${ctx.rel(file)}: ${loaded.problem.message}`);
	return loaded.value || null;
}
function writeJson(ctx, file, value) {
	ctx.safe.mkdir(path.dirname(file));
	ctx.safe.write(file, `${JSON.stringify(value, null, 2)}\n`);
}
function specSidecarFor(ctx, markdown) {
	const art = artifactFile(ctx, markdown, "spec");
	const value = existing(ctx, art.sidecar, "spec");
	if (!value) throw new Invocation(`contracts: ${ctx.rel(art.sidecar)} does not exist; run init spec first`);
	return { art, value };
}
function init(kind, target, options = {}) {
	const ctx = options.ctx || context(options.root);
	ctx.config = ctx.config || readConfig(ctx);
	const art = artifactFile(ctx, target, kind);
	const previous = existing(ctx, art.sidecar, kind);
	const delivery = previous?.delivery_id || options.delivery || newDelivery();
	const notes = [];
	let value;
	if (kind === "spec") {
		const { ids } = parseCriteria(art.text);
		const ticket = art.text.match(/^\s*\**Ticket:\**\s*([A-Z][A-Z0-9]+-\d+)/m);
		const tracker = options.tracker || previous?.tracker_key || ticket?.[1];
		value = { schema: SCHEMAS.spec, version: VERSION, delivery_id: delivery, ...(tracker ? { tracker_key: tracker } : {}), sha256: contentDigest(art.bytes, "spec"), criteria: ids };
	} else if (kind === "plan") {
		const spec = options.spec
			? specSidecarFor(ctx, options.spec)
			: previous && findSpec(ctx, previous.delivery_id);
		if (!spec) throw new Invocation("contracts: init plan needs --spec <spec path>");
		if (previous && previous.delivery_id !== spec.value.delivery_id)
			throw new Invocation("contracts: this plan already belongs to another delivery");
		const oldSteps = new Map((previous?.steps || []).map((step) => [step.id, step]));
		const steps = parsePlanSteps(art.text).map((step) => {
			const drafted = draftVerify(step.text);
			for (const command of drafted.skipped) notes.push(`${step.id}: not drafted, needs plain argv: ${command}`);
			const criteria = [...new Set(step.text.match(/\bAC\d+\b/g) || [])];
			const verify = drafted.verify.length ? drafted.verify : oldSteps.get(step.id)?.verify || [];
			return { id: step.id, criteria, verify, ...(step.withdrawn ? { withdrawn: true } : {}) };
		});
		value = { schema: SCHEMAS.plan, version: VERSION, delivery_id: spec.value.delivery_id, sha256: contentDigest(art.bytes, "plan"), spec_sha256: spec.value.sha256, steps };
		return finish(ctx, art, value, notes, kind);
	} else {
		const { ids } = parseChecks(art.text);
		const drafted = draftVerify(art.text);
		for (const command of drafted.skipped) notes.push(`not drafted, needs plain argv: ${command}`);
		value = { schema: SCHEMAS.quick, version: VERSION, delivery_id: delivery, sha256: contentDigest(art.bytes, "quick"), checks: ids, verify: drafted.verify.length ? drafted.verify : previous?.verify || [] };
	}
	return finish(ctx, art, value, notes, kind);
}
function findSpec(ctx, delivery) {
	const sidecar = scan(ctx).sidecars.find((item) => item.kind === "spec" && item.value?.delivery_id === delivery);
	return sidecar ? { value: sidecar.value } : null;
}
function finish(ctx, art, value, notes, kind) {
	if (kind === "plan") {
		// The spec digest a plan records is the spec's current content, not its sidecar's memory.
		const spec = scan(ctx).sidecars.find((item) => item.kind === "spec" && item.value?.delivery_id === value.delivery_id);
		const read = spec && readInside(ctx, spec.markdownFile);
		if (read?.bytes) value.spec_sha256 = contentDigest(read.bytes, "spec");
	}
	writeJson(ctx, art.sidecar, value);
	const doc = validate({ ctx: { ...ctx, snapshot: undefined }, target: value.delivery_id });
	return { sidecar: art.sidecar, value, notes, doc };
}
function scopeFor(ctx, delivery, step, phase) {
	const index = scan(ctx);
	const mine = index.sidecars.filter((item) => item.value?.delivery_id === delivery);
	const quick = mine.find((item) => item.kind === "quick");
	const plan = mine.find((item) => item.kind === "plan");
	const spec = mine.find((item) => item.kind === "spec");
	const inputs = {};
	const digestOf = (sidecar) => {
		const read = readInside(ctx, sidecar.markdownFile);
		if (!read.bytes) throw new Invocation(`contracts: ${ctx.rel(sidecar.markdownFile)}: ${read.message}`);
		return contentDigest(read.bytes, sidecar.kind);
	};
	if (quick) {
		if (!quick.value) throw new Invocation(`contracts: ${ctx.rel(quick.file)} is invalid; validate it first`);
		if (step || (phase && phase !== "final")) throw new Invocation("contracts: quick work records one final verification; omit STEP and PHASE");
		inputs.quick_sha256 = digestOf(quick);
		return { name: "quick.json", scope: { kind: "quick" }, phase: "final", expected: "pass", commands: quick.value.verify, inputs };
	}
	if (!plan?.value) throw new Invocation(`contracts: no valid plan sidecar carries ${delivery}`);
	if (spec) inputs.spec_sha256 = digestOf(spec);
	inputs.plan_sha256 = digestOf(plan);
	if (step) {
		const declared = plan.value.steps.find((item) => item.id === step);
		if (!declared) throw new Invocation(`contracts: ${step} is not a step of this plan`);
		const wanted = phase || "step";
		if (!["red", "step"].includes(wanted)) throw new Invocation("contracts: a step records the red or step phase; use PHASE=final without STEP for final verification");
		return { name: `${step}-${wanted}.json`, scope: { kind: "step", step }, phase: wanted, expected: wanted === "red" ? "fail" : "pass", commands: declared.verify.filter((entry) => entry.phase === wanted), inputs };
	}
	if (phase && phase !== "final") throw new Invocation("contracts: the red and step phases need STEP=S<N>");
	const commands = plan.value.steps.filter((item) => item.withdrawn !== true).flatMap((item) => item.verify.filter((entry) => entry.phase === "final"));
	return { name: "final.json", scope: { kind: "final" }, phase: "final", expected: "pass", commands, inputs };
}
function execute(ctx, argv, log) {
	return new Promise((resolve) => {
		let settled = false;
		let interrupted = null;
		let child;
		const onInt = () => stop("SIGINT");
		const onTerm = () => stop("SIGTERM");
		const settle = (result) => {
			if (settled) return;
			settled = true;
			process.removeListener("SIGINT", onInt);
			process.removeListener("SIGTERM", onTerm);
			resolve({ argv, ...result });
		};
		const stop = (signal) => {
			interrupted = signal;
			child?.kill(signal);
		};
		// An unavailable command is its own outcome: never a pass, never an ordinary failure.
		const unavailable = (error) =>
			settle({ exit_code: null, signal: null, status: ["ENOENT", "EACCES"].includes(error.code) ? "unavailable" : "failed", error: error.code || error.message });
		process.on("SIGINT", onInt);
		process.on("SIGTERM", onTerm);
		try {
			child = spawn(argv[0], argv.slice(1), { cwd: ctx.root, shell: false, stdio: ["ignore", "pipe", "pipe"] });
		} catch (error) {
			unavailable(error);
			return;
		}
		for (const stream of [child.stdout, child.stderr])
			stream.on("data", (chunk) => {
				log.push(chunk);
				process.stderr.write(chunk);
			});
		child.once("error", unavailable);
		child.once("close", (exit, signal) => {
			const killed = interrupted || signal;
			settle({ exit_code: exit, signal: killed || null, status: !killed && exit === 0 ? "passed" : "failed", interrupted: Boolean(interrupted) });
		});
	});
}
async function record(options = {}) {
	const ctx = options.ctx || context(options.root);
	ctx.config = ctx.config || readConfig(ctx);
	const delivery = options.delivery;
	if (!delivery || !DELIVERY.test(delivery)) throw new Invocation("contracts: record needs DELIVERY=d-YYYYMMDD-xxxxxx");
	if (options.step && !/^S\d+$/.test(options.step)) throw new Invocation("contracts: STEP must look like S1");
	if (options.phase && !PHASES.includes(options.phase)) throw new Invocation("contracts: PHASE must be red, step or final");
	const plan = scopeFor(ctx, delivery, options.step, options.phase);
	const started = new Date().toISOString();
	const before = snapshot(ctx, ctx.config);
	if (!before) throw new Invocation("contracts: the code snapshot needs a Git repository");
	const commands = [];
	const log = [];
	let status;
	if (options.notRun) {
		status = "not_run";
	} else if (!plan.commands.length) {
		throw new Invocation(`contracts: no ${plan.phase} verification command is declared for this scope`);
	} else {
		for (const argv of plan.commands.map((entry) => entry.argv)) {
			log.push(Buffer.from(`$ ${argv.join(" ")}\n`));
			const result = await execute(ctx, argv, log);
			commands.push(result);
			if (result.status === "unavailable" || result.interrupted) break;
			if (plan.expected === "pass" && result.status !== "passed") break;
		}
		if (commands.some((item) => item.status === "unavailable")) status = "unavailable";
		else if (commands.some((item) => item.interrupted)) status = "failed";
		else if (plan.expected === "fail")
			// Red evidence: every command must fail; any pass is recorded as a pass for the validator to reject.
			status = commands.length && commands.every((item) => item.status === "failed") ? "failed" : "passed";
		else status = commands.length === plan.commands.length && commands.every((item) => item.status === "passed") ? "passed" : "failed";
	}
	const after = snapshot(ctx, ctx.config);
	const evidence = {
		schema: SCHEMAS.evidence,
		version: VERSION,
		delivery_id: delivery,
		scope: plan.scope,
		phase: plan.phase,
		expected: plan.expected,
		inputs: plan.inputs,
		code: { snapshot_sha256: after.snapshot_sha256, head: after.head, files: after.files, ...(after.snapshot_sha256 !== before.snapshot_sha256 ? { changed_during_run: true } : {}) },
		commands: commands.map(({ interrupted, ...item }) => item),
		status,
		...(options.reason ? { reason: String(options.reason).slice(0, 500) } : {}),
		started_at: started,
		finished_at: new Date().toISOString(),
	};
	if (log.length) {
		const name = plan.name.replace(/\.json$/, ".log");
		const logFile = path.join(ctx.workspace, "runs", "evidence", delivery, name);
		const bytes = Buffer.concat(log);
		ctx.safe.mkdir(path.dirname(logFile));
		ctx.safe.write(logFile, bytes);
		evidence.output_ref = ctx.rel(logFile);
		evidence.output_sha256 = digest(bytes);
	}
	const file = path.join(ctx.workspace, "evidence", delivery, plan.name);
	writeJson(ctx, file, evidence);
	const ok = plan.expected === "fail" ? status === "failed" : status === "passed";
	return { file, evidence, ok, interrupted: commands.some((item) => item.interrupted) };
}
// Review evidence: the exact gated output of this run, bound to the code it reviewed.
function recordReview(options) {
	const ctx = options.ctx || context(options.root);
	ctx.config = ctx.config || readConfig(ctx);
	if (!DELIVERY.test(options.delivery || "")) throw new Invocation("contracts: DELIVERY must be d-YYYYMMDD-xxxxxx");
	const index = scan(ctx);
	const mine = index.sidecars.filter((item) => item.value?.delivery_id === options.delivery);
	if (!mine.length) throw new Invocation(`contracts: no sidecar carries ${options.delivery}`);
	const inputs = {};
	for (const sidecar of mine) {
		const read = readInside(ctx, sidecar.markdownFile);
		if (read.bytes) inputs[`${sidecar.kind}_sha256`] = contentDigest(read.bytes, sidecar.kind);
	}
	const code = snapshot(ctx, ctx.config);
	if (!code) throw new Invocation("contracts: the code snapshot needs a Git repository");
	const evidence = {
		schema: SCHEMAS.evidence,
		version: VERSION,
		delivery_id: options.delivery,
		scope: { kind: "review" },
		phase: "final",
		expected: "pass",
		inputs,
		code: { snapshot_sha256: code.snapshot_sha256, head: code.head, files: code.files },
		commands: [{ argv: ["make", "review"], exit_code: options.approved ? 0 : 1, signal: null, status: options.approved ? "passed" : "failed" }],
		status: options.approved ? "passed" : "failed",
		review: {
			tool: options.tool,
			verdict: options.verdict || null,
			output_sha256: options.outputHash,
			// A summary of what the gate read, never the raw review output.
			findings: Array.isArray(options.findings)
				? options.findings.map((finding) => ({
						severity: finding.severity,
						file: String(finding.file).slice(0, 300),
						line: finding.line,
						issue: String(finding.issue).slice(0, 300),
					}))
				: null,
			...(options.reason ? { reason: options.reason } : {}),
			// Provenance: who reviewed. Only the headless review boundary (make review) records an
			// independent review; anything else is labelled as what it is, never as independent.
			independence: ["independent", "self"].includes(options.independence) ? options.independence : "unspecified",
			boundary: typeof options.boundary === "string" && options.boundary ? options.boundary.slice(0, 100) : "unspecified",
		},
		started_at: options.started || new Date().toISOString(),
		finished_at: new Date().toISOString(),
	};
	const file = path.join(ctx.workspace, "evidence", options.delivery, "review.json");
	writeJson(ctx, file, evidence);
	return { file, evidence };
}
function attest(options = {}) {
	const ctx = options.ctx || context(options.root);
	ctx.config = ctx.config || readConfig(ctx);
	if (!DELIVERY.test(options.delivery || "")) throw new Invocation("contracts: attest needs --delivery d-YYYYMMDD-xxxxxx");
	if (!/^(?:AC|QC)\d+$/.test(options.criterion || "")) throw new Invocation("contracts: attest needs --criterion AC<n> or QC<n>");
	for (const key of ["actor", "rationale", "source"])
		if (!options[key] || !String(options[key]).trim()) throw new Invocation(`contracts: attest needs --${key}`);
	const mine = scan(ctx).sidecars.filter((item) => item.value?.delivery_id === options.delivery);
	const owner = mine.find((item) => item.kind === (options.criterion.startsWith("QC") ? "quick" : "spec"));
	const declared = owner?.value?.[owner.kind === "quick" ? "checks" : "criteria"] || [];
	if (!declared.includes(options.criterion)) throw new Invocation(`contracts: ${options.criterion} is not declared by delivery ${options.delivery}`);
	const read = readInside(ctx, owner.markdownFile);
	if (!read.bytes) throw new Invocation(`contracts: ${ctx.rel(owner.markdownFile)}: ${read.message}`);
	const value = {
		schema: SCHEMAS.attestation,
		version: VERSION,
		delivery_id: options.delivery,
		criterion: options.criterion,
		actor: String(options.actor).slice(0, 200),
		attested_at: new Date().toISOString(),
		rationale: String(options.rationale).slice(0, 1000),
		source: String(options.source).slice(0, 500),
		inputs: { [`${owner.kind}_sha256`]: contentDigest(read.bytes, owner.kind) },
	};
	const file = path.join(ctx.workspace, "evidence", options.delivery, `${options.criterion}-attest.json`);
	writeJson(ctx, file, value);
	return { file, value };
}
function enable(options = {}) {
	const ctx = options.ctx || context(options.root);
	const file = path.join(ctx.workspace, "contracts", "config.json");
	const created = !fs.existsSync(file) && !isLink(file);
	if (created)
		writeJson(ctx, file, { schema: SCHEMAS.config, version: VERSION, code_scope: { include: ["**"], exclude: [] } });
	else readConfig(ctx);
	// Record the capability beside the layout version; manifest.js preserves it.
	const manifest = path.join(ctx.workspace, ".sdlc.json");
	let capability = false;
	if (fs.existsSync(manifest)) {
		const read = readInside(ctx, manifest);
		if (!read.bytes) throw new Invocation(`contracts: ${ctx.rel(manifest)}: ${read.message}`);
		const value = JSON.parse(read.bytes.toString("utf8"));
		if (value.capabilities?.contracts?.version !== VERSION) {
			value.capabilities = { ...(value.capabilities || {}), contracts: { version: VERSION } };
			ctx.safe.write(manifest, `${JSON.stringify(value, null, 2)}\n`);
		}
		capability = true;
	}
	return { file, created, capability };
}
// Draft sidecars for artifacts that have none. Markdown is never modified, IDs are only
// extracted (never invented), and no evidence is created: drafts stay legacy_unverified.
function migrate(options = {}) {
	const ctx = options.ctx || context(options.root);
	ctx.config = ctx.config || readConfig(ctx);
	const index = scan(ctx);
	const has = (md) => index.sidecars.some((item) => item.file === md.replace(/\.md$/, "") + SIDECAR);
	const actions = [];
	const specs = new Map();
	for (const sidecar of index.sidecars)
		if (sidecar.kind === "spec" && sidecar.value) specs.set(sidecar.markdownFile, sidecar.value);
	for (const md of index.markdown.filter((item) => item.kind === "spec" && !has(item.file))) {
		const read = readInside(ctx, md.file);
		if (!read.bytes) continue;
		const { ids, duplicates } = parseCriteria(read.bytes.toString("utf8"));
		if (!ids.length || duplicates.length) {
			actions.push({ path: ctx.rel(md.file), action: "skipped", reason: !ids.length ? "no AC IDs to extract; number the criteria first" : `duplicate IDs ${duplicates.join(", ")}` });
			continue;
		}
		const value = { schema: SCHEMAS.spec, version: VERSION, delivery_id: newDelivery(), sha256: contentDigest(read.bytes, "spec"), criteria: ids, migrated: true, review_required: true };
		specs.set(md.file, value);
		actions.push({ path: ctx.rel(md.file.replace(/\.md$/, "") + SIDECAR), action: options.write ? "written" : "would write", criteria: ids, value });
	}
	for (const md of index.markdown.filter((item) => item.kind === "plan" && !has(item.file))) {
		const read = readInside(ctx, md.file);
		if (!read.bytes) continue;
		const text = read.bytes.toString("utf8");
		const link = text.match(/\*\*Spec:\*\*\s*`?(ai-factory\/specs\/[^`\s)]+\.md)/);
		const number = path.basename(md.file).match(/^(\d{4})-/)?.[1];
		let specFile = link && path.resolve(ctx.root, link[1]);
		if (!specFile || !specs.has(specFile))
			specFile = [...specs.keys()].find((file) => number && path.basename(file).startsWith(`${number}-`));
		if (!specFile || !specs.has(specFile)) {
			actions.push({ path: ctx.rel(md.file), action: "skipped", reason: "its spec has no sidecar to link to" });
			continue;
		}
		const spec = specs.get(specFile);
		const specBytes = readInside(ctx, specFile).bytes;
		const steps = parsePlanSteps(text).map((step) => ({ id: step.id, criteria: [...new Set(step.text.match(/\bAC\d+\b/g) || [])], verify: draftVerify(step.text).verify, ...(step.withdrawn ? { withdrawn: true } : {}) }));
		const value = { schema: SCHEMAS.plan, version: VERSION, delivery_id: spec.delivery_id, sha256: contentDigest(read.bytes, "plan"), spec_sha256: specBytes ? contentDigest(specBytes, "spec") : spec.sha256, steps, migrated: true, review_required: true };
		actions.push({ path: ctx.rel(md.file.replace(/\.md$/, "") + SIDECAR), action: options.write ? "written" : "would write", steps: steps.map((step) => `${step.id}[${step.criteria.join(",")}]${step.verify.length ? "" : " (no verify command drafted)"}`), value });
	}
	if (options.write)
		for (const action of actions.filter((item) => item.action === "written"))
			writeJson(ctx, path.join(ctx.root, action.path), action.value);
	return actions.map(({ value, ...item }) => item);
}

// --- command line --------------------------------------------------------------------------
const FLAGS = {
	"--json": "json",
	"--all": "all",
	"--write": "write",
	"--not-run": "notRun",
	"--require": "require",
	"--delivery": "delivery",
	"--step": "step",
	"--phase": "phase",
	"--reason": "reason",
	"--spec": "spec",
	"--tracker": "tracker",
	"--criterion": "criterion",
	"--actor": "actor",
	"--rationale": "rationale",
	"--source": "source",
};
const BOOLEAN = new Set(["json", "all", "write", "notRun"]);
function parseArgs(args, env) {
	const options = { positional: [] };
	for (let index = 0; index < args.length; index++) {
		const arg = args[index];
		if (!arg.startsWith("--")) {
			options.positional.push(arg);
			continue;
		}
		const key = FLAGS[arg];
		if (!key || Object.hasOwn(options, key)) throw new Invocation(`contracts: invalid argument ${arg}`);
		if (BOOLEAN.has(key)) options[key] = true;
		else {
			const value = args[++index];
			if (!value) throw new Invocation(`contracts: ${arg} needs a value`);
			options[key] = value;
		}
	}
	// Make passes selections as literal environment data (see ai.mk).
	options.json = options.json || env.JSON === "1";
	for (const [key, name] of [["delivery", "DELIVERY"], ["step", "STEP"], ["phase", "PHASE"], ["require", "REQUIRE"]])
		if (options[key] === undefined && env[name]) options[key] = env[name];
	options.require = options.require ? String(options.require).split(",").filter(Boolean) : [];
	for (const item of options.require)
		if (!["final", "review"].includes(item)) throw new Invocation("contracts: --require takes final and/or review");
	return options;
}
async function main(argv = process.argv.slice(2), env = process.env, root) {
	const [command, ...rest] = argv;
	const out = (text) => process.stdout.write(`${text}\n`);
	const err = (text) => process.stderr.write(`${text}\n`);
	try {
		const options = parseArgs(rest, env);
		const ctx = context(root);
		if (command === "validate") {
			ctx.config = readConfig(ctx);
			if (options.positional.length > 1) throw new Invocation("contracts: validate takes one delivery ID or path");
			if (!options.positional.length && options.delivery && !options.all && !DELIVERY.test(options.delivery))
				throw new Invocation("contracts: DELIVERY must be d-YYYYMMDD-xxxxxx");
			const target = options.positional[0] || (options.all ? undefined : options.delivery);
			const doc = validate({ ctx, target, require: options.require });
			if (options.json) out(JSON.stringify(doc, null, 2));
			else (doc.status === "valid" ? out : err)(summary(doc));
			return doc.status === "valid" ? 0 : 1;
		}
		if (command === "snapshot") {
			ctx.config = readConfig(ctx);
			const snap = snapshot(ctx, ctx.config);
			if (!snap) throw new Invocation("contracts: the code snapshot needs a Git repository");
			const doc = { schema: SCHEMAS.snapshot, version: VERSION, ...snap };
			out(options.json ? JSON.stringify(doc, null, 2) : `snapshot ${snap.snapshot_sha256} — ${snap.files} files${snap.head ? ` at ${snap.head.slice(0, 12)}` : ""}`);
			return 0;
		}
		if (command === "init") {
			const [kind, target] = options.positional;
			if (!["spec", "plan", "quick"].includes(kind)) throw new Invocation("contracts: init takes spec, plan or quick");
			const result = init(kind, target, { ctx, spec: options.spec, tracker: options.tracker });
			for (const note of result.notes) err(`note: ${note}`);
			// init answers for the artifact it wrote; dependents going stale is reported, not failed.
			const mine = result.doc.artifacts.find((item) => item.path === ctx.rel(result.sidecar));
			const status = mine ? mine.state : "invalid";
			if (options.json) out(JSON.stringify({ sidecar: ctx.rel(result.sidecar), delivery_id: result.value.delivery_id, artifact_state: status, ...result.doc }, null, 2));
			else {
				out(`contracts: wrote ${ctx.rel(result.sidecar)} (${result.value.delivery_id}) — ${status}`);
				(status === "valid" ? out : err)(summary(result.doc));
			}
			return status === "valid" ? 0 : 1;
		}
		if (command === "record") {
			const result = await record({ ctx, delivery: options.delivery, step: options.step, phase: options.phase, notRun: options.notRun, reason: options.reason });
			const line = `contracts: recorded ${result.evidence.status} (${result.evidence.phase}, expected ${result.evidence.expected}) → ${ctx.rel(result.file)}`;
			if (options.json) out(JSON.stringify(result.evidence, null, 2));
			else (result.ok ? out : err)(line);
			return result.interrupted ? 130 : result.ok ? 0 : 1;
		}
		if (command === "attest") {
			const result = attest({ ctx, delivery: options.delivery, criterion: options.criterion, actor: options.actor, rationale: options.rationale, source: options.source });
			out(`contracts: attested ${result.value.criterion} by ${result.value.actor} → ${ctx.rel(result.file)} (counts toward completion only where policy allows)`);
			return 0;
		}
		if (command === "enable") {
			const result = enable({ ctx });
			out(`contracts: ${result.created ? "enabled" : "already enabled"} — ${ctx.rel(result.file)}${result.capability ? "; capability recorded in ai-factory/.sdlc.json" : ""}`);
			return 0;
		}
		if (command === "migrate") {
			const actions = migrate({ ctx, write: options.write });
			if (options.json) out(JSON.stringify({ write: Boolean(options.write), actions }, null, 2));
			else {
				if (!actions.length) out("contracts: nothing to migrate — every artifact already has a sidecar");
				for (const action of actions) {
					out(`${action.action}: ${action.path}${action.reason ? ` — ${action.reason}` : ""}`);
					if (action.criteria) out(`  extracted criteria: ${action.criteria.join(", ")}`);
					if (action.steps) out(`  extracted steps: ${action.steps.join("; ")}`);
				}
				if (actions.some((item) => item.action !== "skipped"))
					out("Drafts are legacy_unverified until reviewed: check each ID and command, then rerun contracts.js init on the Markdown.");
				if (!options.write && actions.length) out("Dry run; pass --write to create the drafts.");
			}
			return 0;
		}
		throw new Invocation("usage: contracts.js <validate|snapshot|init|record|attest|enable|migrate> …");
	} catch (error) {
		if (error instanceof Invocation) {
			err(error.message);
			return 2;
		}
		err(`contracts: ${error.message}`);
		return 2;
	}
}
if (require.main === module)
	main().then((code) => {
		process.exitCode = code;
	});
module.exports = {
	SCHEMAS,
	REQUIRED,
	COMPLETION,
	DELIVERY,
	Invocation,
	guardPath,
	readInside,
	readConfig,
	assurance,
	scan,
	readEvidence,
	stateOf,
	worst,
	attest,
	parseSidecar,
	parseCriteria,
	parseChecks,
	parsePlanSteps,
	draftVerify,
	contentDigest,
	snapshot,
	validate,
	summary,
	init,
	record,
	recordReview,
	enable,
	migrate,
	main,
	context,
};
