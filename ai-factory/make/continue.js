#!/usr/bin/env node
// Read-only inspection for /t4:continue. Selection never runs verification or writes artifacts.
const path = require("node:path");
const crypto = require("node:crypto");
const contracts = require("./contracts.js");
const reports = require("./delivery-report.js");

const ACTIONS = new Set(["plan", "test", "run", "check", "report"]);
const digest = (value) => crypto.createHash("sha256").update(value).digest("hex");
const reference = (artifact) => ({ path: artifact.path, kind: artifact.kind, state: artifact.state, diagnostics: artifact.diagnostics.map(({ code, message }) => ({ code, message })) });

function inspect(options = {}) {
	const root = path.resolve(options.root || process.cwd());
	const input = options.delivery;
	if (!input) return result("question", "Provide a contract delivery ID (d-YYYYMMDD-xxxxxx).", null, [], null);
	if (typeof input !== "string" || !contracts.DELIVERY.test(input))
		return result("blocked", "Expected a contract delivery ID; paths and other identifiers are unsupported.", null, [], null);
	try {
		const ctx = contracts.context(root);
		ctx.config = contracts.readConfig(ctx);
		if (!ctx.config.adopted) return result("blocked", "Artifact contracts are not enabled in this workspace.", input, [], null);
		const index = contracts.scan(ctx);
		const matches = index.sidecars.filter((item) => item.value?.delivery_id === input);
		const planned = matches.filter((item) => item.kind === "spec" || item.kind === "plan");
		if (!planned.length) return result("blocked", "No contract-backed planned delivery matches this ID.", input, [], null);
		const doc = contracts.validate({ ctx, target: input });
		const collected = reports.collect({ ctx, delivery: input });
		const refs = doc.artifacts.map(reference);
		const drift = refs.some((item) => item.diagnostics.some((d) => d.code === "S_SPEC_CHANGED"));
		if (drift)
			return result("blocked", "The spec changed after downstream work. Reconcile the plan, tests, implementation, verification, and review before continuing.", input, refs, collected.fingerprint);
		// Review outcome and freshness are judged by report evaluation in the review phase, as the report does.
		const blocking = refs.some((item) => BLOCKING.has(item.state) && !reviewJudgedLater(item));
		if (blocking)
			return result("blocked", "Contract validation found invalid, stale, or unverified artifacts; resolve the referenced diagnostics before continuing.", input, refs, collected.fingerprint);
		return select({ ctx, input, refs, collected, answers: knownAnswers(options.answers) });
	} catch (error) {
		return result("blocked", `Could not inspect delivery: ${error.message}`, input, [], null);
	}
}

// Only the closed answer vocabulary is read; anything else is ignored as if unanswered.
function knownAnswers(answers) {
	if (!plain(answers)) return {};
	return Object.fromEntries(Object.entries(answers).filter(([key, value]) => answersProblem({ [key]: value }) === null));
}

const BLOCKING = new Set(["invalid", "stale", "legacy_unverified"]);
function reviewJudgedLater(ref) {
	return /\/review\.json$/.test(ref.path) && ref.diagnostics.every((d) => d.code === "E_EVIDENCE_FAILED" || !d.code.startsWith("E_"));
}

