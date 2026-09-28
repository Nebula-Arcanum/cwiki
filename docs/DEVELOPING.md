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
AddressSanitizer leak detection. `make demo` is the user-facing fixture replay
and snapshot demonstration; during the initial build-foundation task it aliases
`make check` and will gain the seed replay before Milestone 0 completes.

Build artifacts stay under `build/` and are removed by `make clean`.
