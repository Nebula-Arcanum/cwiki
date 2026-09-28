# Remind — Feature Inventory

Source: https://dianne.skoll.ca/projects/remind/ (project home) and the `remind(1)` man page (man.archlinux.org / Debian / Linux man pages / mankier mirrors), plus web search on SATISFY/ordinal-weekday recurrence patterns. Remind is the plain-text calendar/reminder *engine*; Wyrd (see wyrd.md) is only one of several front-ends (others: tkremind GUI, rem2ps/rem2pdf/rem2html renderers).

## Core Model
- **Plain-text rule language, not a database**: reminders are `REM` statements in `.rem` text files, hand-edited or generated; Remind recomputes trigger dates by re-evaluating rules each run rather than storing fixed occurrences.
- **Dual calendar support**: works with Gregorian and Hebrew calendars.
- **Multilingual**: documented support across roughly a dozen languages for date/message rendering.
- **Multiple back-end renderers**: same source files can be rendered as plain text (agenda), PostScript (`rem2ps`), PDF/SVG (`rem2pdf`), or HTML (`rem2html`); a Tk GUI front-end (`tkremind`) also exists alongside Wyrd and third-party iCalendar converters.

## Date/Recurrence Specification (REM command)
- **Partial-date matching**: any combination of day-of-month, month name (≥3 letters), year (1990–2075), and weekday(s) can be given; omitted components act as wildcards (e.g., `Mon` alone = every Monday, `Feb` alone = every day in February, no date at all = daily).
- **Multiple weekdays in one rule**: e.g. `Mon Tue Wed` matches any of those days.
- **ISO-style shorthand dates**: `YYYY-MM-DD` / `YYYY/MM/DD`.
- **Ordinal/relative phrasing sugar**: "First Monday April" ⇒ `Mon 1 April`; "Last Monday" ⇒ `Mon 1 --7`; "Lastday May" for month-end; filler word `IN` is accepted and ignored.
- **Nth-weekday-of-month recurrence via SATISFY**: no dedicated "2nd Tuesday" keyword; instead expressed as a base weekday rule filtered by a boolean expression, e.g. `REM Tue 1 SATISFY [wkdaynum($T)==2] MSG ...`, using expression-language date functions to test week-of-month.
- **Back/forward scanning modifiers**: single `-N`/`+N` move N days back/forward while respecting global OMITs; doubled `--N`/`++N` ignore OMITs.
- **Repeat modifier**: `*N` repeats every N days from a fully specified start date (e.g. `28 Oct 1992 *14`).
- **Timed reminders (AT clause)**: `AT HH:MM` (24h) or `AT H:MMam/pm`; optional trigger-delta in minutes (`AT 12:00 +45`) and repeat interval (`AT 12:00 +45 *30`) for recurring intra-day alerts.
- **DURATION**: attaches an event length to a timed reminder (`HH:MM`, minutes, or an expression), used by Wyrd/other front-ends to draw block sizes.
- **Expiration**: `UNTIL <date>` stops recurrence after a date; `THROUGH` is shorthand equivalent to `*1 +0 UNTIL`; supports ranges like `1992-11-30 THROUGH 1992-12-04`.
- **SCANFROM / FROM**: advanced directives controlling the date Remind begins internally scanning from, for optimizing/limiting rule evaluation (documented as for advanced users).
- **Repeat-from-command-line**: `*rep` on the command line re-runs Remind `rep` times with incrementing dates; `@trigger_expression` lets a date be computed via the expression language.

## Exceptions / Skipping (OMIT family)
- **Local OMIT clause**: per-reminder list of weekdays/dates to skip when computing back/delta offsets, unioned with any global OMITs (e.g. `REM 1 +1 OMIT Sat Sun`).
- **Global OMIT command**: declares recurring or one-off excluded dates file-wide — weekdays (`OMIT Sat Sun`), annual dates (`OMIT 1 Jan`), specific dates (`OMIT 7 Sep 1992`), or wrapping ranges (`OMIT 25 Dec THROUGH 4 Jan`); `OMIT DUMP` prints the active list.
- **SKIP / BEFORE / AFTER**: control how a reminder that lands on an omitted day behaves — skip it entirely, move it earlier, or move it later.
- **OMITFUNC**: a user-defined function (given a DATE, returning truthy if omitted) that overrides local/global OMIT lists for full programmatic control; must be paired with BEFORE or AFTER.
- **ADDOMIT**: automatically adds a reminder's own trigger date to the global OMIT list once triggered (e.g. for one-off holiday exceptions).
- **PUSH-OMIT-CONTEXT / CLEAR-OMIT-CONTEXT / POP-OMIT-CONTEXT**: stack-based save/reset/restore of the global OMIT list, letting included files temporarily change exceptions without affecting the parent scope.

