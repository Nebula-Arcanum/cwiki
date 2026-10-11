#!/usr/bin/env node
// Assurance presets: one opt-in selection (ai-factory/assurance.json) resolved as minimums
// over the controls a repository already has. Absent selection means legacy behavior.
//   assurance.js show [--json]                       read-only effective settings
//   assurance.js set light|standard|strict [--json]  preview the changes; writes nothing
//   assurance.js set <preset> --apply [--json]       explicitly write them
//   assurance.js complete <delivery> [--json]        may completion be claimed? Evaluated from
//                                                    current evidence; strict also saves the
//                                                    freshly generated report. Exit 0 only if so.
// resolve() is pure: callers pass validated existing settings (contracts.readConfig output and
// the environment), so this module loads no contract or report module at require time and they
// may import it. Only the CLI composes those inputs, and it never weakens project rules.
const fs = require("node:fs");
const path = require("node:path");
const { boundary } = require("./safe-files.js");
// Preserve the invoked workspace when its entry point is a source-repo symlink (see runner.js).
const ENTRY_DIRECTORY =
	require.main === module ? path.dirname(process.argv[1]) : __dirname;
const ROOT = fs.realpathSync(path.resolve(ENTRY_DIRECTORY, "../.."));

const SCHEMA = "t4-assurance";
const POLICY_SCHEMA = "t4-assurance-policy";
const SETUP_SCHEMA = "t4-assurance-setup";
const VERSION = 1;
const SELECTION = "ai-factory/assurance.json";
const CONFIG = "ai-factory/contracts/config.json";
const NOTE =
	"Project rules in ai-factory/ still apply; a preset only adds requirements and never weakens them or risk routing.";
// Requirements each preset sets as a minimum. Order of keys is the display order.
const PRESETS = {
	light: {
		contracts: false,
		final_verification: false,
		review_required: false,
		review_required_quick: false,
		review_independence: "self",
		gate: "advisory",
		completion: "summary",
		blocking_severities: [],
	},
	standard: {
		contracts: true,
		final_verification: true,
		review_required: true,
		review_required_quick: true,
		review_independence: "independent",
		gate: "advisory",
		completion: "recorded_checks",
		blocking_severities: ["blocker"],
	},
	strict: {
		contracts: true,
		final_verification: true,
		review_required: true,
		review_required_quick: true,
		review_independence: "independent",
		gate: "enforced",
		completion: "fresh_ready_report",
		blocking_severities: ["blocker"],
	},
};
const NAMES = Object.keys(PRESETS);
const RANKS = {
	review_independence: ["unspecified", "self", "independent"],
	gate: ["advisory", "enforced"],
	completion: ["summary", "recorded_checks", "fresh_ready_report"],
};
const SEVERITIES = ["blocker", "major", "minor"];
const ORDER = [
	"contracts",
	"final_verification",
	"review_required",
	"review_required_quick",
	"review_independence",
	"gate",
	"completion",
	"blocking_severities",
	"allow_attestation",
	"code_scope",
];

class AssuranceError extends Error {
	constructor(message, code = 2) {
		super(message);
		this.exitCode = code;
	}
}
function unknownPreset(name) {
	return new AssuranceError(
		`assurance: unknown preset '${name}' (expected ${NAMES.join(", ")})`,
	);
}

// What the repository already enforces, before any preset. Values from a missing contracts
// configuration are labelled `default`: they are not in force and never outrank a preset.
function existing(contracts, env) {
	const adopted = Boolean(contracts && contracts.adopted);
	const policy = (contracts && contracts.completion) || {};
	const from = adopted ? CONFIG : "default";
	const enforce = env.GATE_ENFORCE;
	const gateSource =
		enforce === "0" || enforce === "1" ? "GATE_ENFORCE" : "default";
	return {
		contracts: { value: adopted, source: from },
		final_verification: { value: adopted, source: from },
		review_required: { value: adopted && policy.require_review === true, source: from },
		review_required_quick: { value: adopted && policy.require_review_quick === true, source: from },
		review_independence: { value: "unspecified", source: "default" },
		gate: { value: enforce === "1" ? "enforced" : "advisory", source: gateSource },
		completion: { value: adopted ? "recorded_checks" : "summary", source: from },
		blocking_severities: {
			value: adopted && Array.isArray(policy.blocking_severities) ? policy.blocking_severities : ["blocker"],
			source: from,
		},
		allow_attestation: { value: adopted && policy.allow_attestation === true, source: from },
		code_scope: {
			value: (contracts && contracts.code_scope) || { include: ["**"], exclude: [] },
			source: from,
		},
	};
}
const sorted = (list) =>
	SEVERITIES.filter((item) => list.includes(item));
