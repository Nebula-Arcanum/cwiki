#!/usr/bin/env node
// A completion report assembles evidence that already exists. It never runs a check, edits a
// source artifact or publishes anything; it writes only ai-factory/reports/<id>/. The status is
// computed once from one model, and both the Markdown and the JSON are rendered from that model.
const fs = require("node:fs");
const path = require("node:path");
const crypto = require("node:crypto");
const { execFileSync } = require("node:child_process");
const contracts = require("./contracts.js");
const { digest } = require("./gate.js");
// Preserve the invoked workspace when its entry point is a source-repo symlink (see runner.js).
const ENTRY_DIRECTORY =
	require.main === module ? path.dirname(process.argv[1]) : __dirname;
const ROOT = fs.realpathSync(path.resolve(ENTRY_DIRECTORY, "../.."));

const SCHEMA = "t4-delivery-report";
const VERSION = 1;
const TELEMETRY = { schema: "t4-delivery-telemetry", version: 1 };
const STATES = ["ready", "incomplete", "unverified", "blocked"];
const ATTEMPTS = 3;
const MAX_PATHS = 200;
const NOTE =
	"Assembled from local evidence only. Ready means ready for delivery handoff — not merged, deployed or published. Contract validation is structural; it does not judge whether a requirement is right.";
const { Invocation } = contracts;
const worstState = (states) =>
	states.reduce((a, b) => (STATES.indexOf(b) > STATES.indexOf(a) ? b : a), "ready");
const clip = (text, size) => {
	const value = String(text ?? "");
	return value.length > size ? `${value.slice(0, size - 1)}…` : value;
};

