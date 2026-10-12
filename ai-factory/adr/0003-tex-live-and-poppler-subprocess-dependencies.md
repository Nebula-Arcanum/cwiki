# 0003 — TeX Live and Poppler as runtime subprocess dependencies

Date: 2026-10-11 · Status: accepted

## Context
Milestone 2 renders math, mhchem and TikZ through LuaLaTeX (explicit pdfLaTeX override) and rasterizes
with Poppler `pdftocairo` (R6.1.1-R6.1.3). Neither is a C library cwiki can sensibly link, and no
dependency may arrive without an ADR. The PRODUCT platform boundary requires packages on macOS, Linux
and FreeBSD with no AUR, non-core tap or ports build.

## Decision
TeX Live (`lualatex`, `pdflatex`) and Poppler (`pdftocairo`) are runtime dependencies invoked only as
subprocesses and never linked. Every TeX invocation passes `-no-shell-escape` (R6.4.1) until Milestone 3's
machine-local trust policy exists; synchronized content can never lift it (R6.4.2). `scripts/tex-probe.sh`
checks the three tools plus `mhchem.sty`, `tikz.sty`, `amsmath.sty`, `standalone.cls` and `preview.sty`.
The `docs/SPEC.md` §0 matrix remains the documented user install; the CI subset below is only the
smallest set that makes the probe report all eight items found.

## Package matrix (revalidated 2026-10-11)

| Platform | TeX Live | Poppler (`pdftocairo`) |
|---|---|---|
| FreeBSD 14 quarterly (pkg index) | `texlive-full` 20250308 (`print/texlive-full`); parts `texlive-base` 20250308_4, `texlive-texmf` 20250308_1 | `poppler-utils` 26.04.0_1 (`graphics/poppler-utils`) |
| Arch (`extra`) | official `texlive-*` split packages, 2026.1 (`texlive-basic`, `-latex`, `-latexextra`, `-mathscience`, `-pictures`, `-luatex`) | `extra/poppler` 26.08.0 |
| Homebrew | cask `mactex-no-gui` 2026.0324; cask `basictex` 2026.0301 | formula `poppler` 26.10.0 (homebrew/core) |

None requires the AUR, a non-core Homebrew tap or a FreeBSD port built from source. This confirms the
SPEC §0 matrix; no name or availability change is needed there. FreeBSD has no granular TeX packages.

## CI subset
Probe must report all eight items found, with `CWIKI_REQUIRE_TEX=1` set on every `make check`.

| Job | Install | Caching |
|---|---|---|
| Arch | `texlive-basic texlive-latex texlive-latexextra texlive-mathscience texlive-pictures texlive-luatex poppler` | pacman download cache, keyed on `ci.yml` |
| macOS | `brew install poppler`; cask `basictex`, then `tlmgr install standalone preview mhchem pgf chemgreek` (dependencies resolved by tlmgr) | `/usr/local/texlive`, keyed on `ci.yml`; install skipped on a hit |
| FreeBSD | `texlive-base texlive-texmf poppler-utils` (omits `texlive-docs` and `texlive-tlmgr`) | none |

Any change to the subset edits `ci.yml`, which invalidates both caches.

## Consequences
Easier: one probe states exactly what is missing; CI fails instead of silently skipping TeX tests.
Harder: the Arch and FreeBSD subsets and the BasicTeX package list are unproven until the first CI run
after push approval, and `texlive-texmf` on FreeBSD is large. Shell escape stays off for all runs.