// Earliest unsatisfied prerequisite wins; nothing later is chosen while an earlier phase is open.
//   1. no plan sidecar                      -> plan <spec>
//   2. first unfinished active step S<N>:
//      declares red, no red evidence, not started -> test red S<N>
//      declares red, no red evidence, started     -> question red:S<N>
//      otherwise                                  -> run <plan> S<N> (same step when interrupted)
//   3. every step done, no review recorded:
//      gaps testing not established              -> question gaps_tested
//      answered not tested                       -> test gaps
//      answered tested, review required          -> check
//   4. report evaluation not ready               -> check when only the review is stale, else blocked
//   5. ready, no current completion report       -> report <delivery>
//   6. ready, current completion report          -> complete
// Validation diagnostics (failures, missing or stale evidence, drift) have already stopped earlier;
// answers only choose between tasks and never stand in for verification.
function select({ ctx, input, refs, collected, answers }) {
	const fp = collected.fingerprint;
	const source = (kind) => collected.delivery.sources.find((item) => item.kind === kind)?.markdown || null;
	const spec = source("spec");
	const plan = source("plan");
	if (!collected.plan) {
		if (!spec) return result("blocked", "The delivery has no spec to plan from.", input, refs, fp);
		return result("action", `${spec} has no plan yet; plan it first.`, input, refs, fp, { task: "plan", args: { spec } });
	}
	const records = contracts.readEvidence(ctx, input).records;
	const declared = new Map((collected.plan.steps || []).map((step) => [step.id, step]));
	const active = collected.progress.steps.filter((step) => !step.withdrawn);
	if (!active.length) return result("blocked", `${plan} declares no active steps.`, input, refs, fp);
	const next = active.find((step) => !step.done);
	if (next) {
		const wantsRed = (declared.get(next.id)?.verify || []).some((entry) => entry.phase === "red");
		const stepRecord = records.get(`${next.id}-step.json`)?.value || null;
		const started = next.mark === "~" || Boolean(stepRecord);
		if (wantsRed && !records.has(`${next.id}-red.json`)) {
			if (!started) return result("action", `Step ${next.number} declares a red phase with no recorded red run; write its failing tests first.`, input, refs, fp, { task: "test", args: { spec, plan, mode: "red", step: next.id } });
			if (answers[`red:${next.id}`] !== "proceed")
				return result("question", `Step ${next.number} has started but its declared red run was never recorded. Was red testing done or deliberately skipped? Answer "proceed" to resume the step; red evidence is not created by answering.`, input, refs, fp, { clarify: `red:${next.id}` });
		}
		const state = stepRecord?.commands?.some((command) => command.signal)
			? "interrupted"
			: stepRecord?.status === "failed"
				? "failed its last verification"
				: next.mark === "~"
					? "part-done"
					: "not started";
		const resume = state === "not started" ? "" : " Existing changes and evidence are kept; the step stays unticked until its verification passes.";
		return result("action", `Step ${next.number} is the first unfinished step (${state}).${resume}`, input, refs, fp, { task: "run", args: { plan, step: next.id } });
	}
	const review = collected.review;
	if (!review.evidence) {
		// Final verification and lifecycle telemetry do not show whether gaps testing ran.
		if (answers.gaps_tested === false) return result("action", "Every step is done and gaps testing has not run; run it before review.", input, refs, fp, { task: "test", args: { spec, plan, mode: "gaps" } });
		if (answers.gaps_tested !== true)
			return result("question", "Every step is done and final verification is recorded, but no evidence shows whether gaps testing ran. Has /t4:test gaps been run for this delivery?", input, refs, fp, { clarify: "gaps_tested" });
		if (review.required) return result("action", "Gaps testing is reported done and no review is recorded; review the change.", input, refs, fp, { task: "check", args: { scope: "working-tree", spec, plan } });
	}
	const readiness = reports.evaluate(collected);
	if (readiness.status !== "ready") {
		const open = readiness.reasons.filter((item) => item.state !== "ready");
		const codes = open.map((item) => `${item.code}${item.evidence_ref ? ` (${item.evidence_ref})` : ""}`).join(", ");
		if (open.length && open.every((item) => item.code === "REVIEW_STALE"))
			return result("action", `The recorded review is stale (${codes}); review the current change again.`, input, refs, fp, { task: "check", args: { scope: "working-tree", spec, plan } });
		return result("blocked", `Report evaluation is ${readiness.status}: ${codes}. No continuation task addresses this; resolve it and continue again.`, input, refs, fp);
	}
	const reportRef = `ai-factory/reports/${input}/completion.json`;
	const read = contracts.readInside(ctx, path.join(ctx.root, reportRef));
	let saved = null;
	try {
		saved = read.bytes ? JSON.parse(read.bytes.toString("utf8")) : null;
	} catch {
		saved = null;
	}
	if (saved?.status === "ready" && saved?.snapshot?.fingerprint === fp)
		return result("complete", "Contract and report evaluation show the delivery ready for handoff (not merged, deployed or published), and the completion report matches the current evidence.", input, [...refs, { path: reportRef, kind: "report", state: "valid", diagnostics: [] }], fp);
	return result("action", "Evidence is ready but no completion report matches it; generate the report.", input, refs, fp, { task: "report", args: { delivery: input } });
}