// --- collection --------------------------------------------------------------------------
function git(ctx, args) {
	try {
		return execFileSync("git", args, {
			cwd: ctx.root,
			encoding: "utf8",
			stdio: ["ignore", "pipe", "ignore"],
			env: { ...process.env, GIT_OPTIONAL_LOCKS: "0" },
			maxBuffer: 64 * 1024 * 1024,
		});
	} catch {
		return null;
	}
}
// The same branch base the review runner uses: main, then develop, else none.
function changes(ctx) {
	let base = null;
	for (const branch of ["main", "develop"]) {
		const found = git(ctx, ["merge-base", "HEAD", branch]);
		if (found?.trim()) {
			base = { branch, commit: found.trim() };
			break;
		}
	}
	const entries = new Map();
	if (base) {
		const listed = git(ctx, ["diff", "--name-status", "--no-renames", "-z", base.commit, "--"]) || "";
		const parts = listed.split("\0").filter(Boolean);
		for (let index = 0; index + 1 < parts.length; index += 2) entries.set(parts[index + 1], parts[index][0]);
	}
	for (const file of (git(ctx, ["ls-files", "--others", "--exclude-standard", "-z", "--"]) || "").split("\0").filter(Boolean))
		if (!entries.has(file)) entries.set(file, "?");
	// The report's own output and run scratch are not part of the delivery's change set.
	for (const file of [...entries.keys()]) if (/^ai-factory\/(?:reports|runs)\//.test(file)) entries.delete(file);
	const paths = [...entries.entries()].map(([file, status]) => ({ path: file, status })).sort((a, b) => (a.path < b.path ? -1 : 1));
	const counts = { added: 0, modified: 0, deleted: 0, untracked: 0, other: 0 };
	const names = { A: "added", M: "modified", D: "deleted", "?": "untracked" };
	for (const item of paths) counts[names[item.status] || "other"]++;
	return { base, counts, total: paths.length, paths: paths.slice(0, MAX_PATHS), truncated: paths.length > MAX_PATHS };
}
// Every byte the report depends on, so a report never mixes two states of the evidence.
function fingerprint(ctx, id) {
	const lines = [];
	const add = (file) => {
		const read = contracts.readInside(ctx, file);
		lines.push(`${ctx.rel(file)}\0${read.bytes ? digest(read.bytes) : read.code}\n`);
	};
	const index = contracts.scan(ctx);
	for (const sidecar of index.sidecars.filter((item) => item.value?.delivery_id === id).sort((a, b) => (a.file < b.file ? -1 : 1))) {
		add(sidecar.file);
		add(sidecar.markdownFile);
	}
	const evidence = path.join(ctx.workspace, "evidence", id);
	if (!contracts.guardPath(ctx, evidence))
		for (const name of fs.readdirSync(evidence).sort()) add(path.join(evidence, name));
	add(path.join(ctx.workspace, "runs", "telemetry", `${id}.json`));
	// A selected preset changes what the evidence must show, so a report made under another
	// selection never matches. Absent selection adds nothing: legacy fingerprints are unchanged.
	const selection = path.join(ctx.workspace, "assurance.json");
	if (contracts.readInside(ctx, selection).code !== "E_MISSING_FILE") add(selection);
	// Live lifecycle events count too: the telemetry section is computed from them when enabled.
	const events = path.join(ctx.workspace, "runs", "lifecycle", "events");
	if (!contracts.guardPath(ctx, events)) for (const name of fs.readdirSync(events).sort()) if (!name.startsWith(".")) add(path.join(events, name));
	const code = contracts.snapshot(ctx, ctx.config);
	lines.push(`code\0${code ? code.snapshot_sha256 : "unavailable"}\n`);
	return digest(lines.join(""));
}
function section(text, heading) {
	const lines = (text || "").split(/\r?\n/);
	const start = lines.findIndex((line) => new RegExp(`^#{1,6}\\s+${heading}\\b`, "i").test(line));
	if (start < 0) return null;
	const level = lines[start].match(/^#+/)[0].length;
	const body = [];
	for (const line of lines.slice(start + 1)) {
		const next = line.match(/^(#+)\s/);
		if (next && next[1].length <= level) break;
		body.push(line);
	}
	const value = body.join("\n").trim();
	return value ? clip(value, 2000) : null;
}
// With lifecycle telemetry enabled the section is computed from the live event stream through
// make/lifecycle.js; otherwise an exported file is read if one exists. Either way it is validated
// the same way and never affects status.
function lifecycleExport(ctx, id) {
	let lifecycle;
	try {
		lifecycle = require("./lifecycle.js");
		if (!lifecycle.store.config(ctx.workspace).enabled) return null;
		return { value: lifecycle.telemetryFor({ ai: ctx.workspace, delivery: id }), source: "lifecycle events (ai-factory/runs/lifecycle/)" };
	} catch (error) {
		if (!lifecycle) return null;
		return { error: `lifecycle telemetry could not be computed: ${error.message}` };
	}
}
function telemetry(ctx, id) {
	const file = path.join(ctx.workspace, "runs", "telemetry", `${id}.json`);
	const live = lifecycleExport(ctx, id);
	if (live?.error) return { status: "incompatible", reason: live.error, source: "ai-factory/runs/lifecycle/", coverage: null, metrics: null, pricing: null };
	if (live) return checkTelemetry(live.value, id, live.source);
	const read = contracts.readInside(ctx, file);
	const unavailable = (status, reason) => ({ status, reason, source: ctx.rel(file), coverage: null, metrics: null, pricing: null });
	if (read.code === "E_MISSING_FILE") return unavailable("unavailable", "no delivery-scoped telemetry export exists for this delivery");
	if (!read.bytes) return unavailable("unavailable", read.message);
	let value;
	try {
		value = JSON.parse(read.bytes.toString("utf8"));
	} catch {
		return unavailable("incompatible", "the telemetry export is not valid JSON");
	}
	return checkTelemetry(value, id, ctx.rel(file));
}
function checkTelemetry(value, id, source) {
	const unavailable = (status, reason) => ({ status, reason, source, coverage: null, metrics: null, pricing: null });
	if (!value || value.schema !== TELEMETRY.schema) return unavailable("incompatible", `expected schema ${TELEMETRY.schema}`);
	if (value.version !== TELEMETRY.version) return unavailable("incompatible", `unsupported telemetry version ${JSON.stringify(value.version)}`);
	if (value.delivery_id !== id) return unavailable("incompatible", "the export names a different delivery");
	const coverage = value.coverage;
	if (!coverage || !Number.isInteger(coverage.measured) || !Number.isInteger(coverage.total) || coverage.measured < 0 || coverage.measured > coverage.total)
		return unavailable("incompatible", "coverage must give integer measured ≤ total");
	const metrics = {};
	for (const [key, metric] of Object.entries(value.metrics || {})) {
		// Unknown stays unknown: only a finite number is a measurement.
		if (metric !== null && !(typeof metric === "number" && Number.isFinite(metric)))
			return unavailable("incompatible", `metric ${key} must be a number or null`);
		metrics[key] = metric;
	}
	if (coverage.total === 0) return { ...unavailable("unavailable", "no lifecycle runs are recorded for this delivery"), coverage: { measured: 0, total: 0 } };
	return {
		status: coverage.measured < coverage.total ? "partial" : "available",
		reason: coverage.measured < coverage.total ? `${coverage.measured} of ${coverage.total} runs were measured` : null,
		source,
		coverage: { measured: coverage.measured, total: coverage.total },
		metrics,
		pricing: value.pricing && typeof value.pricing.source === "string" ? { source: clip(value.pricing.source, 200) } : null,
	};
}
function build(ctx, id, env = process.env) {
	const index = contracts.scan(ctx);
	const mine = index.sidecars.filter((item) => item.value?.delivery_id === id);
	const evidenceDir = path.join(ctx.workspace, "evidence", id);
	const guard = contracts.guardPath(ctx, evidenceDir);
	if (!mine.length && guard?.code === "E_MISSING_FILE")
		throw new Invocation(`delivery-report: no sidecar or evidence carries delivery ${id}`);
	// The completion policy the selected preset makes effective (the configured one without a preset).
	const assured = contracts.assurance(ctx, env);
	const policy = assured.completion;
	const byKind = (kind) => mine.filter((item) => item.kind === kind);
	const [specSide] = byKind("spec");
	const [planSide] = byKind("plan");
	const [quickSide] = byKind("quick");
	const markdown = (sidecar) => {
		if (!sidecar) return null;
		const read = contracts.readInside(ctx, sidecar.markdownFile);
		return read.bytes ? { bytes: read.bytes, text: read.bytes.toString("utf8") } : null;
	};
	const specMd = markdown(specSide);
	const planMd = markdown(planSide);
	const quickMd = markdown(quickSide);
	const kind = quickSide ? "quick" : "planned";
	const steps = planMd ? contracts.parsePlanSteps(planMd.text) : [];
	const active = steps.filter((step) => !step.withdrawn);
	const checks = quickMd ? contracts.parseChecks(quickMd.text) : { ids: [], ticked: [] };
	const finished = kind === "quick" ? checks.ids.length > 0 && checks.ids.every((qc) => checks.ticked.includes(qc)) : active.length > 0 && active.every((step) => step.done);
	const reviewRequired = kind === "quick" ? policy.require_review_quick : policy.require_review;
	const doc = contracts.validate({ ctx, target: id, require: finished && reviewRequired ? ["review"] : [] });
	const states = new Map(doc.artifacts.map((artifact) => [artifact.path, artifact]));
	const { records } = contracts.readEvidence(ctx, id);
	const evidenceRef = (name) => `ai-factory/evidence/${id}/${name}`;
	const record = (name) => records.get(name)?.value || null;
	const artifactState = (name) => states.get(evidenceRef(name))?.state || null;
	const first = (text) => (text ? text.split(/\r?\n/).find((line) => /^#\s/.test(line))?.replace(/^#\s+/, "").trim() : null);
	const sources = mine
		.map((sidecar) => {
			const md = markdown(sidecar);
			return {
				kind: sidecar.kind,
				markdown: ctx.rel(sidecar.markdownFile),
				sidecar: ctx.rel(sidecar.file),
				sha256: md ? contracts.contentDigest(md.bytes, sidecar.kind) : null,
				state: states.get(ctx.rel(sidecar.file))?.state || "invalid",
			};
		})
		.sort((a, b) => (a.sidecar < b.sidecar ? -1 : 1));
	const code = contracts.snapshot(ctx, ctx.config);
	const verification = [...records.entries()]
		.filter(([, item]) => item.shape.kind !== "attest")
		.sort(([a], [b]) => (a < b ? -1 : 1))
		.map(([name, item]) => ({
			evidence: evidenceRef(name),
			scope: item.value.scope,
			phase: item.value.phase,
			expected: item.value.expected,
			status: item.value.status,
			state: artifactState(name),
			commands: (item.value.commands || []).map((command) => ({ argv: command.argv, exit_code: command.exit_code, signal: command.signal, status: command.status })),
			output_ref: item.value.output_ref || null,
			output_sha256: item.value.output_sha256 || null,
			finished_at: item.value.finished_at,
		}));
	const reviewRecord = record("review.json");
	const review = {
		required: reviewRequired,
		evidence: reviewRecord ? evidenceRef("review.json") : null,
		state: reviewRecord ? artifactState("review.json") : null,
		status: reviewRecord?.status || null,
		verdict: reviewRecord?.review?.verdict || null,
		tool: reviewRecord?.review?.tool || null,
		output_sha256: reviewRecord?.review?.output_sha256 || null,
		findings: Array.isArray(reviewRecord?.review?.findings) ? reviewRecord.review.findings.map((f) => ({ ...f, blocking: policy.blocking_severities.includes(f.severity) })) : null,
		reason: reviewRecord?.review?.reason || null,
		// Recorded provenance; null for review evidence written before provenance was recorded.
		independence: reviewRecord ? reviewRecord.review?.independence || null : null,
	};
	const attestations = [...records.entries()]
		.filter(([, item]) => item.shape.kind === "attest")
		.map(([name, item]) => ({
			criterion: item.value.criterion,
			evidence: evidenceRef(name),
			state: artifactState(name),
			actor: clip(item.value.actor, 200),
			attested_at: item.value.attested_at,
			rationale: clip(item.value.rationale, 1000),
			source: clip(item.value.source, 500),
		}))
		.sort((a, b) => (a.criterion < b.criterion ? -1 : 1));
	return {
		delivery: {
			id,
			kind,
			tracker_key: specSide?.value?.tracker_key || null,
			title: first(quickMd?.text) || first(specMd?.text) || id,
			sources,
		},
		snapshot: { code_sha256: code?.snapshot_sha256 || null, head: code?.head || null, files: code?.files ?? null },
		policy,
		assurance: assured.policy,
		contract: { status: doc.status, artifacts: doc.artifacts },
		plan: planSide?.value || null,
		spec: specSide?.value || null,
		quick: quickSide?.value || null,
		progress: { steps, checks, finished },
		verification,
		review,
		attestations,
		changes: changes(ctx),
		limitations: {
			open_questions: section(specMd?.text, "Open questions"),
			risks: section(planMd?.text, "Risks"),
		},
		telemetry: telemetry(ctx, id),
		sourceIds: { criteria: specSide?.value?.criteria || (specMd ? contracts.parseCriteria(specMd.text).ids : []), checks: quickSide?.value?.checks || checks.ids },
	};
}
// Collect twice around a fingerprint; a report never mixes two states of the evidence.
function collect(options = {}) {
	const ctx = options.ctx || contracts.context(options.root || ROOT);
	ctx.config = ctx.config || contracts.readConfig(ctx);
	const id = options.delivery;
	if (!contracts.DELIVERY.test(id || "")) throw new Invocation("delivery-report: DELIVERY must be d-YYYYMMDD-xxxxxx");
	for (let attempt = 1; attempt <= (options.attempts || ATTEMPTS); attempt++) {
		const before = fingerprint(ctx, id);
		ctx.snapshot = undefined;
		options.onAttempt?.(attempt);
		const collected = build(ctx, id, options.env);
		const after = fingerprint(ctx, id);
		if (before === after) return { ...collected, fingerprint: after, attempts: attempt };
	}
	throw new Invocation("delivery-report: evidence changed during collection; nothing was written — retry when writers have finished");
}

// --- readiness -----------------------------------------------------------------------------
const RED = new Set(["E_RED_PASSED"]);
function evaluate(collected) {
	const { delivery, policy, contract, progress, review, attestations } = collected;
	const reasons = [];
	const reason = (state, code, message, ref = null) => reasons.push({ state, code, message, evidence_ref: ref });
	const byPath = new Map(contract.artifacts.map((artifact) => [artifact.path, artifact]));
	const evidenceOf = (name) => collected.verification.find((item) => item.evidence === `ai-factory/evidence/${delivery.id}/${name}`) || null;
	const attested = new Map(
		attestations.filter((item) => item.state === "valid").map((item) => [item.criterion, item]),
	);
	const allowed = (criterion) => policy.allow_attestation && attested.has(criterion);
	// Criterion rows: one per declared ID; coverage comes only from the plan sidecar's lists.
	const criteria = [];
	const final = evidenceOf(delivery.kind === "quick" ? "quick.json" : "final.json");
	const finalOk = final && final.status === "passed" && final.state === "valid";
	if (delivery.kind === "quick") {
		for (const id of collected.sourceIds.checks) {
			const ticked = progress.checks.ticked.includes(id);
			const row = { id, steps: [], tests: (collected.quick?.verify || []).map((entry) => entry.argv), evidence: final ? [final.evidence] : [], attestation: attested.get(id)?.evidence || null };
			if (final && final.status === "failed" && !final.commands.some((c) => c.signal)) row.result = "failed";
			else if (!ticked) row.result = "pending";
			else if (finalOk) row.result = "passed";
			else if (allowed(id) && (!final || final.status !== "failed")) row.result = "attested";
			else row.result = "unverified";
			row.basis = row.result === "passed" ? "automated" : row.result === "attested" ? "attested" : "none";
			criteria.push(row);
		}
	} else {
		const sidecarSteps = collected.plan?.steps || [];
		for (const id of collected.sourceIds.criteria) {
			const covering = sidecarSteps.filter((step) => step.withdrawn !== true && step.criteria.includes(id));
			const stepEvidence = covering.map((step) => ({ step, parsed: progress.steps.find((item) => item.id === step.id), record: evidenceOf(`${step.id}-step.json`) }));
			const row = {
				id,
				steps: covering.map((step) => step.id),
				tests: covering.flatMap((step) => step.verify.filter((entry) => entry.phase !== "red").map((entry) => entry.argv)),
				evidence: [...stepEvidence.filter((item) => item.record).map((item) => item.record.evidence), ...(final ? [final.evidence] : [])],
				attestation: attested.get(id)?.evidence || null,
			};
			const failed = stepEvidence.some((item) => item.parsed?.done && item.record?.status === "failed" && !item.record.commands.some((c) => c.signal)) || (final && final.status === "failed" && !final.commands.some((c) => c.signal));
			const stepsOk = stepEvidence.length > 0 && stepEvidence.every((item) => item.parsed?.done && item.record?.status === "passed" && item.record.state === "valid");
			if (!covering.length) row.result = "uncovered";
			else if (failed) row.result = "failed";
			else if (stepEvidence.some((item) => !item.parsed?.done)) row.result = "pending";
			else if (stepsOk && finalOk) row.result = "passed";
			// Its steps passed; final verification is not due until every step is done.
			else if (stepsOk && !progress.finished) row.result = "pending";
			else if (allowed(id)) row.result = "attested";
			else row.result = "unverified";
			row.basis = row.result === "passed" ? "automated" : row.result === "attested" ? "attested" : "none";
			criteria.push(row);
		}
	}
	const attestedIds = new Set(criteria.filter((row) => row.result === "attested").map((row) => row.id));
	// Contract diagnostics, classified by the precedence of spec 0014.
	for (const artifact of contract.artifacts) {
		for (const item of artifact.diagnostics) {
			if (item.code.startsWith("I_")) continue;
			const ref = item.path;
			const onReview = /\/review\.json$/.test(ref);
			const match = ref.match(/\/(S\d+)-step\.json$/);
			// Review outcome and freshness are judged from the verdict and policy below.
			if (onReview && (item.code === "E_EVIDENCE_FAILED" || item.code.startsWith("S_"))) continue;
			if (item.code === "E_EVIDENCE_FAILED") {
				const recorded = collected.verification.find((entry) => entry.evidence === ref);
				if (recorded?.commands.some((command) => command.signal)) reason("unverified", "INTERRUPTED", `verification was interrupted (${recorded.commands.find((c) => c.signal).signal}); it proves nothing`, ref);
				else reason("blocked", item.code, item.message, ref);
				continue;
			}
			if (RED.has(item.code)) {
				reason("blocked", item.code, item.message, ref);
				continue;
			}
			// A step that cannot execute may be covered by an allowed attestation of all its criteria.
			if (match && ["E_EVIDENCE_NOT_RUN", "E_EVIDENCE_UNAVAILABLE"].includes(item.code)) {
				const step = (collected.plan?.steps || []).find((entry) => entry.id === match[1]);
				if (step?.criteria.length && step.criteria.every((id) => attestedIds.has(id))) {
					reason("ready", "ATTESTED", `${step.id} did not run automatically; its criteria are covered by allowed attestations`, ref);
					continue;
				}
			}
			reason("unverified", item.code, item.message, ref);
		}
	}
	// Unfinished work.
	if (delivery.kind === "quick") {
		for (const id of collected.sourceIds.checks)
			if (!progress.checks.ticked.includes(id)) reason("incomplete", "UNFINISHED", `${id} is not ticked yet`, delivery.sources.find((s) => s.kind === "quick")?.markdown || null);
	} else if (!collected.plan) {
		reason("incomplete", "NO_PLAN", "the spec has no plan sidecar yet", null);
	} else {
		for (const step of progress.steps.filter((item) => !item.withdrawn && !item.done))
			reason("incomplete", "UNFINISHED", `Step ${step.number} is not complete (${step.mark === "~" ? "part-done" : "not started"})`, delivery.sources.find((s) => s.kind === "plan")?.markdown || null);
	}
	// Review, judged from the recorded verdict under the project's policy.
	if (review.evidence) {
		const blocking = (review.findings || []).filter((finding) => finding.blocking);
		if (review.state === "stale") reason(review.required ? "unverified" : "incomplete", "REVIEW_STALE", "the code or artifacts changed after the review", review.evidence);
		if (review.reason && !review.verdict) reason("unverified", "REVIEW_INVALID", review.reason, review.evidence);
		else if (blocking.length) reason("blocked", "REVIEW_BLOCKER", `review found ${blocking.length} blocking finding(s) (${policy.blocking_severities.join(", ")})`, review.evidence);
		else if (review.verdict === "request_changes") reason("incomplete", "REVIEW_CHANGES", "the review requested changes", review.evidence);
		else if (review.verdict === "approve" && review.findings === null && review.status === "failed")
			reason("blocked", "REVIEW_BLOCKER", "the review recorded blocking findings", review.evidence);
	} else if (review.required && !progress.finished) {
		reason("incomplete", "REVIEW_PENDING", "no review has been recorded yet", null);
	}
	if (delivery.kind === "planned" && !collected.spec) reason("unverified", "NO_SPEC", "no spec sidecar carries this delivery", null);
	// A selected preset's requirements, from the same resolved policy every caller uses.
	const assurance = collected.assurance?.mode === "preset" ? collected.assurance : null;
	if (assurance) {
		for (const item of assurance.conflicts) reason("blocked", item.code, `${item.message}. ${item.hint}`, "ai-factory/assurance.json");
		for (const item of assurance.unmet) reason("unverified", item.code, `${item.message}. ${item.hint}`, "ai-factory/assurance.json");
		// Only the headless review boundary records independence; self-review never satisfies it.
		if (assurance.settings.review_independence.value === "independent" && review.evidence && review.independence !== "independent")
			reason("unverified", "REVIEW_NOT_INDEPENDENT", `preset ${assurance.preset} requires an independent review, but this review's provenance is ${review.independence || "not recorded"}; record one with make review DELIVERY=${delivery.id}`, review.evidence);
	}
	for (const artifact of attestations)
		if (!policy.allow_attestation) reason("ready", "ATTESTATION_IGNORED", `${artifact.criterion} is attested by ${artifact.actor}, but policy does not allow attestation to count`, artifact.evidence);
	// Safety net: ready needs every criterion demonstrated.
	const unshown = criteria.filter((row) => !["passed", "attested", "pending", "failed", "uncovered"].includes(row.result));
	if (!reasons.some((item) => item.state !== "ready"))
		for (const row of unshown) reason("unverified", "NOT_DEMONSTRATED", `${row.id} has no current passing evidence`, null);
	if (!criteria.length) reason("unverified", "NO_CRITERIA", "no criteria are declared for this delivery", null);
	const status = worstState(reasons.map((item) => item.state));
	const counts = { criteria: criteria.length };
	for (const result of ["passed", "attested", "pending", "failed", "uncovered", "unverified"]) counts[result] = criteria.filter((row) => row.result === result).length;
	reasons.sort((a, b) => STATES.indexOf(b.state) - STATES.indexOf(a.state) || (a.code < b.code ? -1 : a.code > b.code ? 1 : 0) || String(a.evidence_ref).localeCompare(String(b.evidence_ref)));
	return { status, reasons, criteria, counts };
}

// --- model and rendering -------------------------------------------------------------------
function model(collected, generatedAt = new Date().toISOString()) {
	const readiness = evaluate(collected);
	const { delivery } = collected;
	const followUp = readiness.reasons.filter((item) => item.state !== "ready").map((item) => item.message);
	const title = delivery.tracker_key ? `${delivery.tracker_key}: ${delivery.title}` : delivery.title;
	const body = [
		"> Draft generated from local evidence by delivery-report. Review and edit before posting; nothing was published.",
		"",
		`Delivery \`${delivery.id}\` — status **${readiness.status}**.`,
		"",
		"Acceptance:",
		...readiness.criteria.map((row) => `- ${row.id}: ${row.result}`),
		"",
		"Verification run:",
		...(collected.verification.length ? collected.verification.filter((item) => item.phase !== "red").map((item) => `- ${item.commands.map((c) => `\`${c.argv.join(" ")}\``).join(", ")} — ${item.status}`) : ["- none recorded"]),
		"",
		`Review: ${collected.review.verdict || "not recorded"}`,
		...(collected.assurance?.mode === "preset" ? ["", `Assurance: preset ${collected.assurance.preset} (${collected.assurance.status})`] : []),
		...(followUp.length ? ["", "Open before handoff:", ...followUp.map((line) => `- ${line}`)] : []),
	].join("\n");
	return {
		schema: SCHEMA,
		version: VERSION,
		generated_at: generatedAt,
		note: NOTE,
		delivery,
		snapshot: { ...collected.snapshot, fingerprint: collected.fingerprint },
		status: readiness.status,
		reasons: readiness.reasons,
		counts: readiness.counts,
		policy: collected.policy,
		// Optional (schema report.v1): present only when a preset is selected.
		...(collected.assurance?.mode === "preset" ? { assurance: collected.assurance } : {}),
		criteria: readiness.criteria,
		steps: collected.progress.steps.map((step) => ({
			id: step.id,
			mark: step.mark || " ",
			withdrawn: step.withdrawn,
			criteria: (collected.plan?.steps || []).find((item) => item.id === step.id)?.criteria || [],
			evidence: collected.verification.find((item) => item.evidence.endsWith(`/${step.id}-step.json`))?.evidence || null,
			evidence_status: collected.verification.find((item) => item.evidence.endsWith(`/${step.id}-step.json`))?.status || null,
		})),
		contract_status: collected.contract.status,
		changes: collected.changes,
		verification: collected.verification,
		review: collected.review,
		attestations: collected.attestations,
		limitations: { ...collected.limitations, follow_up: followUp },
		telemetry: collected.telemetry,
		mr_draft: { draft: true, title, body },
	};
}
const renderJson = (report) => `${JSON.stringify(report, null, 2)}\n`;
function cell(value) {
	return String(value ?? "—").replace(/\\/g, "\\\\").replace(/\|/g, "\\|").replace(/`/g, "'").replace(/\r?\n/g, " ");
}
const code = (argv) => (argv && argv.length ? `\`${argv.join(" ").replace(/`/g, "'")}\`` : "—");
const value = (metric) => (metric === null || metric === undefined ? "unknown" : String(metric));
function renderMarkdown(report) {
	const out = [];
	const line = (text = "") => out.push(text);
	const d = report.delivery;
	line(`# Completion report — ${cell(d.title)}`);
	line();
	line(`**Status:** ${report.status}`);
	line(`**Delivery:** \`${d.id}\` (${d.kind})${d.tracker_key ? ` · tracker ${cell(d.tracker_key)}` : ""}`);
	line(`**Generated:** ${report.generated_at} · schema ${report.schema} v${report.version}`);
	line(`**Snapshot:** code \`${report.snapshot.code_sha256 || "unavailable"}\`${report.snapshot.head ? ` at \`${report.snapshot.head.slice(0, 12)}\`` : ""} · evidence fingerprint \`${report.snapshot.fingerprint}\``);
	line(`**Criteria:** ${report.counts.criteria} (passed ${report.counts.passed}, attested ${report.counts.attested}, pending ${report.counts.pending}, failed ${report.counts.failed}, uncovered ${report.counts.uncovered}, unverified ${report.counts.unverified})`);
	line();
	line(`> ${report.note}`);
	line();
	line("## Why this status");
	line();
	if (!report.reasons.length) line("Every criterion has current passing evidence and every policy requirement is met.");
	else {
		line("| State | Code | Reason | Evidence |");
		line("|---|---|---|---|");
		for (const item of report.reasons) line(`| ${item.state} | ${item.code} | ${cell(item.message)} | ${item.evidence_ref ? `\`${cell(item.evidence_ref)}\`` : "—"} |`);
	}
	if (report.assurance) {
		const a = report.assurance;
		const shown = (item) => (Array.isArray(item) ? item.join(", ") || "none" : item && typeof item === "object" ? JSON.stringify(item) : item);
		line();
		line("## Assurance");
		line();
		line(`Preset **${a.preset}** — ${a.status}. ${cell(a.note)}`);
		line();
		line("| Requirement | Value | Source |");
		line("|---|---|---|");
		for (const [key, item] of Object.entries(a.settings)) line(`| ${key} | ${cell(shown(item.value))} | ${cell(item.source)}${item.retained ? " (retained above preset)" : ""} |`);
		for (const item of [...a.conflicts, ...a.unmet]) {
			line();
			line(`- ${item.code}: ${cell(item.message)} — ${cell(item.hint)}`);
		}
	}
	line();
	line("## Sources");
	line();
	line("| Kind | Markdown | Sidecar | State | SHA-256 |");
	line("|---|---|---|---|---|");
	for (const source of d.sources) line(`| ${source.kind} | \`${cell(source.markdown)}\` | \`${cell(source.sidecar)}\` | ${source.state} | \`${source.sha256 || "unreadable"}\` |`);
	line();
	line("## Acceptance criteria");
	line();
	line("| Criterion | Steps | Checks | Result | Basis | Evidence |");
	line("|---|---|---|---|---|---|");
	for (const row of report.criteria)
		line(`| ${row.id} | ${row.steps.join(", ") || "—"} | ${row.tests.map(code).join("<br>") || "—"} | ${row.result} | ${row.basis} | ${[...row.evidence, ...(row.attestation ? [row.attestation] : [])].map((ref) => `\`${cell(ref)}\``).join("<br>") || "—"} |`);
	if (report.steps.length) {
		line();
		line("### Plan steps");
		line();
		line("| Step | Mark | Criteria | Evidence | Status |");
		line("|---|---|---|---|---|");
		for (const step of report.steps) line(`| ${step.id}${step.withdrawn ? " (withdrawn)" : ""} | [${step.mark}] | ${step.criteria.join(", ") || "—"} | ${step.evidence ? `\`${cell(step.evidence)}\`` : "—"} | ${step.evidence_status || "none"} |`);
	}
	line();
	line("## Changes and verification");
	line();
	const c = report.changes;
	line(c.base ? `Compared with \`${c.base.branch}\` at \`${c.base.commit.slice(0, 12)}\`, plus untracked files: ${c.total} path(s) — ${c.counts.added} added, ${c.counts.modified} modified, ${c.counts.deleted} deleted, ${c.counts.untracked} untracked.` : `No base branch found; untracked files only: ${c.total} path(s).`);
	if (c.paths.length) {
		line();
		for (const item of c.paths) line(`- \`${item.status}\` ${cell(item.path)}`);
		if (c.truncated) line(`- … ${c.total - c.paths.length} more (listed in completion.json only up to ${c.paths.length})`);
	}
	line();
	line("| Evidence | Phase | Commands | Status | State | Output |");
	line("|---|---|---|---|---|---|");
	if (!report.verification.length) line("| — | — | no verification recorded | — | — | — |");
	for (const item of report.verification)
		line(`| \`${cell(item.evidence)}\` | ${item.phase} (expects ${item.expected}) | ${item.commands.map((c2) => `${code(c2.argv)} → ${c2.status}${c2.exit_code !== null && c2.exit_code !== undefined ? ` (exit ${c2.exit_code})` : ""}${c2.signal ? ` (${c2.signal})` : ""}`).join("<br>") || "—"} | ${item.status} | ${item.state || "—"} | ${item.output_ref ? `\`${cell(item.output_ref)}\`` : "—"} |`);
	line();
	line("## Review");
	line();
	const r = report.review;
	if (!r.evidence) line(`No review recorded${r.required ? " (required by policy)" : ""}.`);
	else {
		line(`Verdict **${r.verdict || "invalid"}** (${r.status}, ${r.state}) by ${cell(r.tool || "unknown")} — \`${cell(r.evidence)}\`${r.reason ? ` — ${cell(r.reason)}` : ""}.`);
		if (report.assurance) line(`Review independence: ${cell(r.independence || "not recorded")}.`);
		if (r.findings === null) line("Findings were not recorded with this review.");
		else if (!r.findings.length) line("No findings.");
		else {
			line();
			line("| Severity | Blocking | Location | Issue |");
			line("|---|---|---|---|");
			for (const f of r.findings) line(`| ${cell(f.severity)} | ${f.blocking ? "yes" : "no"} | ${cell(f.file)}:${cell(f.line)} | ${cell(f.issue)} |`);
		}
	}
	if (report.attestations.length) {
		line();
		line("### Attestations");
		line();
		line(`Policy ${report.policy.allow_attestation ? "allows" : "does not allow"} attestations to count toward completion.`);
		line();
		line("| Criterion | Actor | When | Rationale | Source | State |");
		line("|---|---|---|---|---|---|");
		for (const a of report.attestations) line(`| ${a.criterion} | ${cell(a.actor)} | ${cell(a.attested_at)} | ${cell(a.rationale)} | ${cell(a.source)} | ${a.state} |`);
	}
	line();
	line("## Limitations and follow-up");
	line();
	if (report.limitations.follow_up.length) for (const item of report.limitations.follow_up) line(`- ${cell(item)}`);
	else line("- Nothing outstanding from the evidence.");
	for (const [label, text] of [["Open questions (spec)", report.limitations.open_questions], ["Risks (plan)", report.limitations.risks]])
		if (text) {
			line();
			line(`### ${label}`);
			line();
			line(text);
		}
	line();
	line("## Telemetry");
	line();
	const t = report.telemetry;
	line(`Status: **${t.status}**${t.reason ? ` — ${cell(t.reason)}` : ""}. Unknown values are shown as unknown, never as zero.`);
	if (t.metrics) {
		line();
		line(`Coverage: ${t.coverage.measured} of ${t.coverage.total} runs measured${t.pricing ? ` · pricing: ${cell(t.pricing.source)}` : ""}.`);
		line();
		for (const [key, metric] of Object.entries(t.metrics).sort(([a], [b]) => (a < b ? -1 : 1))) line(`- ${cell(key)}: ${value(metric)}`);
	}
	line();
	line("## Suggested MR description (draft)");
	line();
	line(`**Title:** ${cell(report.mr_draft.title)}`);
	line();
	line("````markdown");
	line(report.mr_draft.body);
	line("````");
	return `${out.join("\n")}\n`;
}

