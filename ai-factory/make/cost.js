#!/usr/bin/env node
// Reads ai-factory/runs/log.csv for `make cost` and classifies what it finds. Read-only over the
// log: nothing here creates, modifies, moves or deletes a file. `log.previous.csv` is never read —
// the rows quarantined there are cumulative snapshots from before spec 0007's collection change,
// and reading them would reinterpret them.
// This file runs inside an adopted repo, so it cannot require the plugin's
// skills/ai-hooks/scripts/_log-schema.js; it carries a byte-identical copy of the record-reading
// pair below, and fixtures/check-log-schema.sh pins the copies together.
const fs = require("fs");

// --- shared schema guard: the records()/width() pair, byte-identical with the two writers, pinned by fixtures/check-log-schema.sh ---
// Records, not lines: csv() quotes any field holding a comma or a newline, so a line-based
// read would tear one row in two and then judge both of the halves malformed.
function records(body) {
	const out = [];
	let cur = null,
		q = false;
	for (const line of body.split("\n")) {
		cur = cur === null ? line : cur + "\n" + line;
		for (let i = 0; i < line.length; i++) {
			const c = line[i];
			if (q) {
				if (c === '"') {
					if (line[i + 1] === '"') i++;
					else q = false;
				}
			} else if (c === '"') q = true;
		}
		if (!q) {
			out.push(cur);
			cur = null;
		}
	}
	if (cur !== null) out.push(cur);
	return out;
}
// Commas inside quotes do not separate fields. An unclosed quote is not a row that can be
// measured at all, so it reports -1 and gets moved aside rather than counted as some width.
function width(rec) {
	let n = 1,
		q = false;
	for (let i = 0; i < rec.length; i++) {
		const c = rec[i];
		if (q) {
			if (c === '"') {
				if (rec[i + 1] === '"') i++;
				else q = false;
			}
		} else if (c === '"') q = true;
		else if (c === ",") n++;
	}
	return q ? -1 : n;
}
// --- end shared schema guard ---

// The inverse of the writers' csv(): a quoted field keeps its commas and newlines, and a doubled
// quote inside one is a single quote. Only called on a record width() could measure.
function fields(rec) {
	const out = [];
	let cur = "",
		q = false;
	for (let i = 0; i < rec.length; i++) {
		const c = rec[i];
		if (q) {
			if (c === '"') {
				if (rec[i + 1] === '"') {
					cur += '"';
					i++;
				} else q = false;
			} else cur += c;
		} else if (c === '"') q = true;
		else if (c === ",") {
			out.push(cur);
			cur = "";
		} else cur += c;
	}
	out.push(cur);
	return out;
}

// By NAME, never by position. Spec 0007 inserts `agent` as the eighth column, not the last, so a
// reader that counted to a fixed index would read `accepted` where it meant `agent`. A column the
// header does not carry is absent, not an error: field() answers "" for it.
function row(names, rec) {
	const vals = fields(rec);
	const o = Object.create(null);
	for (let i = 0; i < names.length; i++) o[names[i]] = vals[i] ?? "";
	return o;
}
const field = (r, name) => (r && name in r ? r[name] : "");

// Three outcomes per record, and no fourth. A record as wide as the header conforms. A record of
// any other width is excluded from every total and counted — which is also what makes a pre-0007
// cumulative row harmless, since a 16-field row under a 17-column header is exactly that, so no
// detector for it exists here and none may be added. A record width() cannot measure at all
// (an unclosed quote) is unreadable and likewise excluded and counted.
function readLog(file) {
	const out = {
		file,
		exists: false,
		header: "",
		names: [],
		columns: 0,
		rows: [],
		wrongWidth: [],
		unreadable: [],
		counts: { records: 0, conforming: 0, wrongWidth: 0, unreadable: 0 },
	};
	let body;
	try {
		body = fs.readFileSync(file, "utf8");
	} catch {
		return out;
	}
	out.exists = true;
	const recs = records(body).filter((r) => r.trim() !== "");
	if (!recs.length) return out;
	const head = recs[0];
	out.header = head;
	out.columns = width(head);
	out.names = out.columns === -1 ? [] : fields(head);
	if (out.columns === -1) {
		// an unmeasurable header: every row below it too
		out.unreadable = recs.slice(1);
		out.counts.records = recs.length - 1;
		out.counts.unreadable = out.unreadable.length;
		return out;
	}
	for (const rec of recs.slice(1)) {
		const w = width(rec);
		if (w === -1) out.unreadable.push(rec);
		else if (w === out.columns) out.rows.push(row(out.names, rec));
		else out.wrongWidth.push(rec);
	}
	out.counts.records = recs.length - 1;
	out.counts.conforming = out.rows.length;
	out.counts.wrongWidth = out.wrongWidth.length;
	out.counts.unreadable = out.unreadable.length;
	return out;
}

