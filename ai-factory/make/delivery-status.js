#!/usr/bin/env node
// Compact, read-only status of one contract delivery (spec 0019). It collects and evaluates the
// delivery once through delivery-report.js — the same collector retries, guarded readers and
// readiness rules as a completion report — and prints a two-column field/value table. It never
// generates or saves a report, reads no saved report, runs no verification, writes nothing and
// names no next action. Progress is derived from artifacts and evidence; it never claims that an
// agent is running. Missing or unreadable evidence is shown as unknown, never as success.
//
//   node ai-factory/make/delivery-status.js <delivery id>
//   exit 0: a status is shown (whatever its readiness) · exit 2: a refusal, one line on stderr
const fs = require("node:fs");
const path = require("node:path");
const contracts = require("./contracts.js");
const reports = require("./delivery-report.js");

// Preserve the invoked workspace when its entry point is a source-repo symlink (see runner.js).
const ENTRY_DIRECTORY = require.main === module ? path.dirname(process.argv[1]) : __dirname;
const ROOT = fs.realpathSync(path.resolve(ENTRY_DIRECTORY, "../.."));
const { Invocation } = contracts;
const FORM = "d-YYYYMMDD-xxxxxx";
const READ_FAILURES = new Set(["E_MISSING_FILE", "E_SYMLINK", "E_UNREADABLE", "E_PATH_ESCAPE"]);
const EVIDENCE_NAME = /^(?:S\d+-(?:red|step)|final|quick)\.json$/;
const NOT_RUNNING = "evidence-derived, not a claim that work is running";

// --- identity ------------------------------------------------------------------------------
// One delivery, one identity: refuse rather than guess when sidecars disagree or none exist.
function identify(ctx, id) {
	const index = contracts.scan(ctx);
	const mine = index.sidecars.filter((item) => item.value?.delivery_id === id);
	const names = (items) => items.map((item) => ctx.rel(item.file)).sort().join(", ");
	for (const kind of ["spec", "plan", "quick"]) {
		const same = mine.filter((item) => item.kind === kind);
		if (same.length > 1) throw new Invocation(`delivery-status: more than one ${kind} sidecar carries ${id} (${names(same)}); a delivery ID must identify one delivery`);
	}
	if (mine.some((item) => item.kind === "quick") && mine.some((item) => item.kind !== "quick"))
		throw new Invocation(`delivery-status: ${id} is carried by both quick and planned sidecars (${names(mine)}); a delivery is one or the other`);
	if (!mine.length) {
		const evidence = contracts.guardPath(ctx, path.join(ctx.workspace, "evidence", id));
		if (evidence?.code === "E_MISSING_FILE") throw new Invocation(`delivery-status: no contract delivery matches ${id}; make contracts lists them`);
		throw new Invocation(`delivery-status: no spec, plan or quick sidecar carries ${id}; only evidence remains under ai-factory/evidence/${id}, which proves nothing on its own`);
	}
}

// --- timestamps ----------------------------------------------------------------------------
// contracts.js does not validate finished_at, so this does: an RFC 3339 date-time with an explicit
// offset and real calendar values, compared as an instant (nanoseconds, so no rounding ties).
const TIMESTAMP = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})(?:\.(\d{1,9}))?(Z|([+-])(\d{2}):(\d{2}))$/;
function instant(value) {
	if (typeof value !== "string") return null;
	const m = TIMESTAMP.exec(value);
	if (!m) return null;
	const [year, month, day, hour, minute, second] = m.slice(1, 7).map(Number);
	const days = new Date(Date.UTC(year, month, 0)).getUTCDate();
	if (month < 1 || month > 12 || day < 1 || day > days || hour > 23 || minute > 59 || second > 59) return null;
	let offset = 0;
	if (m[8] !== "Z") {
		const [hours, minutes] = [Number(m[10]), Number(m[11])];
		if (hours > 23 || minutes > 59) return null;
		offset = (m[9] === "-" ? -1 : 1) * (hours * 60 + minutes);
	}
	const utc = Date.UTC(year, month - 1, day, hour, minute, second) - offset * 60000;
	if (!Number.isFinite(utc)) return null;
	return BigInt(utc) * 1000000n + BigInt((m[7] || "").padEnd(9, "0"));
}

// --- presentation --------------------------------------------------------------------------
const flat = (value) => String(value ?? "").replace(/[\u0000-\u001f\u007f]+/g, " ").replace(/ {2,}/g, " ").trim();
const reasonText = (item) => `${item.code}: ${item.message}${item.evidence_ref ? ` (${item.evidence_ref})` : ""}`;
const isReview = (ref) => /\/review\.json$/.test(ref || "");

