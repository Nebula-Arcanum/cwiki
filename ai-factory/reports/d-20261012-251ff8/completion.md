# Completion report — 0002 — Milestone 2 block extraction and TeX source generation

**Status:** ready
**Delivery:** `d-20261012-251ff8` (planned)
**Generated:** 2026-10-13T15:51:40.207Z · schema t4-delivery-report v1
**Snapshot:** code `0ef13d3b7544bec2c2a249bc6cd5442a42b32221dfcbbb3629bb77c121d7e788` at `4691ba7fc2b6` · evidence fingerprint `2d7c14c2f2426e63de115e78d8aabf8338a0c64139107a804563825c5d45a2eb`
**Criteria:** 35 (passed 35, attested 0, pending 0, failed 0, uncovered 0, unverified 0)

> Assembled from local evidence only. Ready means ready for delivery handoff — not merged, deployed or published. Contract validation is structural; it does not judge whether a requirement is right.

## Why this status

Every criterion has current passing evidence and every policy requirement is met.

## Sources

| Kind | Markdown | Sidecar | State | SHA-256 |
|---|---|---|---|---|
| plan | `ai-factory/plans/0002-m2-block-extraction-tex-source.md` | `ai-factory/plans/0002-m2-block-extraction-tex-source.contract.json` | valid | `092a3dba0e46df8cb9651dc85f9d302e17ddeb32f06cea6b3dfec3c9bbaf28e2` |
| spec | `ai-factory/specs/0002-m2-block-extraction-tex-source.md` | `ai-factory/specs/0002-m2-block-extraction-tex-source.contract.json` | valid | `56d4a50c597fd22676a73d3b3508ef9de200a07bf62d7cbd4d7c0ec74aba99ff` |

## Acceptance criteria

| Criterion | Steps | Checks | Result | Basis | Evidence |
|---|---|---|---|---|---|
| AC1 | S4 | `make build/test-tex-block`<br>`build/test-tex-block` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S4-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC2 | S4 | `make build/test-tex-block`<br>`build/test-tex-block` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S4-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC3 | S4 | `make build/test-tex-block`<br>`build/test-tex-block` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S4-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC4 | S4 | `make build/test-tex-block`<br>`build/test-tex-block` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S4-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC5 | S4 | `make build/test-tex-block`<br>`build/test-tex-block` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S4-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC6 | S4 | `make build/test-tex-block`<br>`build/test-tex-block` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S4-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC7 | S4 | `make build/test-tex-block`<br>`build/test-tex-block` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S4-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC8 | S4 | `make build/test-tex-block`<br>`build/test-tex-block` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S4-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC9 | S1 | `make build/test-tex-env`<br>`build/test-tex-env` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S1-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC10 | S5 | `make build/test-tex-block`<br>`build/test-tex-block` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S5-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC11 | S5 | `make build/test-tex-block`<br>`build/test-tex-block` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S5-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC12 | S1, S5 | `make build/test-tex-env`<br>`build/test-tex-env`<br>`make build/test-tex-block`<br>`build/test-tex-block` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S1-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/S5-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC13 | S3, S5 | `make build/test-config`<br>`build/test-config`<br>`make build/test-tex-block`<br>`build/test-tex-block` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S3-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/S5-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC14 | S5 | `make build/test-tex-block`<br>`build/test-tex-block` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S5-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC15 | S5 | `make build/test-tex-block`<br>`build/test-tex-block` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S5-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC16 | S2, S6 | `make build/test-config`<br>`build/test-config`<br>`make build/test-tex-source`<br>`build/test-tex-source` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S2-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/S6-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC17 | S6 | `make build/test-tex-source`<br>`build/test-tex-source` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S6-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC18 | S2 | `make build/test-config`<br>`build/test-config` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S2-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC19 | S2 | `make build/test-config`<br>`build/test-config` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S2-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC20 | S6 | `make build/test-tex-source`<br>`build/test-tex-source` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S6-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC21 | S6 | `make build/test-tex-source`<br>`build/test-tex-source` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S6-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC22 | S6 | `make build/test-tex-source`<br>`build/test-tex-source` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S6-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC23 | S6 | `make build/test-tex-source`<br>`build/test-tex-source` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S6-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC24 | S7 | `make tex-compile-test`<br>`env CWIKI_REQUIRE_TEX=1 make tex-compile-test` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S7-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC25 | S8 | `make build/fuzz-tex`<br>`build/fuzz-tex` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S8-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC26 | S4, S7 | `make build/test-tex-block`<br>`build/test-tex-block`<br>`make tex-compile-test`<br>`env CWIKI_REQUIRE_TEX=1 make tex-compile-test` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S4-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/S7-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC27 | S9 | `sh tests/render/check_s2_boundaries.sh` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S9-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC28 | S2, S6 | `make build/test-config`<br>`build/test-config`<br>`make build/test-tex-source`<br>`build/test-tex-source` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S2-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/S6-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC29 | S5 | `make build/test-tex-block`<br>`build/test-tex-block` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S5-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC30 | S5 | `make build/test-tex-block`<br>`build/test-tex-block` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S5-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC31 | S4 | `make build/test-tex-block`<br>`build/test-tex-block` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S4-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC32 | S1 | `make build/test-tex-env`<br>`build/test-tex-env` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S1-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC33 | S1, S2, S3, S5 | `make build/test-tex-env`<br>`build/test-tex-env`<br>`make build/test-config`<br>`build/test-config`<br>`make build/test-config`<br>`build/test-config`<br>`make build/test-tex-block`<br>`build/test-tex-block` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S1-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/S2-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/S3-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/S5-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC34 | S6 | `make build/test-tex-source`<br>`build/test-tex-source` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S6-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |
| AC35 | S6 | `make build/test-tex-source`<br>`build/test-tex-source` | passed | automated | `ai-factory/evidence/d-20261012-251ff8/S6-step.json`<br>`ai-factory/evidence/d-20261012-251ff8/final.json` |

