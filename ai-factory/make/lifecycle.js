#!/usr/bin/env node
// Delivery lifecycle telemetry: explicit run/phase/wait boundaries plus usage linked from the
// existing accounting, aggregated with interval arithmetic. Opt-in through
// ai-factory/contracts/config.json `lifecycle.enabled`. The run log (log.csv) and `make cost`
// are untouched; this is a separate interface. A missing measurement is null, never zero, and a
// telemetry failure never fails the work it describes.
const fs = require("node:fs");
const path = require("node:path");
const crypto = require("node:crypto");
const { boundary } = require("./safe-files.js");
const store = require("./lifecycle-events.js")({ boundary });
// Preserve the invoked workspace when its entry point is a source-repo symlink (see runner.js).
const ENTRY_DIRECTORY =
	require.main === module ? path.dirname(process.argv[1]) : __dirname;
const ROOT = fs.realpathSync(path.resolve(ENTRY_DIRECTORY, "../.."));
const REPORT = { schema: "t4-lifecycle-report", version: 1 };
const TELEMETRY = { schema: "t4-delivery-telemetry", version: 1 };
// What no host signal can tell us; reported with every aggregate so coverage is never implied.
const GAPS = [
	"Waiting is only what `lifecycle.js wait-start`/`wait-end` recorded; idle gaps are never guessed as waits.",
	"Interactive usage is attributed only through an explicit session binding (the hook sees `lifecycle.js start`) or an inherited T4_LIFECYCLE_RUN; anything else stays unattributed.",
	"A run whose process crashed stays open until `lifecycle.js end` records its outcome; no end time is invented.",
];
class Invocation extends Error {}
const round = (value) => (value === null || value === undefined ? null : Math.round(value * 1000) / 1000);

// --- interval arithmetic -------------------------------------------------------------------
function union(intervals) {
	const sorted = intervals.filter(([a, b]) => b >= a).sort((x, y) => x[0] - y[0]);
	const merged = [];
	for (const [start, end] of sorted) {
		const last = merged[merged.length - 1];
		if (last && start <= last[1]) last[1] = Math.max(last[1], end);
		else merged.push([start, end]);
	}
	return merged;
}
const total = (intervals) => union(intervals).reduce((sum, [a, b]) => sum + (b - a), 0);
function overlap(a, b) {
	let sum = 0;
	for (const [s1, e1] of union(a)) for (const [s2, e2] of union(b)) sum += Math.max(0, Math.min(e1, e2) - Math.max(s1, s2));
	return sum;
}
const clipTo = (intervals, windows) => union(intervals).flatMap(([s, e]) => union(windows).map(([ws, we]) => [Math.max(s, ws), Math.min(e, we)]).filter(([a, b]) => b > a));
// Same process: the monotonic clock. Otherwise wall clocks, flagged. Backwards is unknown.
function timing(start, end) {
	if (!start) return { start_ms: null, end_ms: end ? Date.parse(end.at) : null, duration_s: null, quality: "missing_start" };
	const startMs = Date.parse(start.at);
	if (!end) return { start_ms: startMs, end_ms: null, duration_s: null, quality: "open" };
	const monotonic = start.clock && end.clock && start.clock.process === end.clock.process;
	const ms = monotonic ? end.clock.mono_ms - start.clock.mono_ms : Date.parse(end.at) - startMs;
	if (!(ms >= 0)) return { start_ms: startMs, end_ms: null, duration_s: null, quality: "invalid_clock" };
	return { start_ms: startMs, end_ms: startMs + ms, duration_s: round(ms / 1000), quality: monotonic ? "monotonic" : "wall_clock" };
}

