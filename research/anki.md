# Anki Feature Inventory

Research pass on Anki's flashcard/spaced-repetition features, sourced from the official manual (docs.ankiweb.net). Flat bullet list grouped by area, for later merge into the master feature table.

## Scheduling Algorithm (SM-2 style / legacy)

- **Learning steps**: New cards go through one or more short intra-day delays (e.g. minutes), configured as a space-separated list; each successful "Good" answer advances to the next step, "Again" restarts the steps.
- **Graduating interval**: Once the final learning step is passed, the card "graduates" to review status with a configurable interval (days) until it's shown again.
- **Easy interval**: A separate, typically longer interval used when a learning card is answered "Easy," graduating it immediately.
- **Ease factor**: A per-card percentage-like multiplier (default starting value 2.50 / 250%) that governs how much a review interval grows on a correct ("Good") answer; adjusted up/down over time based on performance.
- **Interval growth**: On review, new interval ≈ previous interval × ease factor (further modified by Hard/Easy multipliers and a global interval modifier).
- **Hard multiplier**: Configurable multiplier (default 1.20) applied instead of the ease factor when "Hard" is chosen, producing a smaller interval increase and typically also reducing the ease factor slightly.
- **Easy bonus**: Extra multiplier (default 1.30) applied on top of normal growth when "Easy" is chosen, and increases the ease factor.
- **Lapses/relearning steps**: When a review card is answered "Again," it enters a separate relearning step sequence (own short-delay steps), and its interval is reduced (via a "new interval" multiplier, default 0%) and ease factor is penalized.
- **Minimum interval after lapse**: A floor (default 1 day) on the interval a card gets after completing relearning.
- **Maximum interval**: Global cap on how long any interval can grow (default 100 years).
- **Interval modifier**: A single global multiplier applied to all review intervals, used to tune overall workload.
- **Fuzz factor**: Small randomization applied to computed intervals so that cards with the same interval don't all bunch up on the same future day.
- **Day boundary rollover**: Learning steps that would cross the configured "day cutoff" are automatically converted into day-based (review) intervals instead of intra-day delays.
- **Answer buttons**: Four graded responses — Again, Hard, Good, Easy — each previewing the resulting next-review time before the user picks it; Again/Good-only simplified mode is also supported.
- **Leech detection**: A card that has lapsed (been forgotten after being learned) a configurable number of times (default 8) is flagged as a "leech"; configurable action is to tag it, and/or suspend it, so problem cards can be found and reworked.
- **Falling behind / overdue handling**: If reviews aren't done on time, Anki prioritizes the longest-overdue cards first and factors the actual delay into the next interval calculation rather than scheduling strictly off the original due date.

## FSRS (Free Spaced Repetition Scheduler)

- **FSRS algorithm**: A newer, optional scheduler (alternative to the SM-2-derived algorithm) based on a memory model with per-card stability, difficulty, and retrievability estimates rather than a single ease factor.
- **Desired retention**: User-set target probability of recall; higher desired retention produces shorter intervals and more daily reviews, lower retention produces longer intervals and fewer reviews.
- **FSRS parameters / optimization**: Model weights can be optimized (trained) from the user's own review history, recommended periodically (e.g. monthly) as more data accumulates.
- **Reschedule on change**: Option controlling whether existing cards' due dates are recalculated immediately when FSRS parameters or settings are changed.
- **Historical retention**: Assumed recall probability (default 90%) used to fill in gaps for reviews logged before FSRS-relevant data existed.
- **Ignore cards before date**: Lets old review history be excluded from FSRS parameter optimization (e.g. after a scheduling reset or algorithm change).
- **Simulator / "Help Me Decide"**: Projects future daily workload at different desired-retention levels so the user can pick a retention target that fits their study capacity.
- **Stability / Difficulty / Retrievability stats**: FSRS-specific per-card metrics exposed in search (`prop:s`, `prop:d`, `prop:r`) and in the statistics screen.

## Card Types / Note Types / Templates