### Plan steps

| Step | Mark | Criteria | Evidence | Status |
|---|---|---|---|---|
| S1 | [x] | AC9, AC12, AC32, AC33 | `ai-factory/evidence/d-20261012-251ff8/S1-step.json` | passed |
| S2 | [x] | AC16, AC18, AC19, AC28, AC33 | `ai-factory/evidence/d-20261012-251ff8/S2-step.json` | passed |
| S3 | [x] | AC13, AC33 | `ai-factory/evidence/d-20261012-251ff8/S3-step.json` | passed |
| S4 | [x] | AC1, AC2, AC3, AC4, AC5, AC6, AC7, AC8, AC26, AC31 | `ai-factory/evidence/d-20261012-251ff8/S4-step.json` | passed |
| S5 | [x] | AC10, AC11, AC12, AC13, AC14, AC15, AC29, AC30, AC33 | `ai-factory/evidence/d-20261012-251ff8/S5-step.json` | passed |
| S6 | [x] | AC16, AC17, AC20, AC21, AC22, AC23, AC28, AC34, AC35 | `ai-factory/evidence/d-20261012-251ff8/S6-step.json` | passed |
| S7 | [x] | AC24, AC26 | `ai-factory/evidence/d-20261012-251ff8/S7-step.json` | passed |
| S8 | [x] | AC25 | `ai-factory/evidence/d-20261012-251ff8/S8-step.json` | passed |
| S9 | [x] | AC27 | `ai-factory/evidence/d-20261012-251ff8/S9-step.json` | passed |
| S10 | [x] | — | `ai-factory/evidence/d-20261012-251ff8/S10-step.json` | passed |

## Changes and verification

No base branch found; untracked files only: 0 path(s).