// --- aggregation ---------------------------------------------------------------------------
function workspace(root = ROOT) {
	return path.join(root, "ai-factory");
}
function prunedMarkers(ai) {
	const file = path.join(ai, "runs", "lifecycle", "pruned.json");
	try {
		boundary(ai).assertPath(file, { allowMissing: false });
		const value = JSON.parse(fs.readFileSync(file, "utf8"));
		return Array.isArray(value.prunes) ? value.prunes : [];
	} catch {
		return [];
	}
}
function aggregate(options = {}) {
	const ai = options.ai || workspace(options.root);
	const now = options.now ?? Date.now();
	const read = store.read(ai);
	const ordered = read.events.slice().sort((a, b) => Date.parse(a.at) - Date.parse(b.at) || (a.event_id < b.event_id ? -1 : 1));
	const runs = new Map();
	const anomalies = [];
	const run = (id) => {
		if (!runs.has(id)) runs.set(id, { run_id: id, started: null, ended: null, attempts: new Map(), waits: new Map(), bindings: [] });
		return runs.get(id);
	};
	const usages = [];
	const bindings = [];
	for (const event of ordered) {
		if (event.type === "usage_linked") {
			usages.push(event);
			continue;
		}
		if (event.type === "session_bound") {
			bindings.push(event);
			continue;
		}
		const r = run(event.run_id);
		if (event.type === "run_started") {
			if (r.started) anomalies.push({ run_id: r.run_id, issue: "run started twice; the first start is kept" });
			else r.started = event;
		} else if (event.type === "run_ended") {
			if (r.ended) anomalies.push({ run_id: r.run_id, issue: "run ended twice; the first end is kept" });
			else r.ended = event;
		} else if (event.type === "phase_started" || event.type === "phase_ended") {
			if (!r.attempts.has(event.attempt_id)) r.attempts.set(event.attempt_id, { attempt_id: event.attempt_id, started: null, ended: null });
			const attempt = r.attempts.get(event.attempt_id);
			const key = event.type === "phase_started" ? "started" : "ended";
			if (!attempt[key]) attempt[key] = event;
		} else if (event.type === "wait_started" || event.type === "wait_ended") {
			if (!r.waits.has(event.wait_id)) r.waits.set(event.wait_id, { wait_id: event.wait_id, started: null, ended: null });
			const wait = r.waits.get(event.wait_id);
			const key = event.type === "wait_started" ? "started" : "ended";
			if (!wait[key]) wait[key] = event;
		}
	}
	const summaries = new Map();
	for (const r of runs.values()) {
		const t = timing(r.started, r.ended);
		const start = r.started || {};
		const attempts = [...r.attempts.values()].map((a) => ({ ...a, timing: timing(a.started, a.ended), phase: a.started?.phase || start.phase || null, step: a.started?.step || start.step || null, outcome: a.ended?.outcome || null }));
		const waits = [...r.waits.values()].map((w) => ({ ...w, timing: timing(w.started, w.ended) }));
		summaries.set(r.run_id, {
			run_id: r.run_id,
			delivery_id: start.delivery_id || r.ended?.delivery_id || null,
			parent_run_id: start.parent_run_id || null,
			host: start.host || null,
			task: start.task || null,
			phase: start.phase || null,
			step: start.step || null,
			session_id: start.session_id || null,
			started_at: r.started?.at || null,
			ended_at: r.ended?.at || null,
			outcome: r.ended?.outcome || null,
			status: t.quality === "open" ? "open" : t.quality === "missing_start" ? "missing_start" : t.quality === "invalid_clock" ? "invalid_clock" : "closed",
			elapsed_s: t.duration_s,
			open_age_s: t.quality === "open" && t.start_ms !== null ? round(Math.max(0, now - t.start_ms) / 1000) : null,
			timing_quality: t.quality,
			window: t.start_ms === null ? null : [t.start_ms, t.end_ms],
			attempts,
			waits,
		});
	}
	// An explicitly delegated child belongs to its parent's delivery; nothing else is inferred.
	const deliveryOf = (summary, seen = new Set()) => {
		if (summary.delivery_id) return summary.delivery_id;
		if (!summary.parent_run_id || seen.has(summary.run_id)) return null;
		seen.add(summary.run_id);
		const parent = summaries.get(summary.parent_run_id);
		return parent ? deliveryOf(parent, seen) : null;
	};
	for (const summary of summaries.values()) summary.delivery = deliveryOf(summary);
	// Session bindings: runs that declared their session, plus hook-observed `lifecycle.js start`.
	const bound = new Map();
	const bind = (session, runId) => {
		if (!session || !summaries.has(runId)) return;
		if (!bound.has(session)) bound.set(session, new Set());
		bound.get(session).add(runId);
	};
	for (const summary of summaries.values()) bind(summary.session_id, summary.run_id);
	for (const binding of bindings) bind(binding.session_id, binding.run_id);
	// Usage: unique source records counted once; a headless envelope also seen by hooks drops out.
	const seenKeys = new Set();
	const hookSessions = new Set(usages.filter((u) => u.usage.source !== "headless").map((u) => u.session_id).filter(Boolean));
	const linked = [];
	let duplicates = 0;
	let superseded = 0;
	for (const usage of usages) {
		if (seenKeys.has(usage.usage.record_key)) {
			duplicates++;
			continue;
		}
		seenKeys.add(usage.usage.record_key);
		if (usage.usage.source === "headless" && usage.usage.child_session && hookSessions.has(usage.usage.child_session)) {
			superseded++;
			continue;
		}
		const at = Date.parse(usage.at);
		const first = Date.parse(usage.usage.first_at ?? usage.at);
		const last = Date.parse(usage.usage.last_at ?? usage.at);
		let owner = null;
		let method = null;
		let reason = null;
		if (usage.run_id) {
			if (summaries.has(usage.run_id)) {
				owner = usage.run_id;
				method = "explicit";
			} else reason = "names a run with no recorded events";
		} else {
			const candidates = [...(bound.get(usage.session_id) || [])].filter((id) => {
				const window = summaries.get(id).window;
				if (!window) return false;
				const end = window[1] === null ? Number.POSITIVE_INFINITY : window[1];
				return (Number.isNaN(first) ? at : first) <= end && (Number.isNaN(last) ? at : last) >= window[0];
			});
			if (candidates.length === 1) {
				owner = candidates[0];
				method = "session_binding";
			} else reason = candidates.length ? `ambiguous: ${candidates.length} runs bound to this session overlap it` : usage.session_id && bound.has(usage.session_id) ? "outside every bound run's window" : "no run is bound to this session";
		}
		linked.push({ usage, owner, method, reason });
	}
	const deliveries = new Map();
	const deliveryEntry = (id) => {
		if (!deliveries.has(id)) deliveries.set(id, { runs: [], usage: [] });
		return deliveries.get(id);
	};
	for (const summary of summaries.values()) if (summary.delivery) deliveryEntry(summary.delivery).runs.push(summary);
	const unattributed = { runs: [], usage: [] };
	for (const summary of summaries.values()) if (!summary.delivery) unattributed.runs.push(summary);
	for (const item of linked) {
		const summary = item.owner && summaries.get(item.owner);
		if (summary?.delivery) deliveryEntry(summary.delivery).usage.push(item);
		else unattributed.usage.push({ ...item, reason: item.reason || "its run belongs to no delivery" });
	}
	const pruned = prunedMarkers(ai);
	const wanted = options.delivery ? [options.delivery] : [...deliveries.keys()].sort();
	const report = {
		...REPORT,
		generated_at: new Date(now).toISOString(),
		events: { valid: read.events.length, corrupt: read.corrupt, unsupported: read.unsupported },
		anomalies,
		deliveries: wanted.map((id) => deliveryMetrics(id, deliveries.get(id) || { runs: [], usage: [] }, pruned)),
		unattributed: {
			runs: unattributed.runs.map(publicRun),
			usage: tokenTotals(unattributed.usage.map((item) => item.usage)),
			reasons: countBy(unattributed.usage.map((item) => item.reason)),
		},
		deduplicated: { replayed_records: duplicates, headless_seen_by_hooks: superseded },
		coverage_gaps: GAPS,
	};
	return report;
}
function countBy(values) {
	const out = {};
	for (const value of values) out[value] = (out[value] || 0) + 1;
	return out;
}
function tokenTotals(usages) {
	if (!usages.length) return { records: 0, turns: null, input_tokens: null, output_tokens: null, cache_read_tokens: null, cache_write_tokens: null };
	const sum = (key) => usages.reduce((acc, usage) => acc + usage.usage[key], 0);
	return { records: usages.length, turns: sum("turns"), input_tokens: sum("input_tokens"), output_tokens: sum("output_tokens"), cache_read_tokens: sum("cache_read_tokens"), cache_write_tokens: sum("cache_write_tokens") };
}
function publicRun(summary) {
	const { window, attempts, waits, delivery, ...rest } = summary;
	return { ...rest, delivery_id: delivery || rest.delivery_id, attempts: attempts.length, waits: waits.length };
}
function deliveryMetrics(id, entry, pruned) {
	const quality = [];
	const runs = entry.runs.slice().sort((a, b) => (a.started_at || "") < (b.started_at || "") ? -1 : 1);
	const closedWindows = runs.filter((r) => r.status === "closed").map((r) => r.window);
	for (const status of ["open", "invalid_clock", "missing_start"])
		if (runs.some((r) => r.status === status)) quality.push(status === "open" ? "open_runs" : status);
	const complete = runs.length > 0 && runs.every((r) => r.status === "closed");
	const elapsedKnown = total(closedWindows) / 1000;
	// Waits: explicit intervals only, clipped to the measured run windows.
	const waitIntervals = [];
	let openWaits = false;
	for (const r of runs)
		for (const w of r.waits) {
			if (w.timing.quality === "open" || w.timing.quality === "missing_start" || w.timing.quality === "invalid_clock") openWaits = true;
			else waitIntervals.push([w.timing.start_ms, w.timing.end_ms]);
		}
	if (openWaits) quality.push("open_or_invalid_waits");
	const clippedWaits = clipTo(waitIntervals, closedWindows.length ? closedWindows : waitIntervals);
	const waitingKnown = total(clippedWaits) / 1000;
	// Active: union of recorded execution intervals minus recorded waits.
	const attemptIntervals = [];
	let incompleteAttempts = false;
	for (const r of runs)
		for (const a of r.attempts) {
			if (a.timing.quality === "monotonic" || a.timing.quality === "wall_clock") attemptIntervals.push([a.timing.start_ms, a.timing.end_ms]);
			else incompleteAttempts = true;
		}
	if (incompleteAttempts) quality.push("incomplete_attempts");
	const active = !incompleteAttempts && attemptIntervals.length && !openWaits ? (total(attemptIntervals) - overlap(attemptIntervals, clippedWaits)) / 1000 : null;
	// Retries: another attempt of the same phase/step after a failure. After an interruption or
	// an attempt left open it is a resumption, and after a success a rerun — neither is a retry.
	const groups = new Map();
	for (const r of runs)
		for (const a of r.attempts) {
			const key = `${a.phase || r.phase || r.task || "?"}\0${a.step || ""}`;
			if (!groups.has(key)) groups.set(key, []);
			groups.get(key).push({ ...a, start: a.timing.start_ms ?? Number.POSITIVE_INFINITY });
		}
	let retries = 0;
	let resumed = 0;
	let reruns = 0;
	for (const list of groups.values()) {
		list.sort((a, b) => a.start - b.start);
		for (let index = 1; index < list.length; index++) {
			const previous = list[index - 1].outcome;
			if (previous === "failed") retries++;
			else if (previous === "succeeded") reruns++;
			else resumed++;
		}
	}
	// Agent effort: measured agent execution windows, allowed to exceed elapsed time.
	const agentWindows = entry.usage.filter((item) => item.usage.usage.source === "agent" && item.usage.usage.first_at && item.usage.usage.last_at);
	const agentEffort = agentWindows.length ? agentWindows.reduce((sum, item) => sum + Math.max(0, Date.parse(item.usage.usage.last_at) - Date.parse(item.usage.usage.first_at)), 0) / 1000 : null;
	const usages = entry.usage.map((item) => item.usage);
	const tokens = tokenTotals(usages);
	// Money: a known subtotal plus the unknown remainder. Zero-priced is known; unpriced is unknown.
	const priced = usages.filter((u) => u.usage.cost_usd !== null);
	const cost = {
		known_usd: priced.length ? round(priced.reduce((sum, u) => sum + u.usage.cost_usd, 0)) : null,
		total_usd: usages.length && priced.length === usages.length ? round(priced.reduce((sum, u) => sum + u.usage.cost_usd, 0)) : null,
		priced_records: priced.length,
		zero_priced_records: priced.filter((u) => u.usage.cost_usd === 0).length,
		unknown_records: usages.length - priced.length,
		provenance: countBy(usages.map((u) => u.usage.cost_provenance || "unknown")),
	};
	if (pruned.some((marker) => (marker.deliveries || []).includes(id))) quality.push("detail_pruned");
	const outcomes = countBy(runs.map((r) => r.outcome || r.status));
	return {
		delivery_id: id,
		runs: runs.map(publicRun),
		outcomes,
		metrics: {
			elapsed_s: complete ? round(elapsedKnown) : null,
			elapsed_known_s: runs.length ? round(elapsedKnown) : null,
			open_run_age_s: runs.some((r) => r.status === "open") ? Math.max(...runs.filter((r) => r.status === "open").map((r) => r.open_age_s ?? 0)) : null,
			waiting_s: runs.length && !openWaits ? round(waitingKnown) : null,
			active_s: round(active),
			agent_effort_s: round(agentEffort),
			retries: runs.length ? retries : null,
			resumed: runs.length ? resumed : null,
			reruns: runs.length ? reruns : null,
		},
		tokens,
		cost,
		attribution: countBy(entry.usage.map((item) => item.method)),
		coverage: { runs: runs.length, measured_runs: runs.filter((r) => r.status === "closed").length },
		quality,
	};
}
// The provisional per-delivery export that make/delivery-report.js reads (schema telemetry.v1).
function telemetryFor(options) {
	const report = aggregate(options);
	const d = report.deliveries[0];
	const metric = (value) => (typeof value === "number" && Number.isFinite(value) ? value : null);
	return {
		...TELEMETRY,
		delivery_id: d.delivery_id,
		coverage: { measured: d.coverage.measured_runs, total: d.coverage.runs },
		metrics: {
			elapsed_s: metric(d.metrics.elapsed_s),
			active_s: metric(d.metrics.active_s),
			waiting_s: metric(d.metrics.waiting_s),
			agent_effort_s: metric(d.metrics.agent_effort_s),
			runs: d.coverage.runs ? d.coverage.runs : null,
			retries: metric(d.metrics.retries),
			input_tokens: metric(d.tokens.input_tokens),
			output_tokens: metric(d.tokens.output_tokens),
			cache_read_tokens: metric(d.tokens.cache_read_tokens),
			cache_write_tokens: metric(d.tokens.cache_write_tokens),
			cost_usd: metric(d.cost.total_usd),
			cost_usd_known: metric(d.cost.known_usd),
			cost_unknown_records: d.tokens.records ? d.cost.unknown_records : null,
			unattributed_records: report.unattributed.usage.records,
		},
		pricing: { source: Object.keys(d.cost.provenance).sort().join(", ") || "no priced usage" },
		quality: d.quality,
	};
}