// Stronger of the preset minimum and the existing control; ties are attributed to the preset.
function combine(key, minimum, current, name) {
	const preset = { value: minimum, source: `preset:${name}`, retained: false };
	if (current.source === "default") return preset;
	const above = { ...current, retained: true };
	if (key === "blocking_severities") {
		const union = sorted([...minimum, ...current.value]);
		return union.length > minimum.length
			? { value: union, source: current.source, retained: true }
			: preset;
	}
	if (RANKS[key])
		return RANKS[key].indexOf(current.value) > RANKS[key].indexOf(minimum) ? above : preset;
	return current.value === true && minimum === false ? above : preset;
}

// The review gate's mode, shared by resolve(), gate.js and runner.js so every caller applies the
// same rule. Without a preset GATE_ENFORCE keeps its existing meaning; strict enforces the gate,
// a stricter GATE_ENFORCE=1 is always allowed, and GATE_ENFORCE=0 under strict is a conflict.
function enforcement({ preset = null, env = {} } = {}) {
	if (preset !== null && !Object.hasOwn(PRESETS, preset)) throw unknownPreset(preset);
	const value = env.GATE_ENFORCE;
	const conflicts = [];
	if (value !== undefined && value !== "" && value !== "0" && value !== "1")
		conflicts.push({
			code: "E_GATE_INVALID",
			message: `GATE_ENFORCE=${JSON.stringify(value)} is not 0, 1, or unset`,
			hint: "Unset GATE_ENFORCE or set it to 0 or 1.",
		});
	const presetEnforced = preset !== null && PRESETS[preset].gate === "enforced";
	if (presetEnforced && value === "0")
		conflicts.push({
			code: "E_GATE_CONFLICT",
			message: `GATE_ENFORCE=0 conflicts with preset ${preset}, which enforces the review gate`,
			hint: `Unset GATE_ENFORCE (or set it to 1), or select a preset with an advisory gate: assurance.js set standard --apply.`,
		});
	const explicit = value === "0" || value === "1";
	// Attributed as resolve() does: a tie with the preset's own requirement belongs to the preset.
	const source =
		preset === null
			? explicit ? "GATE_ENFORCE" : "default"
			: value === "1" && !presetEnforced ? "GATE_ENFORCE" : `preset:${preset}`;
	return { preset, mode: presetEnforced || value === "1" ? "enforced" : "advisory", source, conflicts };
}

// Pure resolution. `preset` is a preset name or null (no selection); `contracts` is the
// validated contracts.readConfig() result; `env` carries GATE_ENFORCE.
function resolve({ preset = null, contracts, env = {} } = {}) {
	if (preset !== null && !Object.hasOwn(PRESETS, preset)) throw unknownPreset(preset);
	const base = existing(contracts, env);
	const settings = {};
	const gate = enforcement({ preset, env });
	const conflicts = gate.conflicts.filter((item) => item.code === "E_GATE_INVALID");
	const unmet = [];
	if (preset === null) {
		for (const key of ORDER) settings[key] = { ...base[key], retained: false };
	} else {
		const minimum = PRESETS[preset];
		for (const key of ORDER) {
			if (!Object.hasOwn(minimum, key)) {
				// Preserved as configured: presets neither require nor change these.
				settings[key] = { ...base[key], retained: false };
				continue;
			}
			settings[key] = combine(key, minimum[key], base[key], preset);
		}
		// Contracts are reported as they actually are; a requirement is never shown as met.
		if (minimum.contracts && !base.contracts.value) {
			settings.contracts = { value: false, source: base.contracts.source, retained: false };
			unmet.push({
				code: "U_CONTRACTS_NOT_ENABLED",
				message: `preset ${preset} requires artifact contracts, but ${CONFIG} does not exist`,
				hint: `Run assurance.js set ${preset} --apply (or contracts.js enable) to enable them.`,
			});
		}
		conflicts.push(...gate.conflicts.filter((item) => item.code === "E_GATE_CONFLICT"));
	}
	const status = conflicts.length
		? "conflict"
		: unmet.length
			? "incomplete"
			: preset === null
				? "legacy"
				: "active";
	return {
		schema: POLICY_SCHEMA,
		version: VERSION,
		mode: preset === null ? "legacy" : "preset",
		preset,
		status,
		settings,
		conflicts,
		unmet,
		note: NOTE,
	};
}

