# Completion report — 0001 — Milestone 2 acceptance harness for rendered notes

**Status:** ready
**Delivery:** `d-20261011-ff71bd` (planned)
**Generated:** 2026-10-12T11:51:48.647Z · schema t4-delivery-report v1
**Snapshot:** code `65bb6007a92886272a5efda6d5bbf1e54f1688b76edf4dda40c4b46499a716ac` at `7cfc0a1cc2ee` · evidence fingerprint `f974e4642a6c30f51ff675be645fdc2b8c419a2e64daf6f58717cfc3bb941650`
**Criteria:** 21 (passed 21, attested 0, pending 0, failed 0, uncovered 0, unverified 0)

> Assembled from local evidence only. Ready means ready for delivery handoff — not merged, deployed or published. Contract validation is structural; it does not judge whether a requirement is right.

## Why this status

Every criterion has current passing evidence and every policy requirement is met.

## Sources

| Kind | Markdown | Sidecar | State | SHA-256 |
|---|---|---|---|---|
| plan | `ai-factory/plans/0001-m2-render-acceptance-harness.md` | `ai-factory/plans/0001-m2-render-acceptance-harness.contract.json` | valid | `53f1939aa9a9a4389f34f77f7c03d652b84592f3675bf843a0078f505516efde` |
| spec | `ai-factory/specs/0001-m2-render-acceptance-harness.md` | `ai-factory/specs/0001-m2-render-acceptance-harness.contract.json` | valid | `da4b5a1d37842f280a0bd6c0f497966e0b318aad500e19b3f9f2ad3a0d68fe5b` |

## Acceptance criteria