## Conditional Triggering
- **SATISFY clause**: an arbitrary boolean expression re-tested against successive candidate dates until satisfied or an iteration cap is hit (default cap 150, tunable via `-x`/1000 in some versions); powers arbitrary custom recurrence logic (parity weeks, nth-weekday, moon-relative, etc.).
- **SCHED / WARN functions**: user-defined expression-language functions controlling, respectively, whether/when a timed reminder is queued for on-time activation and whether/when an advance-warning notice is issued.
- **IF / IFTRIG / ELSE / ENDIF**: file-level conditional blocks around groups of REM/other statements.
- **TODO reminders**: `TODO`, `COMPLETE-THROUGH`, and `MAX-OVERDUE` clauses mark a reminder as a persistent task rather than a one-off event — it keeps reappearing as overdue past its nominal date until marked complete or until the overdue cap is reached.

## Tags, Priority, Metadata
- **TAG**: up to 48-char identifier(s) (no whitespace/commas) attached to a reminder; multiple TAGs per REM allowed; `-y` CLI flag auto-synthesizes tags for reminders lacking one (used by front-ends like Wyrd to key on specific reminders, e.g. `TAG noweight`/`TAG nodisplay`).
- **PRIORITY**: integer 0–9999 (default 5000, overridable via `$DefaultPrio`), used to order same-time reminders in calendar/sorted output.
- **INFO fields**: free-form `Header: Value` metadata (standard headers include Location, Description, Url) that back-ends may interpret specially (e.g. mapping to calendar-app fields).
- **TZ clause**: attaches an IANA timezone name to a timed reminder so its AT time and OMIT evaluation are computed in that zone rather than the local system zone; `!` prefix suppresses zone-name validation warnings.
- **ONCE**: ensures a reminder fires only once per day, tracked via the reminder file's own last-access time or an explicit `$OnceFile`.
- **NOQUEUE**: includes a timed reminder's time in agenda output without queuing it for background/daemon activation (i.e., calendar-visible but silent).

## Reminder Body / Action Types
- **MSG**: default type — prints the message (after substitution-filter expansion) to stdout / to the `-k` handler command.
- **MSF**: same as MSG but reflowed as a formatted paragraph (indentation/width/sentence-spacing controlled by `$FirstIndent`, `$SubsIndent`, `$FormWidth`, `$EndSent(Ig)`).
- **RUN**: executes the (substituted) body as a shell command instead of printing it — the mechanism for firing arbitrary external programs/notifiers on trigger; disable-able globally (`-r` flag, or `RUN OFF` in-file).
- **CAL**: entry appears only in calendar-rendering modes (`-c`/`-s`/`-p`), not in plain agenda/queue output.
- **PS / PSFILE**: raw PostScript fragments/files consumed only by the `-p` PostScript calendar back-end.
- **SPECIAL**: opaque, back-end-defined payload type (`SPECIAL <subtype> ...`) that unrecognized renderers must ignore — an extension point for front-end-specific data (this is the mechanism Wyrd could plausibly reuse for its own custom hints).

## Triggering Mechanisms (queue/daemon/notification)
- **One-shot/agenda mode**: running `remind file` prints all reminders due "now" for scripted/cron-style use.
- **Queue mode (`-q` variants) / timed-reminder queuing**: timed (`AT`) reminders are queued in-process for activation at their specific time rather than only being listed.
- **Daemon mode (`-z[n]`)**: runs continuously, re-checking every n minutes (default 1) and firing due reminders — the standard way to get "background reminders" without a full calendar app open.
- **Server/JSON daemon mode (`-zj`)**: JSON-based IPC variant of daemon mode, intended for programmatic front-ends to subscribe to trigger events.
- **External notification hook (`-k cmd`)**: pipes each MSG reminder's text into an arbitrary external command (`%s` substituted with body) — the standard integration point for popups/notifications (e.g. piping to `gxmessage`, `notify-send`, a custom pipe, etc.); `-k: cmd` restricts this to only queued (timed) reminders.
- **Foreground vs background queuing (`-f`)**: forces queued reminders to run in the foreground process instead of forking, useful for supervised/embedded integration.
- **Purge mode (`-j[n]`)**: batch-removes expired reminders (useful for maintenance/automation, not interactive triggering).
- **Trigger-all override (`-t`)**: forces all non-expired reminders to fire regardless of their normal delta/advance-warning window (e.g. for a "show everything upcoming" report).

## File Organization / Includes
- **INCLUDE**: pull in another `.rem` file, path relative to the current working directory.
- **DO** (relative include): include a file relative to the directory of the file doing the including (portable multi-file setups).
- **SYSINCLUDE**: include from a fixed system directory (e.g. `/usr/share/remind`), for shared/system-wide definition files.
- **Directory-as-source**: passing a directory to Remind (or Wyrd) processes every `*.rem` file inside it, in sorted order.
- **Nesting limit**: includes may nest up to 8 levels deep.
- **INCLUDECMD**: runs a shell command and evaluates its stdout as if it were REM-file content — lets reminders be generated programmatically (e.g. from another data source); results are cached per unique command; `!` prefix disables RUN within the generated output for safety.
- **RETURN**: exits processing of the current file immediately (useful with conditionals for early-exit logic).