// --- recording -----------------------------------------------------------------------------
function requireEnabled(ai) {
	const config = store.config(ai);
	if (!config.enabled) throw new Invocation("lifecycle: not enabled — set \"lifecycle\": { \"enabled\": true } in ai-factory/contracts/config.json");
	return config;
}
const DELIVERY = /^d-\d{8}-[0-9a-f]{6}$/;
function start(options) {
	const ai = options.ai || workspace(options.root);
	requireEnabled(ai);
	const parent = options.parentRun || (store.PATTERNS.run_id.test(process.env.T4_LIFECYCLE_RUN || "") ? process.env.T4_LIFECYCLE_RUN : null);
	const runId = store.newId("r");
	const attemptId = store.newId("a");
	const common = { host: options.host || "cli", run_id: runId, delivery_id: options.delivery || null, parent_run_id: parent, phase: options.phase, step: options.step || null, task: options.task || null, session_id: options.session || null };
	store.emit(ai, { ...common, type: "run_started", tool: options.tool || null, model: options.model || null });
	store.emit(ai, { ...common, type: "phase_started", attempt_id: attemptId });
	return { run_id: runId, attempt_id: attemptId };
}
function end(options) {
	const ai = options.ai || workspace(options.root);
	requireEnabled(ai);
	const events = store.read(ai).events.filter((event) => event.run_id === options.run);
	const started = events.find((event) => event.type === "run_started");
	if (!started) throw new Invocation(`lifecycle: no run_started event for ${options.run}`);
	if (events.some((event) => event.type === "run_ended")) return { already: true };
	// A run may learn its delivery only at the end (/t4:spec creates the ID); that is still explicit.
	const common = { host: options.host || started.host, run_id: options.run, delivery_id: started.delivery_id || options.delivery || null, phase: started.phase, step: started.step, outcome: options.outcome, reason: options.reason ? String(options.reason).slice(0, 200) : null };
	const open = new Set(events.filter((e) => e.type === "phase_started").map((e) => e.attempt_id));
	for (const e of events) if (e.type === "phase_ended") open.delete(e.attempt_id);
	for (const attempt of open) store.emit(ai, { ...common, type: "phase_ended", attempt_id: attempt });
	store.emit(ai, { ...common, type: "run_ended" });
	return { already: false };
}
function wait(options, kind) {
	const ai = options.ai || workspace(options.root);
	requireEnabled(ai);
	const waitId = kind === "start" ? store.newId("w") : options.wait;
	store.emit(ai, { host: options.host || "cli", type: kind === "start" ? "wait_started" : "wait_ended", run_id: options.run, wait_id: waitId, reason: options.reason ? String(options.reason).slice(0, 200) : null });
	return { wait_id: waitId };
}
function writeAtomic(ai, file, content) {
	const safe = boundary(ai);
	safe.mkdir(path.dirname(file));
	safe.assertPath(file);
	const staged = path.join(path.dirname(file), `.${path.basename(file)}.${process.pid}.${crypto.randomBytes(4).toString("hex")}.tmp`);
	safe.write(staged, content, { exclusive: true });
	try {
		fs.renameSync(staged, file);
	} catch (error) {
		safe.unlink(staged);
		throw error;
	}
}
// Retention: old event detail goes, except for deliveries that have a saved completion report;
// what was removed is recorded so later aggregates say detail was pruned instead of reading low.
function prune(options) {
	const ai = options.ai || workspace(options.root);
	const config = store.config(ai);
	const days = options.days || config.retention_days || 90;
	const cutoff = (options.now ?? Date.now()) - days * 86400000;
	const read = store.read(ai);
	const runDelivery = new Map();
	for (const event of read.events) if (event.type === "run_started") runDelivery.set(event.run_id, event.delivery_id);
	const kept = new Set();
	const reports = path.join(ai, "reports");
	if (fs.existsSync(reports)) for (const name of fs.readdirSync(reports)) if (DELIVERY.test(name)) kept.add(name);
	const doomed = read.events.filter((event) => Date.parse(event.at) < cutoff && !kept.has(event.delivery_id || runDelivery.get(event.run_id)));
	const deliveries = [...new Set(doomed.map((event) => event.delivery_id || runDelivery.get(event.run_id)).filter(Boolean))].sort();
	if (options.write && doomed.length) {
		const safe = boundary(ai);
		for (const event of doomed) safe.unlink(path.join(store.directory(ai), `${event.event_id}.json`));
		const markers = prunedMarkers(ai);
		markers.push({ pruned_at: new Date(options.now ?? Date.now()).toISOString(), before: new Date(cutoff).toISOString(), events: doomed.length, deliveries });
		writeAtomic(ai, path.join(ai, "runs", "lifecycle", "pruned.json"), `${JSON.stringify({ prunes: markers }, null, 2)}\n`);
	}
	return { days, events: doomed.length, deliveries, kept: [...kept].sort(), write: Boolean(options.write) };
}