function context(collected) {
	const id = collected.delivery.id;
	const source = (kind) => collected.delivery.sources.find((item) => item.kind === kind) || null;
	const diagnostics = collected.contract.artifacts.flatMap((artifact) => artifact.diagnostics);
	// A source whose Markdown could not be read through the guarded reader.
	const unreadable = (file) => diagnostics.some((item) => item.path === file && READ_FAILURES.has(item.code));
	const verification = collected.verification.filter((item) => !isReview(item.evidence));
	const final = collected.verification.find((item) => item.evidence === `ai-factory/evidence/${id}/${collected.delivery.kind === "quick" ? "quick.json" : "final.json"}`) || null;
	return { id, source, unreadable, verification, final };
}

// Conservative progress: the earliest phase the artifacts and evidence establish, else unknown.
function progress(collected, readiness, facts) {
	const label = (name, why) => `${name} — ${why} (${NOT_RUNNING})`;
	const unknown = (why) => label("unknown", why);
	let finishedWhy;
	if (collected.delivery.kind === "quick") {
		const quick = facts.source("quick");
		if (!quick || facts.unreadable(quick.markdown)) return unknown("the quick checklist could not be read");
		const { ids, ticked } = collected.progress.checks;
		if (!ids.length) return unknown("the checklist declares no items");
		if (ids.some((qc) => !ticked.includes(qc))) {
			if (readiness.status === "ready") return unknown("evaluation is ready while checklist items are unticked");
			return label("implementation", `${ticked.length} of ${ids.length} checklist items ticked`);
		}
		finishedWhy = "every checklist item is ticked";
	} else {
		const spec = facts.source("spec");
		const plan = facts.source("plan");
		if (!spec) return unknown("no spec sidecar carries this delivery");
		if (facts.unreadable(spec.markdown)) return unknown("the spec could not be read");
		if (!plan) {
			if (facts.verification.length) return unknown("verification is recorded but no plan sidecar exists");
			return label("planning", "a spec without a plan");
		}
		if (facts.unreadable(plan.markdown)) return unknown("the plan could not be read");
		const active = collected.progress.steps.filter((step) => !step.withdrawn);
		if (!active.length) return unknown("the plan lists no active steps");
		const parsed = collected.progress.steps.map((step) => step.id).sort().join(",");
		const declared = (collected.plan?.steps || []).map((step) => step.id).sort().join(",");
		if (parsed !== declared) return unknown("the plan's steps and its sidecar disagree");
		const done = active.filter((step) => step.done).length;
		if (done < active.length) {
			if (readiness.status === "ready") return unknown("evaluation is ready while plan steps are unticked");
			return label("implementation", `${done} of ${active.length} active steps ticked`);
		}
		finishedWhy = "every active step is ticked";
	}
	if (readiness.status === "ready") return label("ready", "the existing report evaluation is ready under project policy");
	const final = facts.final;
	if (!final || final.status !== "passed" || final.state !== "valid") {
		const what = !final ? "no final verification is recorded" : final.status !== "passed" ? `final verification is ${final.status}` : `final verification is ${final.state || "unchecked"}`;
		return label("verification-unproven", `${finishedWhy}, but ${what}`);
	}
	const open = readiness.reasons.filter((item) => item.state !== "ready");
	if (open.length && open.every((item) => item.code.startsWith("REVIEW_") || isReview(item.evidence_ref)))
		return label("review-pending", "final verification passed; the review requirement is not met by a current approval");
	return label("verification-unproven", "final verification passed, but other evidence is missing, stale or blocking");
}

function recorded(collected, facts) {
	if (collected.delivery.kind === "quick") {
		const quick = facts.source("quick");
		if (!quick || facts.unreadable(quick.markdown)) return ["checks", "unknown — the quick checklist could not be read"];
		const { ids, ticked } = collected.progress.checks;
		return ["checks", `${ticked.length} of ${ids.length} ticked — recorded progress, not proof`];
	}
	const plan = facts.source("plan");
	if (!plan) return ["steps", "none — no plan is recorded yet"];
	if (facts.unreadable(plan.markdown)) return ["steps", "unknown — the plan could not be read"];
	const steps = collected.progress.steps;
	const active = steps.filter((step) => !step.withdrawn);
	const withdrawn = steps.length - active.length;
	const partly = active.filter((step) => step.mark === "~").length;
	return ["steps", `${active.filter((step) => step.done).length} of ${active.length} ticked${partly ? `; ${partly} part-done` : ""}${withdrawn ? `; ${withdrawn} withdrawn` : ""} — recorded progress, not proof`];
}

function grouped(readiness) {
	const pick = (results) => readiness.criteria.filter((row) => results.includes(row.result));
	const list = (rows, withResult = false) => (rows.length ? rows.map((row) => (withResult ? `${row.id} (${row.result})` : row.id)).join(", ") : "none");
	return [
		["verified", list(pick(["passed"]))],
		["attested", pick(["attested"]).length ? `${list(pick(["attested"]))} — human attestation, not executable proof` : "none"],
		["remaining", list(pick(["pending", "failed", "uncovered"]), true)],
		["unknown", pick(["unverified"]).length ? `${list(pick(["unverified"]))} — no current passing evidence` : "none"],
	];
}

