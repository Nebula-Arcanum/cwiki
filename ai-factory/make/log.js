#!/usr/bin/env node
// Appends one row for a headless run (make ai) to ai-factory/runs/log.csv.
// The interactive path — skills/ai-hooks/scripts/session-stop.js — writes the SAME columns.
// If you change HEADER here, change it there too; the fixture test pins them together.
const fs = require("fs");
const path = require("path");
const { boundary } = require("./safe-files");
const { execFileSync } = require("child_process");

const HEADER =
	"ts,session_id,source,user,branch,task,tool,agent,model,turns,input_tokens,output_tokens,cache_read_tokens,cache_write_tokens,hit_rate,cost_usd,accepted";

// --- shared schema guard: byte-identical in both writers, pinned by fixtures/check-log-schema.sh ---
const COLS = HEADER.split(",").length;
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
// Two ways the file goes wrong, one remedy. A header from an older schema means the whole
// file predates this one — move every row. A header that matches with rows that do not means
// a writer still on an older schema appended into a current file; the header check cannot see
// that, which is how twelve-field rows sat under a sixteen-field header. Move just those.
// Nothing is reinterpreted into the new columns: the shape is not enough to tell which writer
// produced a row, and guessing is how the original mixed-schema bug started.
function ensureSchema(file) {
	const workspace = path.dirname(path.dirname(file));
	if (
		path.basename(path.dirname(file)) !== "runs" ||
		!["ai-factory", "ai"].includes(path.basename(workspace))
	)
		throw new Error("Log must be inside an adopted workspace runs directory");
	const safe = boundary(workspace);
	safe.assertPath(file);
	const prev = path.join(path.dirname(file), "log.previous.csv");
	safe.assertPath(prev);
	safe.mkdir(path.dirname(file));
	if (!fs.existsSync(file)) {
		try {
			safe.write(file, HEADER + "\n", { exclusive: true });
			return;
		} catch (error) {
			if (error.code !== "EEXIST") throw error;
		}
	}
	const body = fs.readFileSync(file, "utf8");
	if (body.split("\n")[0] !== HEADER) {
		safe.append(prev, body);
		safe.write(file, HEADER + "\n");
		return;
	}
	const keep = [],
		move = [];
	for (const r of records(body).slice(1)) {
		if (!r.trim()) continue;
		(width(r) === COLS ? keep : move).push(r);
	}
	if (!move.length) return;
	safe.append(
		prev,
		`# ${new Date().toISOString()} moved out of ${path.basename(file)}: not ${COLS} fields\n${move.join("\n")}\n`,
	);
	safe.write(file, [HEADER, ...keep].join("\n") + "\n");
}
// --- end shared schema guard ---

function git(args) {
	try {
		return execFileSync("git", args, { stdio: ["ignore", "pipe", "ignore"] })
			.toString()
			.trim();
	} catch {
		return "";
	}
}
const csv = (v) => {
	const s = String(v ?? "");
	return /[",\n]/.test(s) ? '"' + s.replace(/"/g, '""') + '"' : s;
};

// Two output shapes. claude -p --output-format json writes one object with `usage` and
// `total_cost_usd`. codex exec --json writes JSONL events: `thread.started` carries the id,
// each `turn.completed` carries usage. Sniffed, not configured, so the file speaks for itself.
function parseRun(text) {
	const t = text.trim();
	try {
		const o = JSON.parse(t);
		const u = o.usage ?? {};
		return {
			id: o.session_id ?? "",
			turns: o.num_turns ?? "",
			cost: o.total_cost_usd ?? "",
			inp: u.input_tokens ?? 0,
			out: u.output_tokens ?? 0,
			cr: u.cache_read_input_tokens ?? 0,
			cw: u.cache_creation_input_tokens ?? 0,
		};
	} catch {}
	const r = { id: "", turns: 0, cost: "", inp: 0, out: 0, cr: 0, cw: 0 };
	for (const line of t.split("\n")) {
		if (!line.trim()) continue;
		let e;
		try {
			e = JSON.parse(line);
		} catch {
			continue;
		}
		if (e.type === "thread.started" && e.thread_id) r.id = e.thread_id;
		if (e.type === "turn.completed" && e.usage) {
			const u = e.usage;
			r.turns++;
			// codex reports input_tokens INCLUSIVE of cached ones (the OpenAI convention), while
			// claude reports them separately. Subtract so the column means the same in both rows.
			const cached = u.cached_input_tokens ?? 0;
			r.inp += Math.max(0, (u.input_tokens ?? 0) - cached);
			r.cr += cached;
			r.cw += u.cache_write_input_tokens ?? 0;
			// output_tokens already includes reasoning_output_tokens.
			r.out += u.output_tokens ?? 0;
		}
	}
	return r; // cost stays "" — codex reports none, and a guessed price is worse than none
}

const [file, task, tool, model] = process.argv.slice(2);
boundary(path.dirname(path.dirname(file))).assertPath(file, {
	allowMissing: false,
});
const run = parseRun(fs.readFileSync(file, "utf8"));
const { inp, out, cr, cw } = run;
const hit = (cr / (inp + cr + cw || 1)).toFixed(2);

ensureSchema(path.join(path.dirname(file), "log.csv"));
process.stdout.write(
	[
		new Date().toISOString(),
		run.id,
		"make",
		process.env.GITLAB_USER || git(["config", "user.name"]) || "unknown",
		git(["rev-parse", "--abbrev-ref", "HEAD"]),
		task,
		tool,
		"",
		model || "", // a headless run has no subagent, so `agent` is empty
		run.turns,
		inp,
		out,
		cr,
		cw,
		hit,
		run.cost,
		"",
	]
		.map(csv)
		.join(",") + "\n",
);