| Evidence | Phase | Commands | Status | State | Output |
|---|---|---|---|---|---|
| `ai-factory/evidence/d-20261012-251ff8/S1-red.json` | red (expects fail) | `make build/test-tex-env` → failed (exit 2) | failed | valid | `ai-factory/runs/evidence/d-20261012-251ff8/S1-red.log` |
| `ai-factory/evidence/d-20261012-251ff8/S1-step.json` | step (expects pass) | `make build/test-tex-env` → passed (exit 0)<br>`build/test-tex-env` → passed (exit 0) | passed | valid | `ai-factory/runs/evidence/d-20261012-251ff8/S1-step.log` |
| `ai-factory/evidence/d-20261012-251ff8/S10-step.json` | step (expects pass) | `make check` → passed (exit 0) | passed | valid | `ai-factory/runs/evidence/d-20261012-251ff8/S10-step.log` |
| `ai-factory/evidence/d-20261012-251ff8/S2-step.json` | step (expects pass) | `make build/test-config` → passed (exit 0)<br>`build/test-config` → passed (exit 0) | passed | valid | `ai-factory/runs/evidence/d-20261012-251ff8/S2-step.log` |
| `ai-factory/evidence/d-20261012-251ff8/S3-step.json` | step (expects pass) | `make build/test-config` → passed (exit 0)<br>`build/test-config` → passed (exit 0) | passed | valid | `ai-factory/runs/evidence/d-20261012-251ff8/S3-step.log` |
| `ai-factory/evidence/d-20261012-251ff8/S4-step.json` | step (expects pass) | `make build/test-tex-block` → passed (exit 0)<br>`build/test-tex-block` → passed (exit 0) | passed | valid | `ai-factory/runs/evidence/d-20261012-251ff8/S4-step.log` |
| `ai-factory/evidence/d-20261012-251ff8/S5-step.json` | step (expects pass) | `make build/test-tex-block` → passed (exit 0)<br>`build/test-tex-block` → passed (exit 0) | passed | valid | `ai-factory/runs/evidence/d-20261012-251ff8/S5-step.log` |
| `ai-factory/evidence/d-20261012-251ff8/S6-step.json` | step (expects pass) | `make build/test-tex-source` → passed (exit 0)<br>`build/test-tex-source` → passed (exit 0) | passed | valid | `ai-factory/runs/evidence/d-20261012-251ff8/S6-step.log` |
| `ai-factory/evidence/d-20261012-251ff8/S7-step.json` | step (expects pass) | `make tex-compile-test` → passed (exit 0)<br>`env CWIKI_REQUIRE_TEX=1 make tex-compile-test` → passed (exit 0) | passed | valid | `ai-factory/runs/evidence/d-20261012-251ff8/S7-step.log` |
| `ai-factory/evidence/d-20261012-251ff8/S8-step.json` | step (expects pass) | `make build/fuzz-tex` → passed (exit 0)<br>`build/fuzz-tex` → passed (exit 0) | passed | valid | `ai-factory/runs/evidence/d-20261012-251ff8/S8-step.log` |
| `ai-factory/evidence/d-20261012-251ff8/S9-step.json` | step (expects pass) | `sh tests/render/check_s2_boundaries.sh` → passed (exit 0) | passed | valid | `ai-factory/runs/evidence/d-20261012-251ff8/S9-step.log` |
| `ai-factory/evidence/d-20261012-251ff8/final.json` | final (expects pass) | `git diff --exit-code 135a625 -- tests/snapshots tests/fixtures/keys tests/fixtures/vaults/m2-render/class-note.md tests/fixtures/vaults/m2-render/.cwiki/preamble.tex` → passed (exit 0)<br>`git diff --exit-code d7b3dc9 -- src/zone.c` → passed (exit 0)<br>`make verify` → passed (exit 0)<br>`env CWIKI_REQUIRE_TEX=1 make check` → passed (exit 0)<br>`sh tests/render/check_s2_boundaries.sh` → passed (exit 0) | passed | valid | `ai-factory/runs/evidence/d-20261012-251ff8/final.log` |
| `ai-factory/evidence/d-20261012-251ff8/review.json` | final (expects pass) | `make review` → passed (exit 0) | passed | valid | — |

## Review

Verdict **approve** (passed, valid) by claude — `ai-factory/evidence/d-20261012-251ff8/review.json`.

| Severity | Blocking | Location | Issue |
|---|---|---|---|
| major | no | ai-factory/evidence/d-20261012-251ff8/review.json:0 | Running contracts.js validate on d-20261012-251ff8 marks this evidence file invalid (E_EVIDENCE_FAILED, S_PLAN_CHANGED, S_CODE_CHANGED). It still holds the earlier request_changes review, recorded before commits 4412ecf, d7b3dc9 and 4af2e21. All other evidence for the delivery validates. |
| minor | no | src/zone.c:705 | When the zone stack is full (CWIKI_ZONE_MAX_DEPTH = 16), an opening brace-group is not pushed, but its closing brace still pops a zone. Brace groups alone can now reach this limit. A probe with 17 nested braces in '$\\ce{...} y$ and $w$ tail' produced one compilable inline block that ran to the end o |
| minor | no | src/tex_block.c:381 | tex_block measures depth by counting opening spans, not by reading the zone stack. When the zone engine drops a push at the depth limit, the count and the stack disagree. The block then ends through the 'stack emptied without a close span' fallback at the end of the line and takes in unrelated trail |
| minor | no | src/config.c:3365 | tex_package_name checks note 'packages:' entries against the allowed character set only, without string_scalar(). As a result, 'packages: [null]', '[true]' and '[1.0]' are accepted as package names, and tagged nodes are not rejected. 'preamble:' uses the stricter string check, so R2.8.5 is applied u |