function result(outcome, reason, delivery_id, evidence, fingerprint, extra = {}) {
	return { outcome, reason, delivery_id, evidence, fingerprint, ...extra };
}

// --- bounded result ---------------------------------------------------------------------------
// The selector result is data. Its shape is closed: unknown keys, unbounded text, tasks outside
// the allowlist, and arguments other than each task's exact shape are rejected.
const OUTCOMES = ["action", "question", "blocked", "complete"];
const MAX_REASON = 4000;
const MAX_EVIDENCE = 200;
const SPEC = /^ai-factory\/specs\/[A-Za-z0-9][A-Za-z0-9._-]{0,150}\.md$/;
const PLAN = /^ai-factory\/plans\/[A-Za-z0-9][A-Za-z0-9._-]{0,150}\.md$/;
const STEP = /^S[1-9][0-9]{0,3}$/;
const CLARIFY = /^(?:gaps_tested|red:S[1-9][0-9]{0,3})$/;
const FINGERPRINT = /^[a-f0-9]{64}$/;
const plain = (value) => Boolean(value) && typeof value === "object" && !Array.isArray(value) && Object.getPrototypeOf(value) === Object.prototype;
const exactKeys = (value, keys) => plain(value) && Object.keys(value).length === keys.length && Object.keys(value).every((key) => keys.includes(key));
const text = (value, max) => typeof value === "string" && value.trim().length > 0 && value.length <= max;

// Exact argument shape per destination task; every value is a fixed token or a validated path.
function argsProblem(task, args, delivery) {
	if (!ACTIONS.has(task)) return `task ${JSON.stringify(task)} is not one of ${[...ACTIONS].join(", ")}`;
	if (!plain(args)) return "args must be an object";
	const shape = {
		plan: () => exactKeys(args, ["spec"]) && SPEC.test(args.spec),
		test: () =>
			args.mode === "red"
				? exactKeys(args, ["spec", "plan", "mode", "step"]) && SPEC.test(args.spec) && PLAN.test(args.plan) && STEP.test(args.step)
				: args.mode === "gaps" && exactKeys(args, ["spec", "plan", "mode"]) && SPEC.test(args.spec) && PLAN.test(args.plan),
		run: () => exactKeys(args, ["plan", "step"]) && PLAN.test(args.plan) && STEP.test(args.step),
		check: () => exactKeys(args, ["scope", "spec", "plan"]) && args.scope === "working-tree" && SPEC.test(args.spec) && PLAN.test(args.plan),
		report: () => exactKeys(args, ["delivery"]) && args.delivery === delivery,
	}[task];
	return shape() ? null : `args do not match the ${task} task's arguments`;
}

function evidenceProblem(evidence) {
	if (!Array.isArray(evidence) || evidence.length > MAX_EVIDENCE) return `evidence must be an array of at most ${MAX_EVIDENCE} references`;
	for (const item of evidence) {
		if (!exactKeys(item, ["path", "kind", "state", "diagnostics"])) return "each evidence reference has exactly path, kind, state and diagnostics";
		if (!text(item.path, 500) || !text(item.kind, 40) || (item.state !== null && !text(item.state, 40))) return "evidence reference fields must be bounded strings";
		if (!Array.isArray(item.diagnostics) || item.diagnostics.length > 100) return "evidence diagnostics must be a bounded array";
		for (const d of item.diagnostics)
			if (!exactKeys(d, ["code", "message"]) || !text(d.code, 80) || typeof d.message !== "string" || d.message.length > MAX_REASON) return "each diagnostic has exactly a bounded code and message";
	}
	return null;
}