// --- aggregation ------------------------------------------------------------------------------

// The bucket for a row that cannot be attributed on some dimension. A literal, not an empty
// string: "" as a group key is indistinguishable from a branch or task legitimately named "",
// and AC11 requires unattributed spend to be named rather than folded into another group.
const UNATTRIBUTED = "(unattributed)";

// The dimensions the spec names, each reading the column of the same name — except `session`,
// which reads `session_id`, and `day`, which the log has no column for and which is derived.
const DIMENSIONS = [
	"task",
	"agent",
	"tool",
	"model",
	"branch",
	"user",
	"day",
	"session",
];

// `cost_usd` is never read, by the decision of 2026-09-27: this feature reports tokens and turns,
// and a fleet view that wants money prices the token counts itself.
const METRICS = [
	"turns",
	"input_tokens",
	"output_tokens",
	"cache_read_tokens",
	"cache_write_tokens",
];

// A field that should hold a number and does not contributes nothing, rather than poisoning a
// whole group's total with NaN.
const num = (v) => {
	const n = Number(v);
	return Number.isFinite(n) ? n : 0;
};

// The date part of an ISO `ts`. A row whose `ts` is missing or is not a date cannot be placed on
// a day and is bucketed as unattributed, never dated today: spend recorded on a day that did not
// happen is worse than spend with no day.
function day(ts) {
	const s = String(ts ?? "");
	return /^\d{4}-\d{2}-\d{2}/.test(s) ? s.slice(0, 10) : UNATTRIBUTED;
}

function keyOf(r, dim) {
	if (dim === "day") return day(field(r, "ts"));
	const v = String(field(r, dim === "session" ? "session_id" : dim) ?? "");
	return v === "" ? UNATTRIBUTED : v;
}

// Recomputed from the summed tokens, NEVER averaged across rows. `hit_rate` is a ratio, and the
// mean of per-row ratios weights a ten-token row the same as a ten-million-token one — on this
// repo's own log those differ by six orders of magnitude. Same formula the writers use per row.
const hitRate = (m) =>
	m.cache_read_tokens /
	(m.input_tokens + m.cache_read_tokens + m.cache_write_tokens || 1);

const blank = (key) => {
	const m = { key, rows: 0 };
	for (const k of METRICS) m[k] = 0;
	m.hit_rate = 0;
	return m;
};

function accumulate(m, r) {
	m.rows++;
	for (const k of METRICS) m[k] += num(field(r, k));
	return m;
}

// One pass over the conforming rows, producing a group list per dimension plus the totals. Only
// rows the reader judged conforming are counted: a wrong-width or unreadable record reaches no
// total, and its count travels separately in `counts` so the two are never added together.
function aggregate(log) {
	const acc = Object.create(null);
	for (const dim of DIMENSIONS) acc[dim] = new Map();
	const totals = blank("(all)");
	let covered = 0;

	for (const r of log.rows) {
		accumulate(totals, r);
		// Coverage, not an average: AC12. How many rows carry a value at all, since the column is
		// hand-filled and usually empty.
		if (String(field(r, "accepted") ?? "") !== "") covered++;
		for (const dim of DIMENSIONS) {
			const k = keyOf(r, dim);
			acc[dim].set(k, accumulate(acc[dim].get(k) || blank(k), r));
		}
	}

	totals.hit_rate = hitRate(totals);
	const groups = Object.create(null);
	for (const dim of DIMENSIONS) {
		const list = [...acc[dim].values()];
		for (const m of list) m.hit_rate = hitRate(m);
		// Sorted by key so the same log yields the same order every run. Ordering for a reader's eye
		// belongs to whatever renders this, not here.
		list.sort((a, b) => (a.key < b.key ? -1 : a.key > b.key ? 1 : 0));
		groups[dim] = list;
	}

	return {
		file: log.file,
		exists: log.exists,
		columns: log.columns,
		counts: { ...log.counts }, // records · conforming · wrongWidth · unreadable, kept distinct
		totals,
		// AC12: the count and the denominator, so a caller can say "3 of 412" and never an average.
		accepted: { covered, of: log.rows.length },
		dimensions: DIMENSIONS.slice(),
		metrics: METRICS.slice(),
		unattributed: UNATTRIBUTED,
		// AC4: `session` is a dimension like any other, so every session_id survives into whatever is
		// exported — which is what lets a fleet view spot one session written to two layouts' logs.
		groups,
	};
}