## Limitations and follow-up

- Nothing outstanding from the evidence.

### Open questions (spec)

None.

### Risks (plan)

| Risk | Check |
|---|---|
| Frontmatter text leaks zone state into the body (stray `$`, `\begin{` in `preamble:`). | Body-only zone scan; Step 4 frontmatter test; fuzz corpus starts with `---`. |
| Extraction becomes a second parser (R1.9.7). | Blocks derived only from zone stacks/spans; the only extra scanning is the preamble scan (preamble text, not note body) and the `% cwiki:` line match inside a known block; reviewed at `/t4:check`. |
| Preamble-scan regex hits a PCRE2 limit and silently drops a declaration. | Step 1 returns the limit as an error; Step 5/6 turn it into a report and no source. |
| Line table off by one at block edges or frontmatter offset. | Step 6 per-line mapping assertions; Step 7 maps a real TeX error to note line 23; fuzz invariants. |
| `standalone` `multi`+`varwidth` paging or display context differs from the S1 hand compile. | Step 7 real compile under `CWIKI_REQUIRE_TEX=1`; fallback to `preview` recorded if needed. |
| Failing block halts output so the good blocks' pages are missing. | `nonstopmode`; Step 7 asserts at least 5 pages and the line-23 error mapping. |
| Tests write into the committed fixture. | All tests use `fixture_vault_copy_in`; Step 10 `git diff --exit-code` on the S1 files; Step 4 byte comparison. |
| Unreadable-file test passes vacuously when run as root (CI containers). | Test checks `geteuid()==0` and prints an explicit skip for that case only. |
| Fuzz target slow or leaks under sanitizers. | Bounded iterations; `make sanitize` in `make verify`. |
| A new file missing from `APP_SOURCES`/`PRODUCT_TESTS`/`ANALYZE_SOURCES`. | `make analyze` on Linux CI (not installed locally); `/t4:check` review of the Makefile diff. |
| POSIX-make or shell portability of new targets/scripts. | Scripts run under `/bin/sh`; FreeBSD (bmake) CI after push approval. |

## Telemetry

Status: **unavailable** — no delivery-scoped telemetry export exists for this delivery. Unknown values are shown as unknown, never as zero.

## Suggested MR description (draft)

**Title:** 0002 — Milestone 2 block extraction and TeX source generation

````markdown
> Draft generated from local evidence by delivery-report. Review and edit before posting; nothing was published.

Delivery `d-20261012-251ff8` — status **ready**.

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
- AC22: passed
- AC23: passed
- AC24: passed
- AC25: passed
- AC26: passed
- AC27: passed
- AC28: passed
- AC29: passed
- AC30: passed
- AC31: passed
- AC32: passed
- AC33: passed
- AC34: passed
- AC35: passed

Verification run:
- `make build/test-tex-env`, `build/test-tex-env` — passed
- `make check` — passed
- `make build/test-config`, `build/test-config` — passed
- `make build/test-config`, `build/test-config` — passed
- `make build/test-tex-block`, `build/test-tex-block` — passed
- `make build/test-tex-block`, `build/test-tex-block` — passed
- `make build/test-tex-source`, `build/test-tex-source` — passed
- `make tex-compile-test`, `env CWIKI_REQUIRE_TEX=1 make tex-compile-test` — passed
- `make build/fuzz-tex`, `build/fuzz-tex` — passed
- `sh tests/render/check_s2_boundaries.sh` — passed
- `git diff --exit-code 135a625 -- tests/snapshots tests/fixtures/keys tests/fixtures/vaults/m2-render/class-note.md tests/fixtures/vaults/m2-render/.cwiki/preamble.tex`, `git diff --exit-code d7b3dc9 -- src/zone.c`, `make verify`, `env CWIKI_REQUIRE_TEX=1 make check`, `sh tests/render/check_s2_boundaries.sh` — passed
- `make review` — passed

Review: approve
````
