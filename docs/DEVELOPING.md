# Developing cwiki

Use a packaged C11 compiler and a POSIX-compatible `make`. The build does not
depend on GNU make extensions.

## Local verification

Run the same strict build and test entry point used by CI:

```sh
make check
```

Run the test suite under AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
make sanitize
```

Run both in sequence:

```sh
make verify
```

Linux CI additionally runs `make analyze` with packaged `clang-tidy` and enables
AddressSanitizer leak detection.

Run both reviewable demos:

```sh
make demo
```

The first is the Milestone 0 harness replay. Its authoritative recording is
`tests/fixtures/keys/seed.keys.raw`; its decoded companion is `seed.keys.txt`,
and its expected synthetic screen is `tests/snapshots/seed.screen`.

The second drives the real application through a PTY in a disposable fixture
vault. `m1-class-note.keys.raw` enters a chemistry/calculus note with math,
mhchem, and TikZ snippets, fills tab stops, undoes a deliberate edit, saves,
quits, and reopens the note. The demo compares the rendered terminal to
`tests/snapshots/m1-class-note.screen` and the exact saved bytes to
`tests/snapshots/m1-class-note.md`. Its readable key summary is
`m1-class-note.keys.txt`.

CI applies the same checks on Arch Linux, macOS with Homebrew, and FreeBSD. The
deterministic change-review gate is `scripts/review-changes.sh`; it also rejects
whitespace errors and tracked build artifacts before running the checks.

`make demo` also runs `make m2-demo`, the Milestone 2 demo skeleton. It copies
`tests/fixtures/vaults/m2-render` into a disposable root, runs
`scripts/tex-probe.sh`, then walks `scripts/m2-demo-steps.txt`, reporting each
step as pending with its owning slice until that slice adds
`scripts/m2-demo/<id>.sh`. The probe lists the TeX and Poppler tools it needs;
when any is missing it skips and exits 0 unless `CWIKI_REQUIRE_TEX=1`, which CI
sets and which makes a missing item fail `make check`.

Build artifacts stay under `build/` and are removed by `make clean`.
