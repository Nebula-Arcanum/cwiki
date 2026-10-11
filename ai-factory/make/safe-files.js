// Self-contained filesystem boundary for adopted workspaces. The host sandbox must
// still prevent a concurrent hostile process from swapping an ancestor after a check.
const fs = require("node:fs");
const path = require("node:path");

function boundary(root) {
	const base = path.resolve(root);
	function assertPath(file, { allowMissing = true } = {}) {
		const target = path.resolve(file);
		const relative = path.relative(base, target);
		if (
			relative === ".." ||
			relative.startsWith(`..${path.sep}`) ||
			path.isAbsolute(relative)
		) {
			throw new Error(`Workspace write outside ai-factory: ${target}`);
		}
		const parts = relative ? relative.split(path.sep) : [];
		let current = base;
		for (let index = -1; index < parts.length; index++) {
			if (index >= 0) current = path.join(current, parts[index]);
			let stat;
			try {
				stat = fs.lstatSync(current);
			} catch (error) {
				if (allowMissing && error.code === "ENOENT") continue;
				throw error;
			}
			if (stat.isSymbolicLink())
				throw new Error(`Refusing symlink: ${current}`);
			if (index < parts.length - 1 && !stat.isDirectory())
				throw new Error(`Not a directory: ${current}`);
			if (!stat.isDirectory() && (!stat.isFile() || stat.nlink !== 1)) {
				throw new Error(`Refusing non-regular or shared file: ${current}`);
			}
		}
		return target;
	}
	function mkdir(directory) {
		const target = assertPath(directory);
		if (fs.existsSync(target)) {
			if (!fs.statSync(target).isDirectory())
				throw new Error(`Not a directory: ${target}`);
			return target;
		}
		if (target !== base) mkdir(path.dirname(target));
		try {
			fs.mkdirSync(target, { mode: 0o700 });
		} catch (error) {
			if (error.code !== "EEXIST") throw error;
		}
		assertPath(target, { allowMissing: false });
		return target;
	}
	function open(file, flags) {
		const target = assertPath(file);
		const fd = fs.openSync(
			target,
			flags | (fs.constants.O_NOFOLLOW || 0),
			0o600,
		);
		const stat = fs.fstatSync(fd);
		if (!stat.isFile() || stat.nlink !== 1) {
			fs.closeSync(fd);
			throw new Error(`Refusing non-regular or shared file: ${target}`);
		}
		return fd;
	}
	function write(file, content, { exclusive = false } = {}) {
		const flags =
			fs.constants.O_WRONLY |
			fs.constants.O_CREAT |
			(exclusive ? fs.constants.O_EXCL : fs.constants.O_TRUNC);
		const fd = open(file, flags);
		try {
			fs.writeFileSync(fd, content);
		} finally {
			fs.closeSync(fd);
		}
	}
	function append(file, content) {
		const fd = open(
			file,
			fs.constants.O_WRONLY | fs.constants.O_CREAT | fs.constants.O_APPEND,
		);
		try {
			fs.writeFileSync(fd, content);
		} finally {
			fs.closeSync(fd);
		}
	}
	function unlink(file) {
		const target = assertPath(file);
		try {
			fs.unlinkSync(target);
		} catch (error) {
			if (error.code !== "ENOENT") throw error;
		}
	}
	function scratch(parent) {
		mkdir(parent);
		const directory = fs.mkdtempSync(path.join(assertPath(parent), "run-"));
		fs.chmodSync(directory, 0o700);
		return directory;
	}
	function removeScratch(directory) {
		// Never traverse new directories or links introduced while a child was running.
		assertPath(directory, { allowMissing: false });
		for (const entry of fs.readdirSync(directory))
			unlink(path.join(directory, entry));
		fs.rmdirSync(directory);
	}
	return {
		assertPath,
		mkdir,
		open,
		write,
		append,
		unlink,
		scratch,
		removeScratch,
	};
}

// Serialize ledger + row/schema changes, including simultaneous Stop/SubagentStop events.
// A dead owner's lock can be recovered; a live or unreadable lock is never removed.
function withLogLock(ai, action) {
	const safe = boundary(ai),
		runs = path.join(ai, "runs"),
		lock = path.join(runs, ".hook-lock");
	safe.mkdir(runs);
	const deadline = Date.now() + 5000;
	for (;;) {
		try {
			safe.write(lock, String(process.pid), { exclusive: true });
			break;
		} catch (error) {
			if (error.code !== "EEXIST") throw error;
			let owner;
			try {
				safe.assertPath(lock, { allowMissing: false });
				owner = fs.readFileSync(lock, "utf8");
			} catch (readError) {
				if (readError.code === "ENOENT") continue;
				throw readError;
			}
			if (/^[1-9][0-9]*$/.test(owner)) {
				try {
					process.kill(Number(owner), 0);
				} catch (check) {
					if (check.code === "ESRCH") {
						safe.unlink(lock);
						continue;
					}
				}
			}
			if (Date.now() >= deadline)
				throw new Error(
					"Accounting is busy; retry after its current writer finishes",
				);
			Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 10);
		}
	}
	try {
		return action(safe);
	} finally {
		safe.unlink(lock);
	}
}
module.exports = { boundary, withLogLock };