// --- reading the selection and existing configuration (read-only) -----------------------------
function context(root = ROOT) {
	const workspace = path.join(root, "ai-factory");
	return { root, workspace, safe: boundary(workspace), file: path.join(workspace, "assurance.json") };
}
function readSelection(root = ROOT) {
	const ctx = context(root);
	try {
		ctx.safe.assertPath(ctx.file, { allowMissing: false });
	} catch (error) {
		if (error.code === "ENOENT") return { selected: false, preset: null };
		const reason = String(error.message).replace(/: \/.*$/, "");
		throw new AssuranceError(`assurance: ${SELECTION}: ${reason}`);
	}
	let value;
	try {
		value = JSON.parse(fs.readFileSync(ctx.file, "utf8"));
	} catch (error) {
		throw new AssuranceError(
			`assurance: ${SELECTION}: ${error instanceof SyntaxError ? "not valid JSON" : error.code || error.message}`,
		);
	}
	const malformed = (message) =>
		new AssuranceError(`assurance: ${SELECTION}: ${message}; fix it or rerun assurance.js set <preset> after removing it`);
	if (!value || typeof value !== "object" || Array.isArray(value)) throw malformed("must be an object");
	if (value.schema !== SCHEMA) throw malformed(`schema must be ${SCHEMA}`);
	if (value.version !== VERSION) throw malformed(`version must be ${VERSION}`);
	const extra = Object.keys(value).filter((key) => !["schema", "version", "preset"].includes(key));
	if (extra.length) throw malformed(`holds only schema, version and preset (unexpected: ${extra.join(", ")})`);
	if (typeof value.preset !== "string" || !Object.hasOwn(PRESETS, value.preset))
		throw malformed(`unknown preset ${JSON.stringify(value.preset)} (expected ${NAMES.join(", ")})`);
	return { selected: true, preset: value.preset };
}
// Loaded lazily so contracts.js can import this module without a cycle.
function readContracts(root) {
	const contracts = require("./contracts.js");
	const ctx = contracts.context(root);
	try {
		return { contracts, ctx, config: contracts.readConfig(ctx) };
	} catch (error) {
		throw new AssuranceError(
			String(error.message).startsWith("contracts:") ? error.message : `contracts: ${error.message}`,
		);
	}
}
function show({ root = ROOT, env = process.env } = {}) {
	const selection = readSelection(root);
	const { config } = readContracts(root);
	return {
		...resolve({ preset: selection.preset, contracts: config, env }),
		selection: { file: SELECTION, selected: selection.selected },
	};
}