// --- the export: a contract, not a convenience ------------------------------------------------

// These names are published. A collector in another repo parses them, and this repo has no fixture
// that can reach it, so a rename here breaks something invisible from here. check-cost.sh asserts
// the exact set below and fails on any departure — a rename, a removal, or an addition.
//
// The version is a plain integer. Adding a field does NOT change it, because a consumer reading
// version 1 keeps working when a field it never reads appears; renaming a field, removing one, or
// changing what an existing one means DOES. Consumers must therefore ignore fields they do not
// recognise. The check failing on an addition and the version not moving for one are deliberately
// different rules: the check guards against accidental drift, the version communicates breakage.
const SCHEMA = "ai-sdlc-cost";
const VERSION = 1;

// What a consumer may read per group. `rows` and `hit_rate` join the summed columns; there is no
// cost field, and adding one would be a new metric rather than a renamed one.
const EXPORT_METRICS = ["rows", ...METRICS, "hit_rate"];

const metricsOf = (m) => {
	const o = Object.create(null);
	for (const k of EXPORT_METRICS) o[k] = m[k];
	return o;
};

// No generated-at timestamp, deliberately: `ai-factory/docs/coding-standards.md` requires generated
// output to be reproducible — running twice in a row changes nothing — and a clock in the document
// would make every run differ and every diff noisy.
function exportDoc(a) {
	const groups = Object.create(null);
	for (const dim of a.dimensions) {
		groups[dim] = a.groups[dim].map((m) =>
			Object.assign({ key: m.key }, metricsOf(m)),
		);
	}
	return {
		schema: SCHEMA,
		version: VERSION,
		log: a.file,
		// ok | empty | missing | unreadableHeader. A consumer must not infer a damaged log from a
		// zero row count: a header that cannot be parsed counts zero rows just as an empty log does.
		status: status(a),
		// Distinct, never summed together: a wrong-width record and a torn one are different faults.
		counts: { ...a.counts },
		// Coverage and its denominator, never an average (AC12).
		accepted: { ...a.accepted },
		// Named here so a consumer can recognise the bucket without hard-coding the string.
		unattributed: a.unattributed,
		dimensions: a.dimensions,
		metrics: EXPORT_METRICS.slice(),
		totals: metricsOf(a.totals),
		// `agent` and `tool` are two independent dimensions carrying their columns verbatim, so no
		// consumer ever splits a value to recover one (the decision of 2026-09-26).
		groups,
	};
}

// --- the TSV form: the same numbers, flat -------------------------------------------------------

// One header line plus one line per group, carrying the JSON's field names so a consumer reading
// either sees the same words. Flattening needs one column the JSON does not have per group —
// `dimension`, saying which grouping a line belongs to — because the JSON nests that in its keys.
const TSV_COLUMNS = ["dimension", "key", ...EXPORT_METRICS];

// A group key can hold a tab or a newline: `task` does, in the fixtures and in real logs, because
// the writers quote any field containing one. Unescaped, a newline in a key would tear one line in
// two and a consumer would read the halves as two groups. Escaped the way a TSV consumer expects,
// so the one-line-per-group promise actually holds.
const tsvCell = (v) =>
	String(v ?? "")
		.replace(/\\/g, "\\\\")
		.replace(/\t/g, "\\t")
		.replace(/\r/g, "\\r")
		.replace(/\n/g, "\\n");