- **Note types**: A note type defines a set of fields and one or more card templates; built-in types include Basic, Basic (and reversed card), Basic (optional reversed card), Basic (type in the answer), Cloze, and Image Occlusion.
- **Custom note types**: Users can create new note types (optionally cloned from existing ones) via a note type manager, with arbitrary fields and templates.
- **Fields**: Each note type has named data fields (e.g. Front/Back); field content is reused across all of that note's generated cards via template placeholders.
- **Card templates**: Separate Front, Back, and Styling (CSS) definitions per card type, written in HTML/CSS, with live preview.
- **Multiple cards per note**: A single note can generate multiple distinct cards (card types) if its templates/fields support it — e.g. reversed-card types generate a forward and backward card from the same note.
- **Deck override**: A specific card template can be pinned to always be created in a particular deck, overriding the note's normal target deck.
- **Browser appearance override**: A simplified/alternate template rendering can be defined specifically for how a card shows up in Browse list columns.
- **Cloze deletion**: `{{c1::text}}` syntax hides part of a field's text; each distinct cloze number (`c1`, `c2`, …) in a note generates its own card, with all other clozes in that field shown as ordinary text.
- **Cloze hints**: `{{c1::text::hint}}` shows a custom hint word/phrase in place of the blanked text.
- **Cloze conditional templates**: `{{#c1}}...{{/c1}}` blocks let extra content (e.g. a per-cloze hint field) appear only when that specific cloze card is being generated/shown.
- **Field replacement modifiers**: Special template filters transform field content — `{{furigana:Field}}`/`{{kana:Field}}`/`{{kanji:Field}}` for Japanese ruby text, `{{hint:Field}}` for click-to-reveal hints, `{{type:Field}}`/`{{type:cloze:Field}}` for type-in-answer checking (with `type:nc:` to ignore accents).
- **Text-to-speech fields**: `{{tts lang_CODE:Field}}` reads a field aloud with configurable language, voice, and speed; `{{tts ... cloze-only:...}}` reads just the hidden cloze portion.
- **Special/meta fields**: Templates can reference `{{Tags}}`, `{{Type}}`, `{{Deck}}`, `{{Subdeck}}`, `{{Card}}` and `{{FrontSide}}` (the rendered front, usable on the back template) in addition to user-defined fields.
- **Image Occlusion note type**: Native card type (since Anki 23.10) that hides regions of an image with rectangle/ellipse/polygon masks; supports "Hide All, Guess One" and "Hide One, Guess One" modes.
- **Conditional card generation**: Whether a card is generated for a note depends on whether its relevant field(s) contain text (empty fields can suppress the corresponding card).

## Decks

- **Decks and subdecks**: Cards are organized into decks; decks can be nested hierarchically (`Parent::Child`) and studied together or independently.
- **Deck presets/options groups**: Deck options (limits, steps, FSRS settings, etc.) are defined in reusable "preset" groups that can be shared across multiple decks.
- **Per-deck vs. shared limits**: New-card and review limits can be set to apply per individual deck, cumulatively including subdecks, or just for "today" as a temporary override.
- **Limits start from top**: Option making a parent deck's daily limits constrain all its subdecks combined, rather than each subdeck having independent limits.

## Deck Options (Scheduling Configuration)

- **New cards/day & max reviews/day**: Daily caps on how many new cards are introduced and how many review cards are shown.
- **New cards ignore review limit**: Lets new cards keep appearing even after the review limit for the day has been hit.
- **Insertion order**: New cards can be added to the deck sequentially or in random order.
- **New card gather/sort order**: Controls how new cards are pulled from subdecks and in what order they're presented (by deck, position, random, etc.).
- **New/review mixing order**: Whether new cards are interleaved with review cards, shown before them, or after them.
- **Review sort order**: Cards due for review can be ordered by due date, deck, interval length, ease, or "relative overdueness."
- **Burying**: New-sibling, review-sibling, and interday-learning-sibling burying options hide related cards from the same note until the next day, to avoid answering near-duplicate cards back-to-back.
- **Easy days**: Per-weekday multipliers to reduce (or increase) review load on specific days of the week.
- **Audio playback options**: Auto-play toggle and whether front-side audio is skipped when replaying the answer.
- **Timers**: Optional per-card answer timer (default cap 60s) with an on-screen visible countdown and an option to stop timing once the answer is shown.
- **Auto advance**: Optional automatic progression from question to answer (and to the next card) after a configurable delay, for hands-free review.
- **Custom scheduling (JS)**: Advanced hook allowing user-supplied JavaScript to override interval calculations for a deck.