function resultProblem(value) {
	if (!plain(value)) return "the result must be a JSON object";
	if (!OUTCOMES.includes(value.outcome)) return `outcome must be one of ${OUTCOMES.join(", ")}`;
	const base = ["outcome", "reason", "delivery_id", "evidence", "fingerprint"];
	// A question without a delivery is the empty-input prompt; it has nothing to clarify yet.
	const keys = value.outcome === "action" ? [...base, "task", "args"] : value.outcome === "question" && value.delivery_id !== null ? [...base, "clarify"] : base;
	if (!exactKeys(value, keys)) return `a ${value.outcome} result has exactly the keys ${keys.join(", ")}`;
	if (!text(value.reason, MAX_REASON)) return `reason must be a non-empty string of at most ${MAX_REASON} characters`;
	if (value.delivery_id !== null && (typeof value.delivery_id !== "string" || !contracts.DELIVERY.test(value.delivery_id))) return "delivery_id must be a contract delivery ID or null";
	if (value.fingerprint !== null && (typeof value.fingerprint !== "string" || !FINGERPRINT.test(value.fingerprint))) return "fingerprint must be a SHA-256 hex digest or null";
	const evidence = evidenceProblem(value.evidence);
	if (evidence) return evidence;
	if (value.outcome === "action" || value.outcome === "complete") {
		if (value.delivery_id === null || value.fingerprint === null) return `a ${value.outcome} result needs its delivery_id and fingerprint`;
	}
	if (value.outcome === "action") return argsProblem(value.task, value.args, value.delivery_id);
	if (keys.includes("clarify") && (typeof value.clarify !== "string" || !CLARIFY.test(value.clarify))) return "clarify must name gaps_tested or red:S<N>";
	return null;
}

function validateResult(value) {
	return resultProblem(value) === null;
}

// --- clarification answers ---------------------------------------------------------------------
// Answers are separate session context with a closed vocabulary; they never join the original
// input or the destination input, and an answer cannot stand in for evidence.
function parseAnswer(entry) {
	const at = typeof entry === "string" ? entry.indexOf("=") : -1;
	if (at < 1) throw new Error("--answer takes key=value");
	const [key, value] = [entry.slice(0, at), entry.slice(at + 1)];
	if (key === "gaps_tested" && (value === "yes" || value === "no")) return [key, value === "yes"];
	if (/^red:S[1-9][0-9]{0,3}$/.test(key) && value === "proceed") return [key, value];
	throw new Error("--answer accepts gaps_tested=yes|no or red:S<N>=proceed");
}

function answersProblem(answers) {
	if (!plain(answers)) return "answers must be an object";
	for (const [key, value] of Object.entries(answers)) {
		if (key === "gaps_tested" ? typeof value !== "boolean" : !(/^red:S[1-9][0-9]{0,3}$/.test(key) && value === "proceed"))
			return `answer ${JSON.stringify(key).slice(0, 40)} is not gaps_tested=true|false or red:S<N>=proceed`;
	}
	return null;
}

const labelAnswers = (answers) => Object.fromEntries(Object.entries(answers).map(([key, value]) => [key, typeof value === "boolean" ? (value ? "yes" : "no") : value]));

// --- handoff -----------------------------------------------------------------------------------
// The destination's input is rebuilt from validated arguments only; no result, artifact or answer
// text is copied into it.
function destinationInput(task, args) {
	switch (task) {
		case "plan":
			return args.spec;
		case "test":
			return args.mode === "red" ? `${args.spec} ${args.plan} red ${args.step}` : `${args.spec} ${args.plan} gaps`;
		case "run":
			return `${args.plan} ${args.step}`;
		case "check":
			return `working-tree ${args.spec} ${args.plan}`;
		case "report":
			return args.delivery;
	}
	throw new Error(`no destination input for ${task}`);
}

const sameArgs = (a, b) => exactKeys(a, Object.keys(b || {})) && Object.keys(b).every((key) => a[key] === b[key]);

