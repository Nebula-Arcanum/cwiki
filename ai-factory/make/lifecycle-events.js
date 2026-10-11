// Lifecycle event store, shared by the headless runner, make/lifecycle.js and the plugin hooks.
// skills/ai-hooks/scripts/_lifecycle-events.js is a byte-identical copy (check-lifecycle.sh pins
// it), so this file requires nothing but Node built-ins: the caller hands in its own boundary().
//
// One immutable file per event under ai-factory/runs/lifecycle/events/. Nothing is ever edited
// in place, so two deliveries, two sessions or a parent and its delegated agent cannot overwrite
// each other's attribution. Events carry IDs, timings, outcomes and usage counts only — never a
// prompt, document content, secret or command output.
const fs = require("node:fs");
const path = require("node:path");
const crypto = require("node:crypto");

const SCHEMA = "t4-lifecycle-event";
const VERSION = 1;
const TYPES = [
	"run_started",
	"phase_started",
	"wait_started",
	"wait_ended",
	"phase_ended",
	"run_ended",
	"usage_linked",
	"session_bound",
];
const OUTCOMES = ["succeeded", "failed", "interrupted"];
const HOSTS = ["claude", "codex", "headless", "cli"];
const PATTERNS = {
	event_id: /^e-[0-9a-f]{32}$/,
	run_id: /^r-[0-9a-f]{16}$/,
	parent_run_id: /^r-[0-9a-f]{16}$/,
	attempt_id: /^a-[0-9a-f]{16}$/,
	wait_id: /^w-[0-9a-f]{16}$/,
	delivery_id: /^d-\d{8}-[0-9a-f]{6}$/,
	session_id: /^[A-Za-z0-9_-][A-Za-z0-9_.:-]{0,199}$/,
	phase: /^[a-z][a-z0-9-]{0,39}$/,
	task: /^[a-z][a-z0-9-]{0,39}$/,
	step: /^S\d{1,4}$/,
};
// One random identity per process, so two timestamps from the same process can use the
// monotonic clock and only cross-process durations fall back to wall-clock differences.
const PROCESS = `p-${crypto.randomBytes(8).toString("hex")}`;
const ORIGIN = process.hrtime.bigint();
const newId = (prefix) => `${prefix}-${crypto.randomBytes(8).toString("hex")}`;
const hashId = (...parts) => `e-${crypto.createHash("sha256").update(parts.join("\0")).digest("hex").slice(0, 32)}`;