// Latest non-review verification by recorded finished_at, ascending evidence path on ties. Any
// record whose time cannot be read makes the chronology unknown; file times are never used.
function latest(collected, facts) {
	const listed = new Set(collected.verification.map((item) => item.evidence));
	const prefix = `ai-factory/evidence/${facts.id}`;
	const problems = [];
	for (const artifact of collected.contract.artifacts) {
		if (artifact.kind !== "evidence" || listed.has(artifact.path)) continue;
		if (artifact.path !== prefix && !(artifact.path.startsWith(`${prefix}/`) && EVIDENCE_NAME.test(artifact.path.slice(prefix.length + 1)))) continue;
		const codes = artifact.diagnostics.filter((item) => item.code !== "E_EVIDENCE_NOT_RUN" && !item.code.startsWith("I_")).map((item) => item.code);
		if (codes.length) problems.push(`${artifact.path} cannot be read (${[...new Set(codes)].join(", ")})`);
	}
	let best = null;
	for (const item of facts.verification) {
		const at = instant(item.finished_at);
		if (at === null) problems.push(`${item.evidence} has a missing or invalid finished_at`);
		else if (!best || at > best.at) best = { at, item };
	}
	if (problems.length) return `unknown — chronology cannot be established: ${problems.join("; ")}`;
	if (!best) return "none — no verification is recorded";
	const item = best.item;
	const signal = (item.commands || []).find((command) => command.signal)?.signal;
	return [item.evidence, item.phase, `expects ${item.expected}`, signal ? `${item.status}, interrupted (${signal})` : item.status, item.state || "unchecked", `finished ${item.finished_at}`].join(" · ");
}

function reviewRow(collected) {
	const review = collected.review;
	if (!review.evidence) return `not recorded — ${review.required ? "required" : "not required"} by policy`;
	const findings = review.findings;
	const blocking = findings ? findings.filter((finding) => finding.blocking).length : null;
	return [
		review.verdict || "no valid verdict",
		`status ${review.status || "unknown"}`,
		review.state || "unchecked",
		findings === null ? "findings not recorded" : blocking ? `${blocking} blocking of ${findings.length} finding(s)` : `no blocking findings of ${findings.length}`,
		review.evidence,
		...(review.reason ? [review.reason] : []),
	].join(" · ");
}

function rows(collected, readiness) {
	const facts = context(collected);
	const blockers = readiness.reasons.filter((item) => item.state === "blocked");
	const others = readiness.reasons.filter((item) => item.state !== "blocked");
	const evidenceCount = collected.verification.length + collected.attestations.length;
	return [
		["delivery", facts.id],
		["kind", collected.delivery.kind],
		["progress", progress(collected, readiness, facts)],
		["readiness", `${readiness.status} — existing report evaluation under project policy (contracts: ${collected.contract.status})`],
		recorded(collected, facts),
		...grouped(readiness),
		["latest verification", latest(collected, facts)],
		["review", reviewRow(collected)],
		["blockers", blockers.length ? blockers.map(reasonText).join("; ") : "none recorded — absence of a blocker does not prove readiness"],
		["limitations", others.length ? others.map(reasonText).join("; ") : "none"],
		...collected.delivery.sources.map((item) => ["source", `${item.kind} ${item.markdown} · ${item.sidecar} (${item.state})`]),
		["source", `evidence ai-factory/evidence/${facts.id}/ (${evidenceCount ? `${evidenceCount} record(s)` : "none recorded"})`],
	];
}

function render(table) {
	return `${["field\tvalue", ...table.map(([field, value]) => `${field}\t${flat(value) || "none"}`)].join("\n")}\n`;
}

// --- entry ---------------------------------------------------------------------------------
function status(options = {}) {
	const id = options.delivery;
	if (typeof id !== "string" || !contracts.DELIVERY.test(id)) throw new Invocation(`delivery-status: expected one contract delivery ID of the form ${FORM}`);
	const ctx = contracts.context(options.root || ROOT);
	ctx.config = contracts.readConfig(ctx);
	if (!ctx.config.adopted) throw new Invocation("delivery-status: artifact contracts are not enabled in this workspace; run contracts.js enable to opt in");
	identify(ctx, id);
	const collected = reports.collect({ ctx, delivery: id });
	const readiness = reports.evaluate(collected);
	return render(rows(collected, readiness));
}

function main(argv = process.argv.slice(2)) {
	const err = (text) => process.stderr.write(`${flat(text)}\n`);
	try {
		if (!argv.length) throw new Invocation(`delivery-status: name one delivery ID (${FORM}); make contracts lists them`);
		if (argv.length > 1) throw new Invocation(`delivery-status: expected exactly one delivery ID (${FORM})`);
		process.stdout.write(status({ delivery: argv[0] }));
		return 0;
	} catch (error) {
		if (error instanceof Invocation) {
			err(error.message);
			return 2;
		}
		err(`delivery-status: could not read the delivery: ${error.message}`);
		return 1;
	}
}

if (require.main === module) process.exitCode = main();
module.exports = { status, instant, main };