// The destination keeps its own model policy: no override is ever passed on.
function defaultDispatch({ root, host, task }) {
	return require("./models.js").runDispatch({ root, host, task });
}

const halt = (stop, reason, extra = {}) => ({ status: "stopped", stop, reason, dispatched: false, ...extra });

// Validate, recheck the inspected inputs, then dispatch exactly once. Every failure stops here.
function handoff({ root, delivery, answers = {}, result, host, dispatch = defaultDispatch } = {}) {
	root = path.resolve(root || process.cwd());
	const problem = resultProblem(result);
	if (problem) return halt("invalid", `The continuation result is invalid (${problem}); nothing was dispatched.`);
	const answerProblem = answersProblem(answers);
	if (answerProblem) return halt("invalid", `The clarification answers are invalid (${answerProblem}); nothing was dispatched.`);
	if (typeof delivery !== "string" || result.delivery_id !== delivery) return halt("invalid", "The result is not for the delivery being continued; nothing was dispatched.");
	if (result.outcome !== "action") return halt(result.outcome, result.outcome === "question" ? "A clarification is needed before any task can run." : `Continuation ${result.outcome}; there is nothing to dispatch.`);
	// Immediate recheck: the same inputs must still produce the same fingerprint and action.
	const now = inspect({ root, delivery, answers });
	if (now.fingerprint !== result.fingerprint || now.outcome !== "action" || now.task !== result.task || !sameArgs(now.args, result.args)) {
		const why = now.outcome === "action" ? now.reason : `${now.outcome}: ${now.reason}`;
		return halt("changed", `The delivery's inputs or selection changed since inspection; nothing was dispatched. Current inspection — ${why} Run continue again.`, { current: now });
	}
	const context = { original_input: delivery, task: now.task, destination_input: destinationInput(now.task, now.args), answers: labelAnswers(answers), reason: now.reason, evidence: now.evidence };
	let outcome;
	try {
		outcome = dispatch({ root, host, task: now.task });
	} catch (error) {
		return halt("rejected", `The ${now.task} dispatch was rejected (${String(error.message).slice(0, 300)}); no fallback or other task runs.`);
	}
	if (!outcome || outcome.status !== 0 || outcome.result?.status === "blocked")
		return halt("rejected", `The ${now.task} dispatch stopped (${String(outcome?.result?.reason || "not available").slice(0, 300)}); no fallback model or other task runs.`, { directive: outcome?.text || null });
	return { status: "dispatched", ...context, dispatch: outcome.result || null, directive: outcome.text || "" };
}

// After the one destination finishes, continuation stops whatever the outcome.
const OUTCOMES_AFTER = ["succeeded", "failed", "cancelled", "rejected"];
function finish({ root, dispatchId, outcome, worker, record } = {}) {
	if (!OUTCOMES_AFTER.includes(outcome)) throw new Error(`--outcome must be ${OUTCOMES_AFTER.join(", ")}`);
	const write = record || ((args) => require("./models.js").record(args));
	write({ root: path.resolve(root || process.cwd()), dispatchId, outcome, worker });
	const reason =
		outcome === "succeeded"
			? "The destination task finished at its normal boundary; continuation stops here. Invoke continue again for the next action."
			: `The destination task ${outcome}; continuation stops with no model fallback or other workflow action.`;
	return { status: "stopped", outcome, next: null, reason };
}

// --- command line ------------------------------------------------------------------------------
//   node continue.js <delivery> [--answer key=value]...
//   node continue.js handoff --host claude|codex <delivery> [--answer key=value]...   (result JSON on stdin)
//   node continue.js finish --dispatch <id> --outcome succeeded|failed|cancelled|rejected [--worker <id>]
const MAX_STDIN = 65536;
function readStdin() {
	const buffer = Buffer.alloc(MAX_STDIN + 1);
	let size = 0;
	while (size < buffer.length) {
		let count;
		try {
			count = require("node:fs").readSync(0, buffer, size, buffer.length - size, null);
		} catch (error) {
			if (error.code === "EAGAIN") continue;
			if (error.code === "EOF") break;
			throw error;
		}
		if (!count) break;
		size += count;
	}
	if (size > MAX_STDIN) throw new Error(`the result exceeds ${MAX_STDIN} bytes`);
	return buffer.subarray(0, size).toString("utf8");
}