// --- rendering -----------------------------------------------------------------------------
const show = (value, unit = "") => (value === null || value === undefined ? "unknown" : `${value}${unit}`);
function human(report) {
	const lines = [`lifecycle: ${report.deliveries.length} deliveries · ${report.events.valid} events`];
	if (report.events.corrupt.length) lines.push(`  corrupt events: ${report.events.corrupt.length} (${report.events.corrupt.map((e) => `${e.path}: ${e.reason}`).join("; ")})`);
	if (report.events.unsupported.length) lines.push(`  unsupported events: ${report.events.unsupported.length} (${report.events.unsupported.map((e) => `${e.path}: ${e.reason}`).join("; ")})`);
	for (const d of report.deliveries) {
		const m = d.metrics;
		lines.push("");
		lines.push(`${d.delivery_id} — ${d.coverage.runs} run(s), ${d.coverage.measured_runs} fully measured${d.quality.length ? ` · quality: ${d.quality.join(", ")}` : ""}`);
		lines.push(`  elapsed ${show(m.elapsed_s, "s")} (known ${show(m.elapsed_known_s, "s")}${m.open_run_age_s !== null ? `, open run age ${m.open_run_age_s}s` : ""}) · active ${show(m.active_s, "s")} · waiting ${show(m.waiting_s, "s")} · agent effort ${show(m.agent_effort_s, "s")} (may exceed elapsed)`);
		lines.push(`  retries ${show(m.retries)} · resumed ${show(m.resumed)} · reruns ${show(m.reruns)} · outcomes ${Object.entries(d.outcomes).map(([k, v]) => `${k} ${v}`).join(", ") || "none"}`);
		lines.push(`  tokens: ${d.tokens.records} record(s) — input ${show(d.tokens.input_tokens)}, output ${show(d.tokens.output_tokens)}, cache read ${show(d.tokens.cache_read_tokens)}, cache write ${show(d.tokens.cache_write_tokens)}`);
		lines.push(`  cost: known ${show(d.cost.known_usd, " USD")}, total ${show(d.cost.total_usd, " USD")} · ${d.cost.unknown_records} record(s) unpriced, ${d.cost.zero_priced_records} zero-priced · provenance ${Object.entries(d.cost.provenance).map(([k, v]) => `${k} ${v}`).join(", ") || "none"}`);
	}
	const u = report.unattributed;
	lines.push("");
	lines.push(`unattributed: ${u.runs.length} run(s) without a delivery · ${u.usage.records} usage record(s)${Object.keys(u.reasons).length ? ` (${Object.entries(u.reasons).map(([k, v]) => `${k}: ${v}`).join("; ")})` : ""}`);
	lines.push(`deduplicated: ${report.deduplicated.replayed_records} replayed record(s), ${report.deduplicated.headless_seen_by_hooks} headless envelope(s) also seen by hooks`);
	for (const gap of report.coverage_gaps) lines.push(`gap: ${gap}`);
	return lines.join("\n");
}