// The totals are deliberately absent: AC5 says one header line plus one line per group, and the
// totals are not a group. Nothing is lost — every dimension's groups partition the same conforming
// rows, so a consumer sums any one of them; `hit_rate` is then recomputed from the summed token
// columns, never averaged over the lines, for the reason the engine recomputes it.
function exportTsv(a) {
	const lines = [TSV_COLUMNS.join("\t")];
	for (const dim of a.dimensions) {
		for (const m of a.groups[dim]) {
			// Escaped once, by the map — escaping the key here as well would turn one newline into
			// a literal backslash-backslash-n and hand the consumer a key that is not the task's name.
			lines.push(
				[dim, m.key, ...EXPORT_METRICS.map((k) => m[k])]
					.map(tsvCell)
					.join("\t"),
			);
		}
	}
	return lines.join("\n") + "\n";
}

// --- the rendered table -------------------------------------------------------------------------

// Four stacked tables, one per grouping, each carrying all seven metrics — decided 2026-09-27.
// The other four dimensions the engine computes (tool, model, user, session) are in the export but
// not here: the four below are what AC8 names, and eight tables would bury them.
const TABLE_DIMENSIONS = ["task", "agent", "branch", "day"];

// Widths chosen against AC9's 80 columns and nothing else. 18 + 6 + 7 + 10 + 9 + 11 + 10 + 6 = 77,
// leaving three columns of slack for a terminal that counts differently. A key longer than the
// label column is truncated with a trailing "…" rather than wrapped: AC9 forbids a wrapped line,
// and the untruncated key is always available in the JSON and TSV forms.
const COLS = [
	["", 18, "left"],
	["rows", 5],
	["turns", 6],
	["input", 9],
	["output", 8],
	["cache_r", 10],
	["cache_w", 9],
	["hit", 5],
];
const LABEL_WIDTH = COLS[0][1];

const clip = (s) => {
	const v = String(s ?? "");
	return v.length <= LABEL_WIDTH
		? v.padEnd(LABEL_WIDTH)
		: v.slice(0, LABEL_WIDTH - 1) + "…";
};

// Two decimals, the same convention the writers use for the per-row hit_rate column. This is a
// rounding of the exported ratio, not a second calculation of it: the arithmetic happens once, in
// the engine, and both surfaces read the same number (AC7).
const pct = (n) => n.toFixed(2);

const cells = (m) => [
	m.rows,
	m.turns,
	m.input_tokens,
	m.output_tokens,
	m.cache_read_tokens,
	m.cache_write_tokens,
	pct(m.hit_rate),
];

function tableFor(a, dim) {
	const head = [
		clip(dim),
		...COLS.slice(1).map(([n, w]) => String(n).padStart(w)),
	].join(" ");
	const lines = [head];
	for (const m of a.groups[dim]) {
		lines.push(
			[
				clip(m.key),
				...cells(m).map((v, i) => String(v).padStart(COLS[i + 1][1])),
			].join(" "),
		);
	}
	return lines;
}

// AC9 is about every line, not only the tables: a 120-character footnote wraps just as badly as a
// wide row. Wrapped here at the same 80 so the notes stay inside it, continuation lines indented
// so they read as part of the note rather than as a new one.
const WIDTH = 80;
// `indentAll` matters: when every line carries the indent, the budget for every line is WIDTH minus
// the indent — including the first. Counting the first line against the full width and then adding
// two spaces to it is how a "wrapped at 80" line ends up 82 columns wide.
function wrap(text, indent = "  ", indentAll = false) {
	const out = [];
	let line = "";
	for (const word of String(text).split(" ")) {
		const limit = out.length || indentAll ? WIDTH - indent.length : WIDTH;
		if (line && (line + " " + word).length > limit) {
			out.push(line);
			line = word;
		} else line = line ? line + " " + word : word;
	}
	if (line) out.push(line);
	return out.map((l, i) => (i || indentAll ? indent + l : l));
}

// Four states, and the last three are why AC10 exists: an empty table with no explanation reads as
// a broken feature. `unreadableHeader` is separate from `empty` because it is NOT detectable from
// the row count — an unclosed quote in the header swallows the whole file into one record, so both
// states count zero rows. A corrupt log reported as "nothing recorded yet" would lose data
// silently, so the state is decided by the header, not by the count.
function status(a) {
	if (!a.exists) return "missing";
	if (a.columns === -1) return "unreadableHeader";
	if (a.counts.conforming === 0) return "empty";
	return "ok";
}