// --- explicit setup ---------------------------------------------------------------------------
// Validates everything before the first write. Contracts are enabled before the selection is
// written, so a failure never leaves a preset that claims requirements it cannot meet.
function setup({ root = ROOT, preset, apply = false, env = process.env } = {}) {
	if (typeof preset !== "string" || !Object.hasOwn(PRESETS, preset)) throw unknownPreset(preset);
	const ctx = context(root);
	if (!fs.existsSync(ctx.workspace))
		throw new AssuranceError("assurance: no ai-factory/ here; adopt the layout first");
	const current = readSelection(root);
	const loaded = readContracts(root);
	const requirement = PRESETS[preset];
	// Applied in this order: enable first, so a failure leaves no selection behind.
	const changes = [];
	if (requirement.contracts && !loaded.config.adopted)
		changes.push({ action: "enable_contracts", path: CONFIG });
	if (current.preset !== preset)
		changes.push({ action: "select", path: SELECTION, from: current.preset, to: preset });
	const retained = [];
	if (!requirement.contracts && loaded.config.adopted)
		retained.push(`contracts configuration (${CONFIG}) and recorded evidence are kept; light never disables them`);
	const predicted = requirement.contracts && !loaded.config.adopted
		? { ...loaded.config, adopted: true }
		: loaded.config;
	const policy = resolve({ preset, contracts: predicted, env });
	const result = {
		schema: SETUP_SCHEMA,
		version: VERSION,
		preset,
		from: current.preset,
		apply,
		changes,
		retained,
		blocked: policy.conflicts.length > 0,
		applied: false,
		partial: [],
		failed: null,
		policy,
	};
	if (!apply || result.blocked) return result;
	for (const change of changes) {
		try {
			if (change.action === "enable_contracts") loaded.contracts.enable({ ctx: loaded.ctx });
			else ctx.safe.write(ctx.file, `${JSON.stringify({ schema: SCHEMA, version: VERSION, preset })}\n`);
			result.partial.push(change.action);
		} catch (error) {
			// Diagnostics name workspace-relative paths, never the absolute checkout.
			result.failed = { step: change.action, message: String(error.message).split(`${root}${path.sep}`).join("") };
			return result;
		}
	}
	result.applied = true;
	result.policy = { ...resolve({ preset, contracts: readContracts(root).config, env }) };
	return result;
}