## Filtered Decks / Custom Study

- **Filtered decks**: Temporary decks that pull cards matching a search query out of their home decks for focused study (e.g. cramming, catching up), bypassing normal daily limits.
- **Home deck return**: Cards remember their original deck and automatically return to it once removed from the filtered deck (e.g. after being answered enough times or the filtered deck is emptied/deleted).
- **Rescheduling toggle**: Filtered decks can either apply normal scheduling updates to cards as they're studied, or run in "preview" mode that leaves scheduling untouched.
- **Custom Study**: A guided UI (built on filtered decks) offering one-click presets — review forgotten cards, preview recently added cards, study by card state or tag, temporarily raise today's new/review limits, etc.
- **Filtered deck ordering**: Cards in a filtered deck can be ordered oldest-seen-first, randomly, by interval, by lapse count, by creation/due date, or by overdueness.
- **Exclusions**: Suspended, buried, and already-filtered cards cannot be pulled into a new filtered deck.

## Tags & Organization

- **Tags**: Free-text labels attached to notes (not individual cards), space-separated, searchable and manageable from the Browse sidebar.
- **Hierarchical tags**: Tags can be nested with `::` (e.g. `Math::Calculus`) and browsed as a tree.
- **Tag renaming/deletion**: Right-click or keyboard shortcuts (F2 rename, Del delete) in the sidebar rename/remove tags collection-wide.
- **Clear unused tags**: Utility to remove tags no longer attached to any note.
- **Marked tag**: A special `marked` tag toggled with a shortcut, used to flag notes of interest for later review/searching.
- **Flags**: Independent color-coded flags (red/orange/green/blue/pink/turquoise/purple, plus none) that can be set per card, separate from tags.
- **Find and Replace**: Bulk text substitution across selected notes' fields or tags, with optional regular-expression support.

## Browse / Search

- **Browse window**: A spreadsheet-like list of all notes/cards with sortable, configurable columns and a persistent sidebar (decks, tags, note types, saved searches, card state shortcuts).
- **Cards/Notes toggle**: Switches the list between one-row-per-card and one-row-per-note views.
- **Card Info panel**: Shows a given card's full review history and current scheduling stats (interval, ease, due date, lapses, etc.).
- **Search query language**: A dedicated query syntax supports free text, exact phrases (`"..."`), wildcards (`*`, `_`), and boolean combination via implicit AND, explicit `or`, `-` negation, and parentheses for grouping.
- **Field search**: `fieldname:value` matches a specific field's content; `field:` (empty) / `field:_*` (non-empty) check field emptiness.
- **Deck/tag/note-type search**: `deck:Name` (with `::` for subdecks), `tag:name` (`tag:none` for untagged), `note:TypeName`, `card:Name`/`card:1` (card template by name or position).
- **State search**: `is:new`, `is:learn`, `is:review`, `is:due`, `is:suspended`, `is:buried` (and buried subtypes) filter by card status.
- **Property search**: `prop:` comparisons on interval (`ivl`), due, repetitions (`reps`), lapses, ease, queue position, and (with FSRS) stability/difficulty/retrievability.
- **Time-based search**: `added:`, `edited:`, `rated:`, `introduced:` filter by how recently a note/card was created, modified, or answered.
- **Regex and normalization search**: `re:` for regular-expression matching; `nc:` to ignore accents/diacritics; `w:` for whole-word matches.
- **Flag/note-id/card-id search**: `flag:0-7`, `nid:`, `cid:` for direct lookups.
- **Saved/sidebar searches**: Common searches are one click away in the sidebar, with modifier-click to AND/OR/negate/replace the current search term.

## Statistics

