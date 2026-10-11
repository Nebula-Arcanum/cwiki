# Definition of done — cwiki

1. Applicable completion checks pass. The required full suite is `make verify` (strict build, all tests, then ASan/UBSan), run once at completion; run `make check` again after later edits. Run `make analyze` where `clang-tidy` is installed. State any unavailable check.
2. Changed behavior has meaningful regression coverage. Planned work uses `/t4:test` from the spec; quick work may add tests in the same session against its acceptance checklist. Documentation-only work uses relevant document checks.
3. Planned work updates the spec if behavior changed and ticks only verified plan steps. Quick work reports its acceptance checklist and verification; it does not require a spec or plan.
4. Review the actual change; no blocker remains. Quick changes may use a clearly labeled self-review unless independent review is required by the user/project. Planned work uses `/t4:check`.
5. Commit with the repository's existing conventional prefixes (`feat(scope):`, `fix:`, `test:`, `docs(spec):`, `build:`, `ci:`) as a local checkpoint after each verified task, staging explicit paths. There is no MR flow.
6. No new dependency without an ADR — write it with `/t4:adr`. Before first linking one, revalidate its FreeBSD, Arch and Homebrew packages and record the result in `docs/SPEC.md`.
7. cwiki is one binary with no service boundary; a change to an on-disk vault format or a shared
   internal interface needs the matching `docs/SPEC.md` requirement, and an ADR when it decides
   something the spec left open.
8. Where artifact contracts are enabled, `make -f ai-factory/make/ai.mk contracts DELIVERY=<id>` is
   valid and `/t4:report <id>` says `ready` before handoff; otherwise the MR states each remaining
   reason from the report.

9. Milestone completion is a separate, stricter gate owned by `docs/ACCEPTANCE.md`: every closure
   row verified or explicitly deferred, the production executable exercised in real kitty, a
   user-runnable fixture-vault demo, and the user's manual acceptance. Update `docs/ROADMAP.md`
   status with the work; a passing step is not a completed milestone.
