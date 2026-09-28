# Wyrd — Feature Inventory

Source: https://gitlab.com/wyrd-calendar/wyrd (cloned to research/wyrd/), README.md, doc/manual.mld, doc/wyrd.mld, doc/wyrdrc.mld, ChangeLog.

Wyrd is a terminal (ncurses) front-end to the Remind reminder engine, written in OCaml. It does not implement its own scheduling logic — it shells out to `remind` for all date/recurrence computation and just visualizes/edits the underlying REM text files.

## Identity / Concept
- **Thin front-end to Remind**: Wyrd has no scheduling engine of its own; it displays and edits plain-text `.rem` files and always defers actual date logic to the external `remind` binary (`remind_command` config var).
- **Low resource use**: explicitly positioned against "bloated" GUI calendar apps — fast startup, minimal footprint (tagline: "tired of waiting for your bloated calendar program to start up").
- **Name/etymology only, no literal "unfinished business" mechanic**: "Wyrd" is an Anglo-Saxon/Nordic word for fate/personal destiny; no code or doc reference to an "unfinished business" UI concept was found — it's just the project name.
- **File or directory as data source**: launches against a single reminders file (default `~/.reminders`) or a directory of `*.rem` files plus any `INCLUDE`d files.

## Layout / Views
- **Scrollable timetable (day schedule) window**: left pane, shows timed reminders in a vertical time grid; current time row highlighted in red if visible.
- **Duration-aware rendering**: reminders with a Remind `DURATION` are drawn as a block sized to that duration.
- **Overlap handling via indentation levels**: overlapping timed reminders render at up to 4 indentation levels so all remain at least partially visible (colorable as `timed_reminder1`–`4`).
- **Month mini-calendar**: upper-right pane; each day's color reflects reminder "busy level" (white→blue→magenta gradient); selected date cyan, today red.
- **Untimed reminders list**: lower-right pane, lists all-day/untimed reminders for the selected date.
- **Description window**: bottom pane, shows full `MSG` text of the currently selected reminder(s); independently scrollable.
- **Zoom levels**: hourly / half-hourly / quarter-hourly time-grid granularity, cyclic toggle (`z`), settable default via `default_zoom`.
- **Configurable pane width**: `untimed_window_width` controls month-calendar/untimed pane width.
- **12/24-hour clock toggles**: independent booleans for schedule, selection info, status bar, and description window time formatting.
- **Week numbering**: optional ISO-8601 week numbers in the month view; `week_starts_monday` controls first day of week.
- **Formatted calendar preview**: `view_week`/`view_month` pipe Remind's own formatted week/month calendar output into a pager.
- **Busy-level algorithm choice**: count of reminders per day, or total hours of reminders per day (`busy_algorithm`), with 4 configurable color thresholds (`busy_level1`–`4`).

## Navigation
- Scroll schedule up/down (arrows or `j`/`k`).
- Jump ±1 day (PageUp/PageDown, numpad 4/6, `<`/`>`, `H`/`L`).
- Jump ±1 week (numpad 8/2, `[`/`]`, `K`/`J`).
- Jump ±1 month (`{`/`}`).
- Jump to "now" (`<home>`), with optional `home_sticky` mode that keeps following current time until another nav key is pressed.
- Jump to next reminder (`<tab>`) — forward only, no "previous reminder" (limited by one-directional `remind -n`).
- Switch focus between timed/untimed panes (`<left>`/`<right>` or `h`/`l`).
- Go-to-date entry (`g` + date digits + return); accepts YYYYMMDD, MMDD, or DD, with `goto_big_endian` toggling ISO vs. DD/MM/YYYY parsing.
- Numpad and vi-style (`HJKL`) directional movement also works in the month calendar.
- Full keybinding list viewable in-app via `?` (piped to pager).
- Manual screen refresh (`Ctrl-L`) for corrupted terminal redraw.
- Quit with `Q`.

## Creating / Editing Reminders
- **Direct-to-editor creation**: selecting a timeslot and pressing `t` (timed) or `u` (untimed) opens `$EDITOR` with a pre-filled `REM` template (date/time already filled in) appended to the reminders file, cursor placed for entry.
- **Recurring-template shortcuts**: `w`/`W` create weekly timed/untimed reminders, `m`/`M` create monthly timed/untimed reminders — same editor-template mechanism with different base recurrence.
- **File-selection dialog variants**: `T`/`U` do the same as `t`/`u` but first prompt to choose which reminder file (from `INCLUDE`d files or `*.rem` directory) to append to.
- **Edit existing reminder**: selecting a reminder and pressing `<return>` opens the editor at the exact file/line of its `REM` statement.
- **Disambiguation dialog**: if a timeslot has multiple overlapping reminders, a selection dialog lets the user pick which one to edit.
- **Blank-slot quick create**: pressing `<enter>` on an empty slot starts a new timed/untimed reminder depending on which pane is focused.
- **Generic file edit**: `e` opens the reminders file in the editor with no specific line targeted.
- **User-defined templates**: up to 10 custom templates (`template0`–`template9`) bindable to keys via `new_templateN` / `new_templateN_dialog` operations, each with its own field-substitution string.
- **Template placeholder substitutions**: `%monname%`, `%mon%`/`%0mon%`, `%mday%`/`%0mday%`, `%year%`, `%hour%`, `%min%`, `%wdayname%`, `%wday%` available in `timed_template`/`untimed_template`/`templateN`.
- **Editor cursor-jump workflow tip**: documented pattern using Vim-LaTeX's `<++>` placeholder markers so Ctrl-J jumps between templated fields (e.g. delta, message) after generation.
- **Pluggable edit commands**: `edit_old_command`/`edit_new_command`/`edit_any_command` config vars let the user fully customize the shell command used, with `%file%`/`%line%` substitution — not hardwired to `$EDITOR`.