// --- command line --------------------------------------------------------------------------
function parse(args) {
	const options = {};
	const names = { "--delivery": "delivery", "--phase": "phase", "--step": "step", "--task": "task", "--parent-run": "parentRun", "--session": "session", "--run": "run", "--outcome": "outcome", "--reason": "reason", "--wait": "wait", "--days": "days" };
	for (let index = 0; index < args.length; index++) {
		const arg = args[index];
		if (arg === "--json" || arg === "--write") {
			options[arg.slice(2)] = true;
			continue;
		}
		const key = names[arg];
		if (!key || Object.hasOwn(options, key) || !args[index + 1]) throw new Invocation(`lifecycle: invalid argument ${arg}`);
		options[key] = args[++index];
	}
	const check = (key, pattern, label) => {
		if (options[key] !== undefined && !pattern.test(options[key])) throw new Invocation(`lifecycle: ${label}`);
	};
	check("delivery", DELIVERY, "--delivery must be d-YYYYMMDD-xxxxxx");
	check("phase", store.PATTERNS.phase, "--phase must be a lowercase task or phase name");
	check("task", store.PATTERNS.task, "--task must be a lowercase task name");
	check("step", store.PATTERNS.step, "--step must look like S1");
	check("run", store.PATTERNS.run_id, "--run must be a run ID (r-…)");
	check("parentRun", store.PATTERNS.run_id, "--parent-run must be a run ID (r-…)");
	check("session", store.PATTERNS.session_id, "--session must be a session identifier");
	check("wait", store.PATTERNS.wait_id, "--wait must be a wait ID (w-…)");
	check("outcome", /^(succeeded|failed|interrupted)$/, "--outcome must be succeeded, failed or interrupted");
	check("days", /^[1-9]\d{0,4}$/, "--days must be a positive integer");
	if (options.days) options.days = Number(options.days);
	return options;
}
function main(argv = process.argv.slice(2), env = process.env) {
	const [command, ...rest] = argv;
	const out = (text) => process.stdout.write(`${text}\n`);
	const err = (text) => process.stderr.write(`${text}\n`);
	try {
		const options = parse(rest);
		if (options.delivery === undefined && env.DELIVERY && ["report", "export"].includes(command)) {
			if (!DELIVERY.test(env.DELIVERY)) throw new Invocation("lifecycle: DELIVERY must be d-YYYYMMDD-xxxxxx");
			options.delivery = env.DELIVERY;
		}
		const json = options.json || Boolean(env.JSON);
		if (["start", "end", "wait-start", "wait-end"].includes(command)) {
			// Recording is best effort by contract: a refused or failed event is reported on stderr and
			// never turns the work it describes into a failure.
			try {
				if (command === "start") {
					if (!options.phase) throw new Invocation("lifecycle: start needs --phase");
					const result = start(options);
					out(`lifecycle: started run ${result.run_id} attempt ${result.attempt_id}`);
				} else if (command === "end") {
					if (!options.run || !options.outcome) throw new Invocation("lifecycle: end needs --run and --outcome");
					const result = end(options);
					out(result.already ? `lifecycle: run ${options.run} had already ended` : `lifecycle: ended run ${options.run} (${options.outcome})`);
				} else if (command === "wait-start") {
					if (!options.run) throw new Invocation("lifecycle: wait-start needs --run");
					out(`lifecycle: wait ${wait(options, "start").wait_id} started for run ${options.run}`);
				} else {
					if (!options.run || !options.wait) throw new Invocation("lifecycle: wait-end needs --run and --wait");
					wait(options, "end");
					out(`lifecycle: wait ${options.wait} ended`);
				}
			} catch (error) {
				if (error instanceof Invocation && /needs|must/.test(error.message)) throw error;
				err(`lifecycle: event not recorded — ${error.message}`);
			}
			return 0;
		}
		if (command === "report") {
			const report = aggregate({ delivery: options.delivery });
			out(json ? JSON.stringify(report, null, 2) : human(report));
			return 0;
		}
		if (command === "export") {
			if (!options.delivery) throw new Invocation("lifecycle: export needs --delivery (or DELIVERY=)");
			const ai = workspace();
			const doc = telemetryFor({ ai, delivery: options.delivery });
			const file = path.join(ai, "runs", "telemetry", `${options.delivery}.json`);
			writeAtomic(ai, file, `${JSON.stringify(doc, null, 2)}\n`);
			if (json) out(JSON.stringify(doc, null, 2));
			else out(`lifecycle: wrote ${path.relative(ROOT, file)} (${doc.coverage.measured} of ${doc.coverage.total} runs measured)`);
			return 0;
		}
		if (command === "prune") {
			const result = prune({ days: options.days, write: options.write });
			out(`lifecycle: ${result.write ? "pruned" : "would prune"} ${result.events} event(s) older than ${result.days} days${result.deliveries.length ? ` (deliveries ${result.deliveries.join(", ")})` : ""}; kept every delivery with a saved report${result.write ? "" : " — pass --write to remove"}`);
			return 0;
		}
		throw new Invocation("usage: lifecycle.js <start|end|wait-start|wait-end|report|export|prune> …");
	} catch (error) {
		err(error instanceof Invocation ? error.message : `lifecycle: ${error.message}`);
		return 2;
	}
}
if (require.main === module) process.exitCode = main();
module.exports = { REPORT, TELEMETRY, GAPS, union, total, overlap, timing, aggregate, telemetryFor, start, end, wait, prune, human, main, store };