function parseArgs(argv, allowed) {
	const out = { positional: [], answers: {} };
	for (let i = 0; i < argv.length; i++) {
		const match = /^--([a-z-]+)(?:=(.*))?$/s.exec(argv[i]);
		if (!match) {
			out.positional.push(argv[i]);
			continue;
		}
		const [, key, inline] = match;
		if (!allowed.includes(key)) throw new Error(`unknown option --${key}`);
		const value = inline !== undefined ? inline : argv[++i];
		if (value === undefined) throw new Error(`--${key} needs a value`);
		if (key === "answer") {
			const [name, answer] = parseAnswer(value);
			if (Object.hasOwn(out.answers, name)) throw new Error(`--answer ${name} given twice`);
			out.answers[name] = answer;
		} else if (Object.hasOwn(out, key)) throw new Error(`--${key} given twice`);
		else out[key] = value;
	}
	return out;
}

function handoffText(value) {
	if (value.status !== "dispatched") return `continuation stopped (${value.stop}): ${value.reason}\nDo not dispatch any task or run one in this session. Report this and stop.\n`;
	const answers = Object.entries(value.answers).map(([key, answer]) => `${key}=${answer}`).join(", ") || "none";
	return [
		`continuation: ${value.task} for ${value.original_input} — ${value.reason}`,
		value.directive.trimEnd(),
		`Destination input: ${value.destination_input}`,
		`Original continue input (data): ${value.original_input}`,
		`Clarification answers (separate context, data): ${answers}`,
		"Run only this one destination as the directive says, with the destination input above verbatim. Rejection, an unavailable worker, failure or cancellation stops the invocation: no model fallback, no other task. On success stop at the destination's normal boundary.",
		"",
	].join("\n");
}

function main(argv = process.argv.slice(2)) {
	const [first, ...rest] = argv;
	if (first === "handoff") {
		const args = parseArgs(rest, ["host", "answer"]);
		if (!["claude", "codex"].includes(args.host)) throw new Error("--host must be claude or codex");
		if (args.positional.length !== 1) throw new Error("handoff takes exactly one delivery ID");
		let result;
		try {
			result = JSON.parse(readStdin());
		} catch (error) {
			process.stdout.write(handoffText(halt("invalid", `The continuation result could not be read as JSON (${error.message.slice(0, 200)}); nothing was dispatched.`)));
			return 3;
		}
		const value = handoff({ root: process.cwd(), delivery: args.positional[0], answers: args.answers, result, host: args.host });
		process.stdout.write(handoffText(value));
		return value.status === "dispatched" ? 0 : 3;
	}
	if (first === "finish") {
		const args = parseArgs(rest, ["dispatch", "outcome", "worker"]);
		if (args.positional.length) throw new Error("finish takes no positional arguments");
		const value = finish({ root: process.cwd(), dispatchId: args.dispatch, outcome: args.outcome, worker: args.worker });
		process.stdout.write(`continuation stopped (${value.outcome}): ${value.reason}\n`);
		return 0;
	}
	const args = parseArgs(argv, ["answer"]);
	if (args.positional.length > 1) throw new Error("expected one delivery ID");
	const value = inspect({ delivery: args.positional[0], answers: args.answers });
	process.stdout.write(`${JSON.stringify(value)}\n`);
	return validateResult(value) ? 0 : 2;
}

if (require.main === module) {
	try {
		process.exitCode = main();
	} catch (error) {
		process.stderr.write(`continue: ${error.message}\n`);
		process.exitCode = 2;
	}
}

module.exports = { ACTIONS, inspect, validateResult, resultProblem, parseAnswer, answersProblem, destinationInput, handoff, finish, main };