## Quick / Natural-Language Entry
- **Quick reminder mode** (`q`): free-text natural-language entry (e.g. "meeting with Bob tomorrow at 11", "drop off package at 3pm", "Wednesday 10am-11:30 go grocery shopping") parsed into a `REM` line and immediately created; view auto-scrolls to the new reminder's location.
- **English-centric parser**: documented as currently biased toward US English date/time conventions.
- **CLI quick-add**: `wyrd --add "text"` / `wyrd -a "text"` creates a reminder from the same natural-language parser without launching the UI; exits 0 on success, nonzero with an error message on unparseable input — usable from scripts/other automation.

## Clipboard (Cut/Copy/Paste)
- **Cut** (`X`): deletes the selected reminder's `REM` line from its file and stores it in an internal clipboard.
- **Copy** (`y`): same as cut but leaves the original in place.
- **Paste** (`p`): appends the clipboard reminder as a new `REM` line at the selected date/time (preserving original `DURATION` for timed reminders) and opens the editor there.
- **Paste-to-file dialog** (`P`): same as paste but first prompts for which reminder file to paste into.
- **Known limitation**: cut only removes the single `REM` line responsible for the occurrence; it does not follow more complex Remind scripting logic (e.g., macro-generated reminders), and `delta`/`tdelta` settings are not preserved across copy/paste.

## Viewing / Reporting
- View all reminders for the selected date (`r`) or all non-expired reminders on/after it (`R`), rendered via a pager.
- View Remind's own formatted one-week (`c`) or one-month (`C`) calendar output for the selected date, in a pager.
- Pager command is configurable (`pager_command`, e.g. force `less -c` for full repaint instead of scroll).

## Search
- Incremental regex search (`/`) over reminder `MSG` text, case-insensitive, Emacs-compatible regex syntax.
- Repeat last search forward (`n`).
- Cancel search entry (`<esc>` or `Ctrl-g`).
- Forward-only limitation: no backward search or "previous reminder" jump, because it's implemented via one-directional `remind -n` invocation.

## Alarms / External Triggering
- Wyrd itself has no built-in alarm/popup system; it delegates entirely to Remind's own trigger mechanisms.
- Documented pattern: run `remind -z -k'gxmessage -title "reminder" %s &' ~/.reminders &` as a background daemon to pop up alarms when reminders trigger (typically launched from `~/.xinitrc`).
- Advance warning: setting a Remind `tdelta` (e.g. `AT 14:30 +15`) triggers early for a heads-up.
- Alternative: use Remind's `RUN` command per-reminder to fire an arbitrary program, optionally generated via Wyrd's `templateN` mechanism.

## Reminder Tagging (Wyrd-specific semantics)
- `TAG noweight`: reminder is displayed normally but excluded from the month-view "busy level" weighting.
- `TAG nodisplay`: reminder is hidden entirely and excluded from busy-level weighting.
- Tag matching is case-insensitive; explicitly noted as not guaranteed compatible with other Remind front-ends (e.g. tkremind).

## Configuration System (`~/.wyrdrc`)
- Muttrc-like config syntax: `set var="value"`, `bind key operation`, `unbind key`, `color object fg bg`, `include "file"`.
- Full rebindable keymap: every calendar action (scroll, day/week/month nav, goto, zoom, edit, quick_add, new_timed/untimed (+dialog variants), new_templateN (+dialog), copy/cut/paste (+dialog), switch_window, search ops, next_reminder, view_remind(_all), view_week/month, refresh, quit, entry_complete/backspace/cancel) is a named "operation" bindable to arbitrary keys, including Ctrl/Meta modifiers and octal keycodes for uncommon terminal keys.
- Fully customizable color scheme across ~17 distinct UI elements (help bar, empty/current/overlap-level timeslots, untimed entries, date strip, selection info, description pane, status bar, calendar labels/activity levels/today marker, dividers), using 8 basic terminal colors + "default"/transparent.
- Config toggles: 12/24-hour displays (4 independent contexts), Monday-first week, ISO week numbers, cursor-centered vs. scrolling schedule, home-key "sticky" follow mode, big-endian vs little-endian goto/quick-add date parsing, advance-warning display, untimed-pane width, bold untimed text, terminal color rendering mode (`reminder_colors`: auto/true/false, needs `ccc` termcap support).
- Can run without any `~/.wyrdrc` present (uses system default at `/etc/wyrdrc` or built-in defaults; supported since 1.6).

## CLI / Integration
- `wyrd [OPTIONS] [FILE]` — FILE may be a single reminders file or a directory of `.rem` files.
- `--version`, `--help`, `--showrc` (print resolved config file path), `--add`/`-a EVENT` (quick natural-language add, see above).
- Respects `$EDITOR` (default `vi`) and `$PAGER` (default `less`) environment variables.

## Recent engine-facing improvements (from ChangeLog, relevant to integration)
- Uses Remind's JSON output mode internally (needs Remind ≥ 4.0.0 + yojson) rather than parsing plain text.
- Supports Remind's colorized output pass-through (`reminder_colors` setting).
- Supports arbitrary terminal sizes and multi-line reminder descriptions.
