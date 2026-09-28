# calcurse feature inventory

Source: calcurse manual (calcurse.org/files/manual.html), calcurse-caldav manual, GitHub (lfos/calcurse).

## Calendar views

- Monthly view: default view, full month grid, navigate/highlight a day to see its items in the appointment panel.
- Weekly view: 7 days shown, each divided into six 4-hour time slices; a slice is colored differently if an appointment falls in it; shows ISO week number.
- View toggle: single key (TAB-like) switches calendar panel between month/week view (`appearance.calendarview` config: 0=monthly, else weekly).
- Moon phase indicator: shows current lunar phase as a symbol (`|)`, `(|)`, `(|`, `|`) in the appointment panel.
- Day/date navigation with vim-style movement (h/j/k/l analogues); numeric prefix repeats movement (e.g. `10k` moves 10 weeks).

## Appointment / event model

- Appointments: timed items with start time + duration/end time, a one-line description, and optional attached note.
- Events: untimed (all-day) items, same description/note attachment as appointments, no start/end time.
- "Important"/flagged marker: an item can be flagged, which controls whether it triggers notifications (see notification.notifyall).
- Items addressed internally by a hash (SHA1-ish digest of content) rather than a stable ID — used for note linking, CLI filtering, export UID.

## Recurrence

- RRULE-based recurrence (RFC 2445/5545 subset) for both appointments and events.
- Supported frequencies: DAILY, WEEKLY, MONTHLY, YEARLY, with an interval and either an UNTIL date or a COUNT.
- EXDATE supported (exclude specific occurrence dates from a recurring item).
- Not supported: SECONDLY/MINUTELY/HOURLY frequencies, all BYxxx qualifiers (BYSECOND/BYMINUTE/BYHOUR/BYDAY/BYMONTHDAY/BYYEARDAY/BYWEEKNO/BYMONTH/BYSETPOS), WKST, and EXRULE.
- Recurring items get their own format-string specifiers (`--format-recur-apt`, `--format-recur-event`) distinct from one-off items.

## Todo / task model

- Flat todo list, not tied to a calendar date (pure task list, no due-date field in the base model).
- Priority: integer 1 (highest) to 9 (lowest); priority 0 reserved for completed items and shown/filtered separately.
- Completion: simple done/not-done toggle (no partial progress); `--filter-completed` / `--filter-uncompleted` restrict CLI output to one state.
- Todos support attached notes and description text like appointments/events.
- iCalendar import maps VTODO PRIORITY/SUMMARY/DESCRIPTION/VALARM into calcurse todos (no due-date/DTSTART mapping mentioned).

## Notes

- Each note is a separate plain-text file under `~/.calcurse/notes/`, named by a SHA1 hash of... the reference, so identical notes can be shared/deduped across multiple items.
- Notes attach to appointments, events, or todos; opened/edited via `$VISUAL`/`$EDITOR`, viewed via `$PAGER` (default `/usr/bin/less`).
- Format specifiers `%n` (note filename) and `%N` (note content) expose notes in non-interactive output/export.
- Garbage collection: `-g/--gc` CLI flag (or `general.autogc` config) deletes note files no longer referenced by any item.

## TUI layout and panels

- Three fixed panels: Appointment panel (items for the selected day), Calendar panel (month/week grid), Todo panel (task list).
- 8 selectable panel arrangements (`appearance.layout` = 0-7) changing which panel is where and how big.
- `appearance.sidebarwidth`: sidebar width as a percentage, adjustable live.
- `appearance.compactpanels`: strips panel captions/borders for a denser look.
- Notify-bar: persistent status line showing current date/time, active calendar file name, and the next appointment within 24h with a live countdown; blinks when a flagged appointment's warning window is active.
- Status bar: context-sensitive line listing currently available keybindings/actions.
- Built-in line editor for text input fields with Emacs-style editing keys (^a/^b/^d/^e/^f/^h/^k/^w) and horizontal-scroll indicators.
- Color/theme configuration menu: pick fg/bg colors per UI element (borders, titles, keystrokes, status text) with live preview; supports terminal-default and a no-color/black-and-white theme.

## Keybindings philosophy

- Defaults modeled on vim conventions (directional keys, modal command feel).
- Every action is rebindable from an in-app configuration menu (`C`); an action can have multiple bound keys.
- Binding a key already in use is rejected and the user is prompted to pick a different key (no silent override/conflict).
- Bindable key vocabulary: letters (case-sensitive), digits, Ctrl-combinations, Escape, Tab, Space, arrow keys, Home, End.
- Numeric-prefix repeat count for movement commands (vim-like `10k`).
- In-app contextual help: `?` opens a pager with docs; `:help <feature>` / `:help <key>` jumps to a specific topic.

## Import / export