// --- command line -----------------------------------------------------------------------------
function text(value) {
	if (Array.isArray(value)) return value.length ? value.join(",") : "none";
	if (value && typeof value === "object")
		return `include=${text(value.include)} exclude=${text(value.exclude)}`;
	return String(value);
}
function table(policy) {
	return ORDER.map((key) => {
		const item = policy.settings[key];
		const value = text(item.value);
		return `  ${key.padEnd(22)} ${value.padEnd(18)} (${item.source}${item.retained ? ", retained above preset" : ""})`;
	});
}
function headline(policy) {
	if (policy.preset === null)
		return `assurance: no preset selected — legacy behavior; existing configuration unchanged${policy.status === "conflict" ? " (conflict)" : ""}`;
	return `assurance: preset ${policy.preset} — ${policy.status}`;
}
function problems(policy) {
	return [
		...policy.conflicts.map((item) => `conflict ${item.code}: ${item.message}\n  ${item.hint}`),
		...policy.unmet.map((item) => `unmet ${item.code}: ${item.message}\n  ${item.hint}`),
	];
}
// One line for runners and gates: the preset, its status and the requirements it applies.
function describe(policy) {
	const shown = ORDER.filter((key) => !["allow_attestation", "code_scope"].includes(key))
		.map((key) => `${key}=${text(policy.settings[key].value)}`)
		.join(" ");
	return `assurance: preset ${policy.preset} — ${policy.status}: ${shown}`;
}
function parseArgs(argv) {
	const [command, ...rest] = argv;
	if (!["show", "set", "complete"].includes(command))
		throw new AssuranceError("usage: assurance.js show [--json] | set light|standard|strict [--apply] [--json] | complete <delivery> [--json]");
	const options = { command, json: false, apply: false, positional: [] };
	for (const arg of rest) {
		if (arg === "--json" && !options.json) options.json = true;
		else if (arg === "--apply" && command === "set" && !options.apply) options.apply = true;
		else if (arg.startsWith("--")) throw new AssuranceError(`assurance: invalid argument ${arg}`);
		else options.positional.push(arg);
	}
	if (command === "show" && options.positional.length)
		throw new AssuranceError("assurance: show takes no arguments");
	if (command === "set" && options.positional.length !== 1)
		throw new AssuranceError(`assurance: set takes one preset (${NAMES.join(", ")})`);
	if (command === "complete" && options.positional.length !== 1)
		throw new AssuranceError("assurance: complete takes one delivery ID (d-YYYYMMDD-xxxxxx)");
	return options;
}
// Completion is judged by delivery-report.js from current evidence; loaded lazily (it imports
// contracts.js, which imports this module).
function complete({ root = ROOT, delivery, env = process.env } = {}) {
	const reports = require("./delivery-report.js");
	try {
		return reports.completion({ root, delivery, env });
	} catch (error) {
		if (error instanceof AssuranceError) throw error;
		throw new AssuranceError(String(error.message).replace(/^(?!assurance:|contracts:|delivery-report:)/, "assurance: "));
	}
}
function main(argv = process.argv.slice(2), env = process.env, root = ROOT) {
	const out = (line) => process.stdout.write(`${line}\n`);
	const err = (line) => process.stderr.write(`${line}\n`);
	try {
		const options = parseArgs(argv);
		if (options.command === "show") {
			const policy = show({ root, env });
			const ok = policy.status === "active" || policy.status === "legacy";
			if (options.json) out(JSON.stringify(policy, null, 2));
			else {
				out(headline(policy));
				for (const line of table(policy)) out(line);
				for (const line of problems(policy)) err(line);
				out(policy.note);
			}
			return ok ? 0 : 1;
		}
		if (options.command === "complete") {
			const result = complete({ root, delivery: options.positional[0], env });
			if (options.json) out(JSON.stringify(result, null, 2));
			else {
				out(`assurance: completion ${result.claimable ? "may be claimed" : "may NOT be claimed"} — ${result.delivery} (${result.preset ? `preset ${result.preset}` : "no preset"}, requires ${result.requirement})`);
				out(`  report status: ${result.report_status}${result.written.length ? ` (freshly generated: ${result.written.join(", ")})` : " (evaluated now from current evidence; nothing written)"}`);
				for (const item of result.open) err(`  [${item.state}] ${item.code}: ${item.message}${item.evidence_ref ? ` — ${item.evidence_ref}` : ""}`);
				out(`  ${result.note}`);
			}
			return result.claimable ? 0 : 1;
		}
		const result = setup({ root, preset: options.positional[0], apply: options.apply, env });
		const ok = !result.blocked && (!result.apply || result.applied);
		if (options.json) {
			out(JSON.stringify(result, null, 2));
			if (result.failed) err(`assurance: setup failed at ${result.failed.step}: ${result.failed.message}`);
			return ok ? 0 : 1;
		}
		const verb = result.applied ? "applied" : result.apply ? "not applied" : "preview";
		out(`assurance: ${verb} — select ${result.preset} (from ${result.from || "no preset"})`);
		if (!result.changes.length) out(`  nothing to change; preset ${result.preset} is already selected`);
		for (const change of result.changes)
			out(`  ${change.action.padEnd(17)} ${change.path}${change.action === "select" ? `: ${change.from || "none"} → ${change.to}` : ""}`);
		for (const note of result.retained) out(`retained: ${note}`);
		for (const line of problems(result.policy)) err(line);
		if (result.blocked) err(`assurance: preset ${result.preset} was not selected; resolve the conflict first`);
		if (result.failed) {
			err(`assurance: setup failed at ${result.failed.step}: ${result.failed.message}`);
			err(`assurance: completed before the failure: ${result.partial.length ? result.partial.join(", ") : "nothing"}; preset ${result.preset} is not active`);
			return 1;
		}
		if (result.applied) out(headline(result.policy));
		else out("  effective settings after applying:");
		for (const line of table(result.policy)) out(line);
		if (!result.apply && !result.blocked) out(`Preview only; rerun with --apply to write these changes: assurance.js set ${result.preset} --apply`);
		out(result.policy.note);
		return ok ? 0 : 1;
	} catch (error) {
		err(error instanceof AssuranceError ? error.message : `assurance: ${error.message}`);
		return error instanceof AssuranceError ? error.exitCode : 2;
	}
}
module.exports = {
	PRESETS,
	SCHEMA,
	POLICY_SCHEMA,
	VERSION,
	SELECTION,
	AssuranceError,
	enforcement,
	resolve,
	readSelection,
	show,
	setup,
	complete,
	describe,
	main,
};
// After the exports: complete() reaches this module again through contracts.js while main runs.
if (require.main === module) process.exitCode = main();