- **Today summary**: Text overview of the day's reviews — counts by learning/review/relearning/filtered-deck card, plus an "again" count and accuracy percentage.
- **Future Due graph**: Forecast of upcoming review workload based on current scheduled intervals.
- **Calendar/heatmap**: Visual history of past review activity by day.
- **Review Count / Review Time graphs**: Reviews and time spent, broken down by card maturity (new/young/mature).
- **Card Counts**: Pie chart of the deck's composition (mature, young, unseen/new, suspended).
- **Review Intervals graph**: Distribution of interval lengths across cards.
- **Card Ease graph**: Distribution of ease factors across cards.
- **FSRS stat graphs**: Stability, Difficulty, and Retrievability distributions (when FSRS is enabled).
- **Hourly Breakdown**: Success rate and review volume bucketed by hour of day, useful for identifying when studying performs best.
- **Answer Buttons graph**: Frequency of each answer button pressed, broken down by card state.
- **True Retention table**: Actual recall-success rate broken down by timeframe and card maturity/interval bucket.
- **Export/scope controls**: Stats can be filtered to a deck or search, scoped to last 12 months/all-time/deck-lifetime, and exported as PDF.

## Review UI / Ergonomics

- **Answer buttons with interval preview**: Each of the four buttons shows the resulting next-review delay before it's pressed.
- **Keyboard-driven review**: Space/Enter reveals the answer and (once shown) defaults to "Good"; number keys 1-4 pick a specific answer button; `/` opens deck switch, `S` returns to the deck overview from study.
- **Undo**: A general undo stack lets the last review, edit, or other action be reverted (single-step undo of recent operations, not specific to review-only mid-session).
- **Suspend**: Manually hides a card from review indefinitely until explicitly unsuspended (independent of the automatic leech-based suspension).
- **Bury**: Manually or automatically hides a card until the next day only (temporary, auto-clears), distinct from suspending.
- **Mark note**: Quick shortcut to tag the current note "marked" for later attention.
- **Edit during review**: The current card can be edited in place without leaving the study session.
- **Auto-advance**: Optional hands-free timed progression through cards (see Deck Options above).

## Media & LaTeX in Cards

- **Media attachment**: Images, audio, and video can be attached to fields via a file picker or direct paste; a microphone control supports recording audio directly.
- **LaTeX/MathJax support**: An in-editor shortcut inserts MathJax/LaTeX markup for rendering mathematical and chemical notation on cards.
- **HTML source editing**: A raw HTML editor view lets users hand-edit a field's underlying markup instead of the rich-text view.
- **Rich text toolbar**: Bold/italic/underline/sub/superscript, text color, formatting eraser, lists, alignment, and indentation controls in the field editor.
- **Text-to-speech (TTS) rendering**: See field modifiers above — cards can read specified fields aloud at review time via template TTS directives.

## Import / Export

- **Anki Deck Package (.apkg)**: Exports a single deck (with subdecks) including notes, note types, and (optionally) scheduling/media, for sharing with other users; importing merges into the existing collection.
- **Anki Collection Package (.colpkg)**: Exports the entire collection; importing one replaces the current collection outright.
- **Strip scheduling on import**: Option to import a shared deck without its original learning progress, also removing leech/marked tags from imported cards.
- **Text file import/export**: Plain text/CSV import with field-to-column mapping, and matching plain-text export.
- **Third-party format import**: Support for importing from other flashcard/SRS programs (e.g. Mnemosyne, SuperMemo) via dedicated importers.
- **Add-on ecosystem**: A large catalog of user-contributed add-ons (browsable/installable in-app) extend or modify nearly every part of Anki — editor tools, review behavior, statistics, note-type helpers, etc. (covered only briefly here, as a general extensibility point, not itemized).

## Sync

- **AnkiWeb sync**: Built-in two-way sync of collection and media against Anki's hosted AnkiWeb service, enabling multi-device study.
- **Media sync**: Media files sync separately from card/scheduling data; removing unused media locally (via Check Media) removes it from AnkiWeb on the next sync too.
- **Forced one-way sync**: A "force changes in one direction" override lets the user resolve a sync conflict by picking local or remote as the source of truth for card data (media still syncs normally).
- **Self-hosted sync server**: An alternative, self-run sync server binary for users who don't want to rely on AnkiWeb, aimed at technically comfortable users.