| Criterion | Steps | Checks | Result | Basis | Evidence |
|---|---|---|---|---|---|
| AC1 | S6 | `sh tests/docs/check_m2_closure.sh` | passed | automated | `ai-factory/evidence/d-20261011-ff71bd/S6-step.json`<br>`ai-factory/evidence/d-20261011-ff71bd/final.json` |
| AC2 | S6 | `sh tests/docs/check_m2_closure.sh` | passed | automated | `ai-factory/evidence/d-20261011-ff71bd/S6-step.json`<br>`ai-factory/evidence/d-20261011-ff71bd/final.json` |
| AC3 | S6 | `sh tests/docs/check_m2_closure.sh` | passed | automated | `ai-factory/evidence/d-20261011-ff71bd/S6-step.json`<br>`ai-factory/evidence/d-20261011-ff71bd/final.json` |
| AC4 | S6 | `sh tests/docs/check_m2_closure.sh` | passed | automated | `ai-factory/evidence/d-20261011-ff71bd/S6-step.json`<br>`ai-factory/evidence/d-20261011-ff71bd/final.json` |
| AC5 | S2 | `sh tests/render/check_render_fixture.sh` | passed | automated | `ai-factory/evidence/d-20261011-ff71bd/S2-step.json`<br>`ai-factory/evidence/d-20261011-ff71bd/final.json` |
| AC6 | S2 | `sh tests/render/check_render_fixture.sh` | passed | automated | `ai-factory/evidence/d-20261011-ff71bd/S2-step.json`<br>`ai-factory/evidence/d-20261011-ff71bd/final.json` |
| AC7 | S2, S6 | `sh tests/render/check_render_fixture.sh`<br>`sh tests/docs/check_m2_closure.sh` | passed | automated | `ai-factory/evidence/d-20261011-ff71bd/S2-step.json`<br>`ai-factory/evidence/d-20261011-ff71bd/S6-step.json`<br>`ai-factory/evidence/d-20261011-ff71bd/final.json` |
| AC8 | S1, S4 | `make support-test`<br>`sh tests/render/test_m2_demo.sh build/cwiki-m2-demo`<br>`make demo` | passed | automated | `ai-factory/evidence/d-20261011-ff71bd/S1-step.json`<br>`ai-factory/evidence/d-20261011-ff71bd/S4-step.json`<br>`ai-factory/evidence/d-20261011-ff71bd/final.json` |
| AC9 | S4 | `sh tests/render/test_m2_demo.sh build/cwiki-m2-demo`<br>`make demo` | passed | automated | `ai-factory/evidence/d-20261011-ff71bd/S4-step.json`<br>`ai-factory/evidence/d-20261011-ff71bd/final.json` |
| AC10 | S4 | `sh tests/render/test_m2_demo.sh build/cwiki-m2-demo`<br>`make demo` | passed | automated | `ai-factory/evidence/d-20261011-ff71bd/S4-step.json`<br>`ai-factory/evidence/d-20261011-ff71bd/final.json` |
| AC11 | S4 | `sh tests/render/test_m2_demo.sh build/cwiki-m2-demo`<br>`make demo` | passed | automated | `ai-factory/evidence/d-20261011-ff71bd/S4-step.json`<br>`ai-factory/evidence/d-20261011-ff71bd/final.json` |
| AC12 | S3 | `sh tests/render/test_tex_probe.sh`<br>`make check`<br>`env CWIKI_REQUIRE_TEX=1 make check` | passed | automated | `ai-factory/evidence/d-20261011-ff71bd/S3-step.json`<br>`ai-factory/evidence/d-20261011-ff71bd/final.json` |
| AC13 | S3 | `sh tests/render/test_tex_probe.sh`<br>`make check`<br>`env CWIKI_REQUIRE_TEX=1 make check` | passed | automated | `ai-factory/evidence/d-20261011-ff71bd/S3-step.json`<br>`ai-factory/evidence/d-20261011-ff71bd/final.json` |
| AC14 | S3 | `sh tests/render/test_tex_probe.sh`<br>`make check`<br>`env CWIKI_REQUIRE_TEX=1 make check` | passed | automated | `ai-factory/evidence/d-20261011-ff71bd/S3-step.json`<br>`ai-factory/evidence/d-20261011-ff71bd/final.json` |
| AC15 | S3 | `sh tests/render/test_tex_probe.sh`<br>`make check`<br>`env CWIKI_REQUIRE_TEX=1 make check` | passed | automated | `ai-factory/evidence/d-20261011-ff71bd/S3-step.json`<br>`ai-factory/evidence/d-20261011-ff71bd/final.json` |
| AC16 | S5 | `env CWIKI_REQUIRE_TEX=1 make check` | passed | automated | `ai-factory/evidence/d-20261011-ff71bd/S5-step.json`<br>`ai-factory/evidence/d-20261011-ff71bd/final.json` |
| AC17 | S5 | `env CWIKI_REQUIRE_TEX=1 make check` | passed | automated | `ai-factory/evidence/d-20261011-ff71bd/S5-step.json`<br>`ai-factory/evidence/d-20261011-ff71bd/final.json` |
| AC18 | S5 | `env CWIKI_REQUIRE_TEX=1 make check` | passed | automated | `ai-factory/evidence/d-20261011-ff71bd/S5-step.json`<br>`ai-factory/evidence/d-20261011-ff71bd/final.json` |
| AC19 | S5 | `env CWIKI_REQUIRE_TEX=1 make check` | passed | automated | `ai-factory/evidence/d-20261011-ff71bd/S5-step.json`<br>`ai-factory/evidence/d-20261011-ff71bd/final.json` |
| AC20 | S5 | `env CWIKI_REQUIRE_TEX=1 make check` | passed | automated | `ai-factory/evidence/d-20261011-ff71bd/S5-step.json`<br>`ai-factory/evidence/d-20261011-ff71bd/final.json` |
| AC21 | S7 | `git diff --exit-code 152787c -- src tests/snapshots tests/fixtures/keys`<br>`make verify`<br>`env CWIKI_REQUIRE_TEX=1 make check`<br>`make demo`<br>`make check` | passed | automated | `ai-factory/evidence/d-20261011-ff71bd/S7-step.json`<br>`ai-factory/evidence/d-20261011-ff71bd/final.json` |

### Plan steps

| Step | Mark | Criteria | Evidence | Status |
|---|---|---|---|---|
| S1 | [x] | AC8 | `ai-factory/evidence/d-20261011-ff71bd/S1-step.json` | passed |
| S2 | [x] | AC5, AC6, AC7 | `ai-factory/evidence/d-20261011-ff71bd/S2-step.json` | passed |
| S3 | [x] | AC12, AC13, AC14, AC15 | `ai-factory/evidence/d-20261011-ff71bd/S3-step.json` | passed |
| S4 | [x] | AC8, AC9, AC10, AC11 | `ai-factory/evidence/d-20261011-ff71bd/S4-step.json` | passed |
| S5 | [x] | AC16, AC17, AC18, AC19, AC20 | `ai-factory/evidence/d-20261011-ff71bd/S5-step.json` | passed |
| S6 | [x] | AC1, AC2, AC3, AC4, AC7 | `ai-factory/evidence/d-20261011-ff71bd/S6-step.json` | passed |
| S7 | [x] | AC21 | `ai-factory/evidence/d-20261011-ff71bd/S7-step.json` | passed |

