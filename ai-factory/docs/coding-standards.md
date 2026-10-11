# Coding standards — cwiki

Match the surrounding code first; these are the rules the build and the spec already enforce.

## Language and build
C11, `-pedantic`, POSIX feature macros from the root `Makefile`. The compiler is the linter:
`-Wall -Wextra -Werror -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes`. Fix the
warning; never suppress it, cast it away blindly or relax the flags. The `Makefile` is POSIX
make — no GNU extensions, no pattern rules that need them. `make analyze` (clang-tidy,
warnings as errors) must stay clean.

## Structure
One module is one `src/<name>.c` plus `src/<name>.h`; includes use `-Isrc`. A module owns its
state and exposes it through its header. New modules are added to `APP_SOURCES` and
`ANALYZE_SOURCES`. Reuse the existing primitive before writing another: one zone engine, one
regex runtime, one bounded structural search, one durable-write path, one action/keymap dispatch.

## Text and memory
Positions are `(line, byte offset)`; interaction units are grapheme clusters via utf8proc. Never
split a UTF-8 sequence or a cluster. Display width comes from `unicode`, never from
`utf8proc_charwidth` or byte counts. Check every allocation and every size computation for
overflow; the sanitizer run is part of done, not an optional extra.

## Bounds
Anything that scans or matches takes an explicit limit: PCRE2 match, depth and JIT-stack limits
on every compiled pattern, line-distance and time budgets on structural search. Hitting a limit
is reported visibly, never treated as a silent no-match (R1.11.4). Compile patterns at load, not
per keystroke.

## Errors and files
Return errors to the caller; never swallow one. Note writes go only through `durable_write`; a
failed or durability-uncertain save leaves the buffer dirty. Signal handlers do async-signal-safe
work only (R1.5.8). Terminal state is restored on every exit path.

## Behaviour as data
User-visible commands are named actions; keys bind to action names. Where the spec calls for a
table (conceal categories, zone regions, snippets, cycles), build a table, not a boolean (R1.14.2).

## Tests
Tests live in `tests/<area>/test_<module>.c`, are plain C executables, and are wired into
`PRODUCT_TESTS`. Parsers and translators also get a fuzz target. Internal seams use the
`CWIKI_<MODULE>_TESTING` guard rather than exporting internals. Fault-injection covers every
durable-write step. Screen behaviour is asserted by key-recording replay against approved
snapshots in `tests/snapshots/`; a snapshot changes only when the specified behaviour changes.
Tests never touch a path outside their temporary fixture root.

## Dependencies
No new dependency without an ADR and a three-platform package check (FreeBSD pkg, Arch official
repositories, Homebrew core). Nothing from the AUR, a non-core tap or a ports-only build.