## Expression Language
- **Typed values**: INT, STRING (UTF-8, byte- and multibyte-aware variants), TIME (time-of-day or duration), DATE, DATETIME, with literal syntax for each (e.g. `'1993-02-22'`, `'2008-04-05@23:11'`, `4:30PM`).
- **Standard operators**: arithmetic, comparison, logical, ternary `?:`, string concatenation via `+`, bitwise ops, C-like precedence.
- **User-defined functions (FSET)**: reusable named functions in the expression language, usable inside SATISFY/OMITFUNC/SCHED/WARN and message bodies.
- **Built-in date/time functions** (representative set): `trigdate()`, `trigvalid()`, `isomitted(date)`, `today()`/`realtoday()`, `wkdaynum(date)`, `day()`/`month()`/`year()`, `hour()`/`minute()`, `date()`/`datetime()` constructors.
- **Astronomical functions**: `sunrise()`/`sunset()` and `moondate()` (moon-phase-relative date calculation) — enables reminders tied to sunrise/sunset or lunar phases directly in the rule language.
- **String utilities**: byte vs. multibyte-safe `index`/`substr`/`strlen` pairs (`mbindex`, `mbsubstr`, `mbstrlen`).
- **`shell(command)`**: runs a shell command and returns its output as a string for use inside an expression (disabled together with RUN by `-r`).
- **Translation lookup**: `_("text")` for localized message strings.
- **System/introspection variables**: `$Td`/`$Tm`/`$Ty`/`$Tw`/`$T`/`$Tt` (components of the current trigger date/time), `$RunOff`, `$DefaultPrio`, `$OnceFile`, plus formatting-related variables (`$FirstIndent`, `$FormWidth`, etc.) and terminal-capability hints (`$TerminalColors`, `$TerminalHyperlinks`).
- **EXPR ON/OFF**: can disable expression evaluation entirely (security/sandboxing knob, paired with `RUN ON/OFF`).

## Message Substitution / Formatting
- Rich set of `%`-escape codes for embedding computed values in MSG/MSF text: relative-day phrasing ("in 3 day's time", "tomorrow", "today"), absolute date in several formats (ISO, US, European, weekday+ordinal, etc.), time-until-trigger phrasing for timed reminders ("in 45 minutes", "2 hours ago"), pluralization helpers (`%p`/`%q`/`%s`), INFO-field interpolation (`%<Header>`), translation-table lookup, and capitalization/article-stripping variants of most codes.

## Command-Line Options (selected, engine-level)
- **Output/report modes**: `-n` (next occurrence, simple format), `-c`/`-s`/`-p` (calendar rendering: text/simple/PostScript), `-pp`/`-ppp` (JSON output variants), `--json` (JSON agenda output).
- **Filtering**: `--only-todos`, `--only-events`, `--hide-completed-todos`, `-o` (ignore ONCE), `-a` (skip today's already-past timed reminders).
- **Sorting**: `-g[a|d]` sort agenda output by date/time/priority, ascending/descending.
- **Security/sandboxing**: `-r` (disable RUN/shell()), `-u [+]name` (drop privileges / run as another user), `-+ username` (trust files owned by a given user, up to 20), `--max-execution-time=n`, `--max-expr-complexity=n` (resource-exhaustion guards on the expression evaluator — relevant if embedding Remind-like scripting in an untrusted context).
- **Variable/function injection from CLI**: `-i var=expr` / `-i func(args)=definition` sets or defines variables/functions without editing the file, useful for parameterized invocation from other tools.
- **Debug/tooling**: `-d` debug-mode flags, `--print-tokens` (parser token dump, aimed at syntax-highlighter authors), `--print-errs` (machine-readable error/translation pairs).

## Notable Design Properties Relevant to a Time-Blocking/Task App
- Recurrence, exceptions, and conditional logic are all one unified expression language rather than separate "recurrence rule" vs. "exception list" data structures — trades simplicity of a fixed RRULE-style grammar for full Turing-complete flexibility (functions, shell-outs, arbitrary predicates).
- TODO/overdue semantics (COMPLETE-THROUGH, MAX-OVERDUE) give a lightweight task-tracking layer on top of what is otherwise a pure calendar/alarm tool.
- Triggering is decoupled into three independent layers: raw rule evaluation (library-like, called repeatedly), a queueing/timing layer for "fire at HH:MM," and an actuation layer (`-k`, RUN, daemon) — any of which a new tool could adopt piecemeal without needing the whole daemon.

Sources consulted: https://dianne.skoll.ca/projects/remind/ ; https://man.archlinux.org/man/remind.1.en ; general web search on remind(1) man page mirrors (manpages.debian.org, linux.die.net, mankier.com) and SATISFY/wkdaynum recurrence usage.