// Names the file read and the row count found, as AC10 requires.
//
// The path goes on a line of its own, and it is the one line that may exceed the 80 columns AC9
// asks for: a path longer than the terminal cannot be wrapped without breaking it, and a broken
// path is one a developer cannot paste or click. Everything else wraps.
function emptyMessage(a) {
	const c = a.counts;
	const body = {
		missing:
			"no log here yet — nothing has been recorded, so there are 0 rows to report. A run writes one; until then this is the expected answer and not a failure.",
		unreadableHeader: `cannot read this log's header — its first record does not parse as one, so no row beneath it can be read and 0 rows were counted. This is a damaged log, not an empty one: look for an unclosed quote on the first line.`,
		empty: `holds no countable row — ${c.records} record(s) read, of which ${c.wrongWidth} were the wrong width and ${c.unreadable} unreadable, leaving 0 counted.`,
	}[status(a)];
	return [a.file, ...wrap(body, "  ", true)];
}

// A view over the engine's structure, never a second implementation of the arithmetic (AC7).
function renderTable(a) {
	// Nothing to tabulate: say which file was read and what was found, and stop. Four empty tables
	// would be worse than a sentence.
	if (status(a) !== "ok") return emptyMessage(a).join("\n") + "\n";

	const out = [];
	for (const dim of TABLE_DIMENSIONS) {
		out.push(...tableFor(a, dim), "");
	}
	out.push(
		[
			clip("TOTAL"),
			...cells(a.totals).map((v, i) => String(v).padStart(COLS[i + 1][1])),
		].join(" "),
	);

	// AC12: coverage, and only when some row carries a value. A permanent "0 of N" trains a reader
	// to skip a line; the standing fact that the column is hand-filled lives in the docs instead.
	if (a.accepted.covered > 0) {
		out.push(
			...wrap(
				`accepted: ${a.accepted.covered} of ${a.accepted.of} rows carry a value`,
			),
		);
	}
	// Excluded records are reported, and the two kinds separately: a wrong-width record and a torn
	// one are different faults, and adding them would call one the other (AC14).
	if (a.counts.wrongWidth > 0 || a.counts.unreadable > 0) {
		out.push(
			...wrap(
				`excluded: ${a.counts.wrongWidth} of the wrong width, ${a.counts.unreadable} unreadable — none counted in any total`,
			),
		);
	}
	// The footnote AC8 asks for. It matters most above the agent table: Codex inlines the agent in
	// the session, so one session is the task and the agent, while Claude spreads across named
	// agents — a reader comparing the two would be comparing a part against a whole.
	const tools = a.groups.tool
		.map((g) => g.key)
		.filter((k) => k !== a.unattributed);
	if (tools.length > 1) {
		out.push(
			...wrap(
				`note: totals span ${tools.length} tools (${tools.join(", ")}); they attribute agents differently, so the agent table mixes a part with a whole`,
			),
		);
	}
	return out.join("\n") + "\n";
}

// --- entry point --------------------------------------------------------------------------------

// JSON mode writes ONE document to stdout and nothing else: no progress line, no banner, no path
// echo (AC1). Anything diagnostic belongs on stderr. The TSV mode and the rendered table are the
// next two steps of ai-factory/plans/0008-make-cost-report-and-export.md and land here.
// `JSON=1` and `TSV=1` are the whole flag surface, and v1 takes no filter of any kind — decided
// 2026-09-27. With both set, JSON wins: one of them has to, and silently printing two documents to
// one stdout would be worse than either choice. Asserted, so it cannot change unnoticed.
function main(argv, env) {
	const file = argv[2] || "ai-factory/runs/log.csv";
	const a = aggregate(readLog(file));
	if (env.JSON) {
		process.stdout.write(JSON.stringify(exportDoc(a), null, 2) + "\n");
		return 0;
	}
	if (env.TSV) {
		process.stdout.write(exportTsv(a));
		return 0;
	}
	process.stdout.write(renderTable(a));
	return 0;
}

// Only when run, never when required: a fixture that loads this module must not print.
if (require.main === module) process.exit(main(process.argv, process.env));

module.exports = {
	records,
	width,
	fields,
	row,
	field,
	readLog,
	aggregate,
	day,
	UNATTRIBUTED,
	DIMENSIONS,
	METRICS,
	exportDoc,
	exportTsv,
	renderTable,
	status,
	emptyMessage,
	main,
	SCHEMA,
	VERSION,
	EXPORT_METRICS,
	TSV_COLUMNS,
	TABLE_DIMENSIONS,
};