// --- saving --------------------------------------------------------------------------------
// Both files are staged first, then renamed; a failure leaves the previous report in place.
function save(ctx, report) {
	const directory = path.join(ctx.workspace, "reports", report.delivery.id);
	ctx.safe.mkdir(directory);
	const staged = [];
	try {
		for (const [name, content] of [["completion.json", renderJson(report)], ["completion.md", renderMarkdown(report)]]) {
			const target = path.join(directory, name);
			const temporary = path.join(directory, `.${name}.${process.pid}.${crypto.randomBytes(4).toString("hex")}.tmp`);
			ctx.safe.write(temporary, content, { exclusive: true });
			ctx.safe.assertPath(target);
			staged.push([temporary, target]);
		}
		for (const [temporary, target] of staged) fs.renameSync(temporary, target);
	} catch (error) {
		for (const [temporary] of staged) ctx.safe.unlink(temporary);
		throw new Invocation(`delivery-report: could not write the report: ${error.message}`);
	}
	return staged.map(([, target]) => target);
}
function generate(options = {}) {
	const ctx = options.ctx || contracts.context(options.root || ROOT);
	ctx.config = ctx.config || contracts.readConfig(ctx);
	const collected = collect({ ...options, ctx });
	const report = model(collected, options.generatedAt);
	const files = options.write === false ? [] : save(ctx, report);
	return { report, files: files.map((file) => ctx.rel(file)) };
}
// May completion be claimed? Readiness is evaluated now from current evidence and never read
// from a saved report, so a cached report authorizes nothing and generating a report never needs
// a previous one. The effective completion requirement decides what else is needed:
//   summary             only a normal task summary (light, or no contracts, without a preset)
//   recorded_checks     the report evaluated now must be ready; saving it is optional (nothing written)
//   fresh_ready_report  the report is generated and saved now, and must be ready (strict)
function completion(options = {}) {
	const env = options.env || process.env;
	const ctx = options.ctx || contracts.context(options.root || ROOT);
	ctx.config = ctx.config || contracts.readConfig(ctx);
	const collected = collect({ ...options, ctx, env });
	const report = model(collected, options.generatedAt);
	const policy = collected.assurance;
	const requirement = policy ? policy.settings.completion.value : ctx.config.adopted ? "recorded_checks" : "summary";
	const files = requirement === "fresh_ready_report" ? save(ctx, report).map((file) => ctx.rel(file)) : [];
	const claimable = policy?.status !== "conflict" && (requirement === "summary" || report.status === "ready");
	return {
		schema: "t4-assurance-completion",
		version: VERSION,
		delivery: report.delivery.id,
		mode: policy ? policy.mode : "legacy",
		preset: policy ? policy.preset : null,
		status: policy ? policy.status : "legacy",
		requirement,
		report_status: report.status,
		claimable,
		written: files,
		open: report.reasons.filter((item) => item.state !== "ready"),
		note:
			requirement === "fresh_ready_report"
				? "Strict completion needs this freshly generated report to be ready; an earlier saved report never counts."
				: requirement === "recorded_checks"
					? "Required checks and review are judged from current evidence; a saved report is optional and none was read or written."
					: "This policy requires only a normal task summary; project rules still apply.",
	};
}
function main(argv = process.argv.slice(2), env = process.env) {
	const out = (text) => process.stdout.write(`${text}\n`);
	const err = (text) => process.stderr.write(`${text}\n`);
	try {
		const args = [...argv];
		const json = args.includes("--json") || Boolean(env.JSON);
		const positional = args.filter((arg) => arg !== "--json");
		if (positional.some((arg) => arg.startsWith("--")) || positional.length > 1) throw new Invocation("usage: delivery-report.js <delivery id> [--json]  (or DELIVERY=… in make)");
		const delivery = positional[0] || env.DELIVERY;
		if (!delivery) throw new Invocation("delivery-report: name a delivery (DELIVERY=d-YYYYMMDD-xxxxxx); make contracts lists them");
		const { report, files } = generate({ delivery, env });
		if (json) out(renderJson(report).trimEnd());
		else {
			out(`delivery-report: ${report.status} — ${report.delivery.id} (${report.delivery.kind})`);
			for (const item of report.reasons) out(`  [${item.state}] ${item.code}: ${item.message}${item.evidence_ref ? ` — ${item.evidence_ref}` : ""}`);
			out(`  criteria: ${report.counts.criteria} (passed ${report.counts.passed}, attested ${report.counts.attested}, pending ${report.counts.pending}, failed ${report.counts.failed}, uncovered ${report.counts.uncovered}, unverified ${report.counts.unverified})`);
			out(`  telemetry: ${report.telemetry.status}`);
			for (const file of files) out(`  wrote ${file}`);
			out("  nothing was published; the MR description in the report is a draft");
		}
		return report.status === "ready" ? 0 : 1;
	} catch (error) {
		err(error instanceof Invocation ? error.message : `delivery-report: ${error.message}`);
		return 2;
	}
}
if (require.main === module) process.exitCode = main();
module.exports = { SCHEMA, VERSION, STATES, collect, evaluate, model, renderJson, renderMarkdown, save, generate, completion, main };