module.exports = function lifecycleEvents({ boundary }) {
	const directory = (ai) => path.join(ai, "runs", "lifecycle", "events");
	// Opt-in lives beside the artifact contracts: ai-factory/contracts/config.json `lifecycle`.
	function config(ai) {
		const file = path.join(ai, "contracts", "config.json");
		try {
			boundary(ai).assertPath(file, { allowMissing: false });
		} catch (error) {
			if (error.code === "ENOENT") return { enabled: false, retention_days: null };
			throw error;
		}
		const value = JSON.parse(fs.readFileSync(file, "utf8"));
		const lifecycle = value?.lifecycle;
		if (lifecycle === undefined) return { enabled: false, retention_days: null };
		if (!lifecycle || typeof lifecycle !== "object" || Array.isArray(lifecycle)) throw new Error("lifecycle config must be an object");
		if (lifecycle.enabled !== undefined && typeof lifecycle.enabled !== "boolean") throw new Error("lifecycle.enabled must be true or false");
		const days = lifecycle.retention_days;
		if (days !== undefined && !(Number.isInteger(days) && days > 0)) throw new Error("lifecycle.retention_days must be a positive integer");
		return { enabled: lifecycle.enabled === true, retention_days: days ?? 90 };
	}
	function problem(event) {
		if (!event || typeof event !== "object" || Array.isArray(event)) return "not an object";
		if (event.schema !== SCHEMA) return "unknown schema";
		if (event.version !== VERSION) return `unsupported version ${JSON.stringify(event.version)}`;
		if (!TYPES.includes(event.type)) return `unknown type ${JSON.stringify(event.type)}`;
		if (!HOSTS.includes(event.host)) return "unknown host";
		if (typeof event.at !== "string" || Number.isNaN(Date.parse(event.at))) return "at must be an ISO timestamp";
		for (const [key, pattern] of Object.entries(PATTERNS))
			if (event[key] !== undefined && event[key] !== null && !(typeof event[key] === "string" && pattern.test(event[key]))) return `invalid ${key}`;
		if (!event.event_id) return "missing event_id";
		if (event.clock !== null && event.clock !== undefined && !(event.clock && /^p-[0-9a-f]{16}$/.test(event.clock.process) && Number.isFinite(event.clock.mono_ms))) return "invalid clock";
		if (event.outcome !== undefined && event.outcome !== null && !OUTCOMES.includes(event.outcome)) return "unknown outcome";
		if (event.reason !== undefined && event.reason !== null && (typeof event.reason !== "string" || event.reason.length > 200)) return "reason must be a short string";
		if (["run_started", "phase_started", "phase_ended", "run_ended", "wait_started", "wait_ended"].includes(event.type) && !event.run_id) return "missing run_id";
		if (["phase_started", "phase_ended"].includes(event.type) && !event.attempt_id) return "missing attempt_id";
		if (["wait_started", "wait_ended"].includes(event.type) && !event.wait_id) return "missing wait_id";
		if (event.type === "session_bound" && !(event.session_id && event.run_id)) return "session_bound needs session_id and run_id";
		if (event.type === "usage_linked") {
			const usage = event.usage;
			if (!usage || typeof usage !== "object") return "usage_linked needs usage";
			for (const key of ["turns", "input_tokens", "output_tokens", "cache_read_tokens", "cache_write_tokens"])
				if (!(Number.isInteger(usage[key]) && usage[key] >= 0)) return `usage.${key} must be a non-negative integer`;
			if (usage.cost_usd !== null && !(typeof usage.cost_usd === "number" && Number.isFinite(usage.cost_usd) && usage.cost_usd >= 0)) return "usage.cost_usd must be a number or null";
			if (typeof usage.record_key !== "string" || !/^[0-9a-f]{64}$/.test(usage.record_key)) return "usage.record_key must be a SHA-256";
		}
		return null;
	}
	// Idempotent: the event ID is the file name, so a replayed hook finds its event already there.
	// Staged under a private name, then renamed, so a reader never sees half an event.
	function emit(ai, fields) {
		const event = {
			schema: SCHEMA,
			version: VERSION,
			event_id: fields.event_id || hashId(crypto.randomUUID()),
			type: fields.type,
			at: fields.at || new Date().toISOString(),
			clock: fields.clock !== undefined ? fields.clock : { process: PROCESS, mono_ms: Number(process.hrtime.bigint() - ORIGIN) / 1e6 },
			host: fields.host,
		};
		for (const key of ["delivery_id", "run_id", "parent_run_id", "attempt_id", "wait_id", "phase", "step", "task", "session_id", "outcome", "reason", "tool", "model", "usage"])
			event[key] = fields[key] ?? null;
		const invalid = problem(event);
		if (invalid) throw new Error(`lifecycle event refused: ${invalid}`);
		const safe = boundary(ai);
		const dir = directory(ai);
		const file = path.join(dir, `${event.event_id}.json`);
		safe.mkdir(dir);
		safe.assertPath(file);
		if (fs.existsSync(file)) return { event, file, duplicate: true };
		const staged = path.join(dir, `.${event.event_id}.${process.pid}.${crypto.randomBytes(4).toString("hex")}.tmp`);
		safe.write(staged, `${JSON.stringify(event)}\n`, { exclusive: true });
		try {
			fs.renameSync(staged, file);
		} catch (error) {
			safe.unlink(staged);
			throw error;
		}
		return { event, file, duplicate: false };
	}
	// Tolerant reader: corrupt and unsupported files are reported, never dropped silently and
	// never interpreted.
	function read(ai) {
		const dir = directory(ai);
		const result = { events: [], corrupt: [], unsupported: [] };
		const safe = boundary(ai);
		try {
			safe.assertPath(dir, { allowMissing: false });
		} catch (error) {
			if (error.code === "ENOENT") return result;
			throw error;
		}
		for (const entry of fs.readdirSync(dir, { withFileTypes: true }).sort((a, b) => (a.name < b.name ? -1 : 1))) {
			if (entry.name.startsWith(".")) continue;
			const file = path.join(dir, entry.name);
			const relative = path.relative(path.dirname(ai), file).split(path.sep).join("/");
			if (!entry.isFile() || !/^e-[0-9a-f]{32}\.json$/.test(entry.name)) {
				result.corrupt.push({ path: relative, reason: entry.isSymbolicLink() ? "symlink refused" : "unexpected entry" });
				continue;
			}
			let event;
			try {
				safe.assertPath(file, { allowMissing: false });
				event = JSON.parse(fs.readFileSync(file, "utf8"));
			} catch (error) {
				result.corrupt.push({ path: relative, reason: error instanceof SyntaxError ? "not valid JSON" : error.message.replace(/: \/.*$/, "") });
				continue;
			}
			const invalid = problem(event);
			if (invalid) {
				(/^unsupported version|^unknown schema/.test(invalid) ? result.unsupported : result.corrupt).push({ path: relative, reason: invalid });
				continue;
			}
			if (`${event.event_id}.json` !== entry.name) {
				result.corrupt.push({ path: relative, reason: "event_id does not match the file name" });
				continue;
			}
			result.events.push(event);
		}
		return result;
	}
	// A usage record's identity comes from the unique source records it counted (message uuids,
	// agent ids, response ids), never from the session ID alone: one session holds many increments.
	function recordKey(claimed) {
		return crypto.createHash("sha256").update([...claimed].sort().join("\n")).digest("hex");
	}
	// Minimal CSV field split, matching the run log's quoting (make/log.js csv()).
	function csvFields(row) {
		const fields = [];
		let current = "";
		let quoted = false;
		const text = row.replace(/\r?\n$/, "");
		for (let index = 0; index < text.length; index++) {
			const char = text[index];
			if (quoted) {
				if (char === '"' && text[index + 1] === '"') {
					current += '"';
					index++;
				} else if (char === '"') quoted = false;
				else current += char;
			} else if (char === '"') quoted = true;
			else if (char === ",") {
				fields.push(current);
				current = "";
			} else current += char;
		}
		fields.push(current);
		return fields;
	}
	return { SCHEMA, VERSION, TYPES, OUTCOMES, HOSTS, PATTERNS, config, problem, emit, read, recordKey, csvFields, newId, hashId, directory };
};
