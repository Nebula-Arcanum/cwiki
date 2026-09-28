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

Replay the checked-in raw key recording and compare its deterministic screen to
the expected snapshot:

```sh
make demo
```

The authoritative recording is `tests/fixtures/keys/seed.keys.raw`; its decoded
companion is `seed.keys.txt`, and the expected screen is
`tests/snapshots/seed.screen`.

CI applies the same checks on Arch Linux, macOS with Homebrew, and FreeBSD. The
deterministic change-review gate is `scripts/review-changes.sh`; it also rejects
whitespace errors and tracked build artifacts before running the checks.

Build artifacts stay under `build/` and are removed by `make clean`.