## Changes and verification

No base branch found; untracked files only: 0 path(s).

| Evidence | Phase | Commands | Status | State | Output |
|---|---|---|---|---|---|
| `ai-factory/evidence/d-20261011-ff71bd/S1-red.json` | red (expects fail) | `make support-test` → failed (exit 2) | failed | valid | `ai-factory/runs/evidence/d-20261011-ff71bd/S1-red.log` |
| `ai-factory/evidence/d-20261011-ff71bd/S1-step.json` | step (expects pass) | `make support-test` → passed (exit 0) | passed | valid | `ai-factory/runs/evidence/d-20261011-ff71bd/S1-step.log` |
| `ai-factory/evidence/d-20261011-ff71bd/S2-step.json` | step (expects pass) | `sh tests/render/check_render_fixture.sh` → passed (exit 0) | passed | valid | `ai-factory/runs/evidence/d-20261011-ff71bd/S2-step.log` |
| `ai-factory/evidence/d-20261011-ff71bd/S3-step.json` | step (expects pass) | `sh tests/render/test_tex_probe.sh` → passed (exit 0)<br>`make check` → passed (exit 0)<br>`env CWIKI_REQUIRE_TEX=1 make check` → passed (exit 0) | passed | valid | `ai-factory/runs/evidence/d-20261011-ff71bd/S3-step.log` |
| `ai-factory/evidence/d-20261011-ff71bd/S4-step.json` | step (expects pass) | `sh tests/render/test_m2_demo.sh build/cwiki-m2-demo` → passed (exit 0)<br>`make demo` → passed (exit 0) | passed | valid | `ai-factory/runs/evidence/d-20261011-ff71bd/S4-step.log` |
| `ai-factory/evidence/d-20261011-ff71bd/S5-step.json` | step (expects pass) | `env CWIKI_REQUIRE_TEX=1 make check` → passed (exit 0) | passed | valid | `ai-factory/runs/evidence/d-20261011-ff71bd/S5-step.log` |
| `ai-factory/evidence/d-20261011-ff71bd/S6-step.json` | step (expects pass) | `sh tests/docs/check_m2_closure.sh` → passed (exit 0) | passed | valid | `ai-factory/runs/evidence/d-20261011-ff71bd/S6-step.log` |
| `ai-factory/evidence/d-20261011-ff71bd/S7-step.json` | step (expects pass) | `make check` → passed (exit 0) | passed | valid | `ai-factory/runs/evidence/d-20261011-ff71bd/S7-step.log` |
| `ai-factory/evidence/d-20261011-ff71bd/final.json` | final (expects pass) | `git diff --exit-code 152787c -- src tests/snapshots tests/fixtures/keys` → passed (exit 0)<br>`make verify` → passed (exit 0)<br>`env CWIKI_REQUIRE_TEX=1 make check` → passed (exit 0)<br>`make demo` → passed (exit 0) | passed | valid | `ai-factory/runs/evidence/d-20261011-ff71bd/final.log` |
| `ai-factory/evidence/d-20261011-ff71bd/review.json` | final (expects pass) | `make review` → passed (exit 0) | passed | valid | — |

## Review

Verdict **approve** (passed, valid) by claude — `ai-factory/evidence/d-20261011-ff71bd/review.json`.

| Severity | Blocking | Location | Issue |
|---|---|---|---|
| minor | no | tests/docs/check_s1_gaps.sh:24 | The AC3 guard regex ('R1\\.12\\.7\|R6\\.4\\.1\|R1\\.12\\.9a\|R1\\.8\\.7') still omits R6.3.3a. The S10 row now correctly cites 'R6.3.3 (search part)', but a regression that puts the deferred link-navigation id back into an open scope row would pass the check. |
| minor | no | tests/docs/check_s1_gaps.sh:1 | No make target, script or step evidence runs check_s1_gaps.sh. Only review.json mentions it, so its AC2, AC10, AC16-AC19 and AC21 assertions are manual. Its AC21 'git diff 152787c -- src' will fail for good once S2 touches src/, so it cannot join 'make check' as written. |
| minor | no | tests/support/fixture_vault.c:42 | copy_file opens the destination with O_CREAT\|O_TRUNC but without O_EXCL or O_NOFOLLOW, and copy_tree accepts EEXIST from mkdir and recurses into the existing directory. If a destination root already holds a symlink, the copy would follow it and write outside the root. That breaks the plan's rule tha |
| minor | no | tests/support/m2_demo.c:52 | The root is copied into saved[4096] with an unchecked snprintf. If the path were truncated, the access() check after destroy would test the wrong path and could pass falsely. |
| minor | no | docs/ACCEPTANCE.md:189 | The S5 scope row cites R6.1.3 and R9.3.1, but the ROADMAP S5 Slices row also lists 'R6.1.4 inputs' (cell metrics and colours that feed the render key). Each slice's R ids should match the ROADMAP Slices table. |
| minor | no | .github/workflows/ci.yml:68 | The macOS BasicTeX cache restores /usr/local/texlive only. On a cache hit, the cask's /etc/paths.d entry and the /Library/TeX symlinks are absent, and restoring into root-owned /usr/local has not been tried on a runner. The PATH loop should still find the binaries, but this is unproven, as ADR 0003  |