- Import: iCalendar (RFC 2445) files via `-i/--import`. Recognized VEVENT fields: DTSTART, DTEND, DURATION, RRULE, EXDATE, VALARM, SUMMARY, DESCRIPTION. Recognized VTODO fields: PRIORITY, VALARM, SUMMARY, DESCRIPTION. DESCRIPTION becomes an attached note; VALARM marks the item "important". Negative durations dropped, timezones ignored, unsupported RRULEs cause the item to be skipped.
- Export: `-x/--export` to iCalendar (default) or pcal format; `--export-uid` adds a UID property derived from the item hash.
- User-defined output formatting via printf-style format strings per item type (`--format-apt`, `--format-event`, `--format-todo`, `--format-recur-apt`, `--format-recur-event`), plus an extended `%(...)` strftime-like syntax for custom date rendering and duration formatting.
- CalDAV sync via separate companion script `calcurse-caldav` (Python, contrib tool, not the core binary): two-way, keep-local, or keep-remote initial sync modes; keeps a local snapshot db (`~/.calcurse/caldav/sync.db`) to diff local vs. remote state each run; supports basic auth and OAuth2 (e.g. Google Calendar); has its own `pre-sync`/`post-sync` hook directory; since calcurse items have no stable ID, an edited item is synced as delete+create rather than an update; meant to be re-run manually or via cron, no built-in scheduler.

## Notification / reminder mechanism

- `notification.warning` (default 300s): how far ahead of a flagged item's start time the notify-bar starts blinking and the notification command fires.
- `notification.command` (default `printf \a`): shell command run on warning trigger, executed via `$SHELL` (or `/bin/sh`); can be pointed at any external notifier (e.g. piping `calcurse --next` output to a mail/notify tool).
- `notification.notifyall`: controls which items can trigger notifications — `flagged-only` (default, only "important"-marked items), `unflagged-only`, or `all`.
- Daemon mode (`daemon.enable`): after the interactive UI exits, calcurse can keep running in the background solely to watch for upcoming appointments and fire `notification.command` — this is how reminders work without the TUI open.
- `daemon.log`: optional timestamped log of daemon activity (checks performed, commands launched) to a `daemon.log` file.
- CLI daemon control: `--daemon` starts it explicitly; `--status` reports whether it's running and its PID.
- Notify-bar itself (interactive mode) always shows a live countdown to the next appointment within 24h regardless of the daemon.

## Configuration

- Single flat key=value config file (`~/.calcurse/conf`), edited either directly or via in-app config menu, organized into sections: general, appearance, format, notification.
- Notable general options: `autosave`, `autogc`, `periodicsave` (minutes), `confirmquit`, `confirmdelete`, `systemdialogs`, `progressbar`, `firstdayofweek`.
- Notable appearance options: `defaultpanel` (startup panel), `compactpanels`, `calendarview` (month/week), `layout` (0-7), `sidebarwidth`, `notifybar`.
- Notable format options: `outputdate`, `inputdate` (4 selectable date-entry orders), `notifydate`, `notifytime` — all strftime-based.
- Separate `keys` file stores keybinding customizations independent of `conf`.
- `-D <dir>` CLI flag to point at an alternate data directory (portable/multiple profiles).
- Locale/i18n via standard `LC_ALL`.

## Data storage format

- Plain-text data directory, default `~/.calcurse/`: `conf` (settings), `keys` (bindings), `apts` (appointments + events), `todo` (todo list), `notes/` (one file per note, SHA1-named), optional `daemon.log`.
- No database engine — everything is line-oriented plain text, editable by hand (manual doesn't give exact line syntax, but confirms plain flat files, not binary/SQLite).
- Garbage collection (`-g`/autogc) reconciles the `notes/` directory against references from `apts`/`todo`.

## Hooks / scripting

- Core hook directory `~/.calcurse/hooks/` with 4 lifecycle points: `pre-load`, `post-load`, `pre-save`, `post-save` — executable scripts run automatically around loading/saving data files (letting users script backups, git commits, external sync, etc.). Sample hooks shipped in `contrib/hooks/`.
- calcurse-caldav has its own separate hook directory `~/.calcurse/caldav/hooks/` with `pre-sync`/`post-sync` points.
- Non-interactive CLI itself is heavily scriptable: query mode (`-Q`/`--query`) plus a rich set of `--filter-*` flags (type mask, regex on description, date range, priority, completion state, hash include/exclude) let scripts pull/filter agenda data; `-G/--grep` dumps items in the native data format after filtering; `-F/--filter` rewrites the data files in place after filtering (destructive, manual explicitly warns to be careful).

## Non-interactive CLI surface

- Flags for one-shot output: `-a` (today's appointments), `-d <date|n>` (a date or next n days), `-n` (next appointment within 24h + countdown), `-t[n]` (todo list, optional priority filter), `-r[n]` (range of n days), `-s[date]` (from a date forward).
- All of the above are meant for scripting/status-bar integration (e.g. piping into tmux/dwm status lines) rather than interactive use.