## Limitations and follow-up

- Nothing outstanding from the evidence.

### Open questions (spec)

None.

### Risks (plan)

| Risk | Check |
|---|---|
| Probe or demo script uses a GNU/bash extension, or the Makefile gains a GNU make construct. | Scripts run under `/bin/sh` in the tests; macOS `make` is GNU, so the real POSIX-make proof is the FreeBSD CI job (bmake). `make analyze` and FreeBSD CI only run after the user approves a push. |
| `PATH`-stubbed probe test accidentally finds real TeX tools. | Test sets `PATH` to the stub directory alone and invokes `/bin/sh` by absolute path; the empty-install case asserts eight `missing` lines on this TeX-equipped machine. |
| Copy-in writes outside the root or follows a symlink out of the fixture. | Step 1 symlink-rejection test; copy-in builds destination paths only under `vault->root`. |
| Demo leaves a temporary vault behind. | Driver asserts the root is gone; `test_m2_demo.sh` checks the printed root no longer exists. |
| Fixture blocks compile differently in a later standalone template (S2). | AC7 records the exact wrapper; S2 owns the real template and must re-assert the same failing line. |
| CI TeX subset too small (a package dependency missing, e.g. `chemgreek` for `mhchem`, LuaTeX format). | `CWIKI_REQUIRE_TEX=1` makes the probe fail the job; first CI run after push approval is the evidence. Locally only the MacTeX install is exercised. |
| BasicTeX cache restored but stale or `PATH` not set. | Probe in `make check` fails with named items under `CWIKI_REQUIRE_TEX=1`. |
| Package revalidation drifts from SPEC §0. | Step 5 reports changes to the user (AC20); SPEC untouched. |
| Closure table misses or duplicates a FEATURES M2 row. | `check_m2_closure.sh` checks structure only; the exactly-once mapping of the 28 rows is checked by `/t4:check` review against `docs/FEATURES.md`. |

## Telemetry

Status: **unavailable** — no delivery-scoped telemetry export exists for this delivery. Unknown values are shown as unknown, never as zero.

## Suggested MR description (draft)

**Title:** 0001 — Milestone 2 acceptance harness for rendered notes

````markdown
> Draft generated from local evidence by delivery-report. Review and edit before posting; nothing was published.

Delivery `d-20261011-ff71bd` — status **ready**.

Acceptance:
- AC1: passed
- AC2: passed
- AC3: passed
- AC4: passed
- AC5: passed
- AC6: passed
- AC7: passed
- AC8: passed
- AC9: passed
- AC10: passed
- AC11: passed
- AC12: passed
- AC13: passed
- AC14: passed
- AC15: passed
- AC16: passed
- AC17: passed
- AC18: passed
- AC19: passed
- AC20: passed
- AC21: passed

Verification run:
- `make support-test` — passed
- `sh tests/render/check_render_fixture.sh` — passed
- `sh tests/render/test_tex_probe.sh`, `make check`, `env CWIKI_REQUIRE_TEX=1 make check` — passed
- `sh tests/render/test_m2_demo.sh build/cwiki-m2-demo`, `make demo` — passed
- `env CWIKI_REQUIRE_TEX=1 make check` — passed
- `sh tests/docs/check_m2_closure.sh` — passed
- `make check` — passed
- `git diff --exit-code 152787c -- src tests/snapshots tests/fixtures/keys`, `make verify`, `env CWIKI_REQUIRE_TEX=1 make check`, `make demo` — passed
- `make review` — passed

Review: approve
````
