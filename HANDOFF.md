# HANDOFF — Music Tutor

Picking up on a different machine: this is the full state of the
project, what it does, how it's built, and what's open.

## What this app is

An Android tablet WebView app that turns a USB-MIDI keyboard into a
sight-reading and chord drill machine for a 33-string lever harp
student. All notation renders on a **single baritone C-clef staff**
(C clef on line 5, so middle C = top line, F3 = middle line, B2 =
bottom line). This puts the SMK-37 PRO keyboard range C2..C5 (MIDI
36..72) symmetrically around the staff — C2 is at 3 ledger lines
below, C5 is at 3 ledger lines above, with the middle of the range
landing on the middle line. No more "million ledger lines below" that
soprano clef produced for the keyboard's bass register.

The home page is split into two columns: vertical drill-mode buttons
on the left, and a permanent reference staff on the right showing the
22 naturals C2..C5 with each labeled. Four drill modes:

1. **Note Reading** — random natural note across the full SMK-37
   keybed (MIDI 36–72, C2–C5) on the baritone C staff; play the exact
   note to advance. Tracks correct / attempts / median ms-to-correct /
   current streak (when scoring is on; see the stats toggle).
2. **Chord Recognition** — random chord shown as one stacked chord on
   the baritone-C staff. Filter chips above the staff toggle which
   chord families appear (Triads, 7ths, 9ths, 13ths), which inversions
   (Root, 1st, 2nd, 3rd), and whether polychords are included. LH and
   RH are always on opposite sides of middle C; LH biased low and RH
   biased high so polychords have a wide gap and don't sound muddy.
   9ths and 13ths use a spread voicing (root + 5th in LH, upper
   structure in RH). Both voices stack into a single chord token.
3. **Hymn Practice** — pick any of 287 OpenHymnal hymns, pre-converted
   to single-staff chord notation with Roman numeral analysis above
   each chord change. Each hymn is transposed so its highest pitch
   lands at C5 (MIDI 72), with low notes folded up an octave if they
   drop below C2 — so every hymn fits the SMK-37 keybed exactly and
   sits naturally on the baritone-C staff. The whole hymn renders on
   one screen (all systems stacked + CSS-zoomed to fit). The bundled
   ABCs include `clef=C1` but the WebView renderer rewrites that to
   `clef=C5` at render time.
4. **Drill Book** — 65 drills organized into 10 thematic groups (Bass
   patterns, Alberti variants, Arpeggios, etc.). Picking a group
   renders ALL its drills as a labeled 3-column grid on one screen.
   Tap-left/right cycles between groups. Multi-voice drills were
   merged via music21 into single-voice patterns so they read as one
   musical line on the baritone-C staff.

## Target hardware

- **Tablet:** Lenovo P90 (serial `P90YPDU16Y251200164`), Android.
  Currently the cable goes USB-C to either the laptop (for
  `installDebug`) or the MIDI keyboard (for play) — not both.
- **MIDI keyboard:** SMK-37 PRO, 37 keys (C2..C5 = MIDI 36..72). Every
  rendered note in every mode is constrained to this range — drills
  pick from it directly, hymns are transposed at conversion time to
  fit (top = C5, with sub-C2 octave-folded).
- **Note-only mode:** the app is also useful with no MIDI device
  connected (e.g., when practicing on harp or piano). All drill modes
  work as pure music stands; scoring is disabled by the user with a
  toggle (see Stats / scoring below).

## Architecture

```
tablet_app/                       Android Gradle project
├── app/src/main/
│   ├── AndroidManifest.xml       package com.tutor.musictutor
│   ├── java/com/tutor/musictutor/
│   │   ├── MainActivity.java     single Activity, WebView host
│   │   ├── MidiSynthService.java foreground service: MIDI in + Oboe synth
│   │   ├── UsbMidiAttachActivity.java   auto-launches on USB attach
│   │   └── OboeSynth.java        JNI wrapper around the native synth
│   ├── cpp/
│   │   ├── CMakeLists.txt
│   │   ├── oboe_synth.cpp        AAudio + FluidLite synth, lock-free SPSC queue
│   │   └── fluidlite/            vendored stripped-down FluidSynth
│   └── assets/
│       ├── index.html            *the whole UI* (HTML + CSS + JS)
│       ├── abc2svg-1.js          ABC -> SVG engraving (309 KB, from HarpHymnal)
│       ├── soundfont/gm.sf2      General MIDI SoundFont (5.9 MB)
│       ├── hymns/0.abc .. 286.abc  one file per OpenHymnal tune
│       │                            (transposed for SMK keybed; see below)
│       └── drills/0.abc .. 64.abc   one file per drill-book exercise
```

The whole UI lives in `index.html`. The Android side just provides:

- the MIDI plumbing (`MidiSynthService` parses incoming MIDI bytes,
  forwards note-on/off events as JS calls to `window.onMidiNote(n, v)`
  and `window.onMidiNoteOff(n)`, and pushes connection-state strings
  via `window.onMidiStatus(msg)`),
- audio playthrough (Oboe + FluidLite SoundFont synth, pinned to the
  built-in speaker so the keyboard's USB audio sink can't hijack it),
- file access so the WebView can `XMLHttpRequest` the per-hymn /
  per-drill `.abc` files (file:// + `setAllowFileAccessFromFileURLs`).

## How the UI is structured

```
[topbar: Home · Back · (Recent ▾, practice only) | Prompt-or-Picker-header | MIDI status]
[chord-filter chips, chord screen only — Triads · 7ths · 9ths · 13ths · Root · 1st · 2nd · 3rd · Polychord]
[stage: abc2svg-rendered baritone-C-clef staff(s), fills most of the screen]
[held-keys pill strip, hidden when nothing is pressed]
[toolbar (drills only): inline stats (correct / attempts / median ms / streak) · Stats: on/off · Skip · Reset stats]
```

The topbar is consistent across all screens (same skeleton always); only
the contents of each slot vary. The picker screens (Hymn / Drill Book)
inject their title + search input directly into the prompt slot via the
`#prompt.picker-mode` class — no separate "header row" inside the picker
body.

`Home` (always visible off-home) jumps straight to the home screen from
anywhere. `Back` steps one level (practice → picker → home).

Sizing:
- **Drills** (Note Reading, Chord Recognition) — `fitSvgToStage()`
  measures the rendered SVG and applies a CSS `transform: scale(N)` so
  the staff fills ~98% × 95% of the stage. The transform is composed
  with `centerBraceCusp()`'s `translateY` so the staff midline lands on
  the stage midline.
- **Hymn practice** — renders ALL systems on one page (stacked in an
  `.abc-stack` wrapper) and uses CSS `zoom` (which, unlike
  `transform: scale`, expands the layout box and prevents stacked SVGs
  from overlapping). No page indicator when the full hymn fits.
- **Drill Book** — drills are organized in 10 groups by their
  hierarchical prefix (`1.x` bass patterns, `2.x` Alberti variants,
  `3.x` arpeggios, etc., titled in `DRILL_GROUP_TITLES`). Picking a
  group loads ALL drills in it onto one screen as a 3-column grid of
  `.drill-block` cells (label + baritone-C staff per cell). The stage
  CSS overrides `align-items: stretch` and `overflow: auto` for
  drill-group mode so cells layout from the top and scroll if needed.
  Tap left/right cycles to prev/next group.

Multi-tune rendering: a drill group's 8+ ABCs are concatenated into one
ABC document with sequential `X:1, X:2, ...` numbers, fed to a single
`Abc` instance via successive `tosvg()` calls. We push a fresh batch into
`chunkBatches` before each `tosvg` so each emitted chunk lands in the
correct batch — this is what lets us map chunks back to drills for
labeling (per-drill counts vary).

Roman-numeral chord annotations in the soprano hymnal would otherwise
bob up and down at the top of each chord stem (since abc2svg places
`"^text"` annotations above the topmost note of each chord stack);
`alignChordAnnotations()` post-processes each system's SVG to move every
above-staff letter-bearing text element to the same Y so they form a
clean horizontal row. Pure-digit texts (time signature numerals) are
filtered out so they're not also pulled up.

Recent-items dropdown: the `Recent ▾` button mounts beside `← Back` on
the hymn-practice and drill-practice screens. Persisted as two separate
localStorage histories:
- `harpTutorRecentHymns` — up to 10 hymn indexes
- `harpTutorRecentDrillGroups` — up to 10 drill group ids

Stats / scoring toggle: persisted in
`localStorage["harpTutorStatsEnabled"]`. When off, the inline stats are
hidden from the toolbar, the home-screen subtitle stats disappear, and
`acceptCorrect()` / `recordWrong()` skip counter updates so the user can
practice on harp or piano (no MIDI input) without polluting their
scores. The Stats: on/off toggle appears on the home screen
("Scoring is on · turn off") and on each drill's bottom toolbar.

TOCs:
- Hymn TOC — 4-column grid (287 hymns).
- Drill Book TOC — group cards in a 4-column grid (10 groups). Each
  group card shows the group id, title, and drill count.

## Build + install

Requires Android SDK + NDK 27.2 (matches HarpHymnal's setup).

```bash
cd tablet_app

# First time on a new machine: create local.properties pointing at SDK
echo "sdk.dir=$ANDROID_HOME" > local.properties

# Build + install on the connected tablet (assumes adb sees it)
./gradlew installDebug

# Launch
adb shell am start -n com.tutor.musictutor/.MainActivity
```

To force-stop + relaunch after install (state may have been mid-drill):

```bash
adb shell am force-stop com.tutor.musictutor && \
  adb shell am start -n com.tutor.musictutor/.MainActivity
```

Screenshots during dev:

```bash
adb exec-out screencap -p > /tmp/shot.png
```

## Regenerating bundled data

### Hymns (single-voice transposed conversion)

The bundled hymns come from a music21-based conversion pipeline that
takes OpenHymnal's four-voice SATB ABCs and:

1. Collapses every onset across the four voices into one stacked chord.
2. Transposes the result so the **highest pitch lands on C5
   (MIDI 72)** — the top of the SMK-37 keybed.
3. **Folds any pitch below C2 (MIDI 36) up an octave** until it sits
   on the keybed.
4. Emits a single-voice ABC with `K:<key> clef=C1` and Roman-numeral
   chord analysis (`"^V7"`, `"^I6"`, etc.) over each chord change.

The `clef=C1` in the source is a leftover — the WebView renderer
**rewrites it to `clef=C5` (baritone C) at render time** so the music
sits balanced around the staff (C2..C5 → ±3 ledger lines, see
"How the UI is structured" above).

The conversion source lives in the `files (4).zip` handoff bundle and
unpacks to a `soprano_hymnal/` tree:

- `convert.py` — reads `OpenHymnal.abc`, writes 287 numbered ABCs
  (some hymns fail to convert) into `soprano_hymnal/abc/NNN.abc`,
  plus Roman-numeral analysis above each chord change via
  `chord_id_v2.py`. Two constants control the transpose anchor and
  fold floor: `C5_MIDI = 72` (target ceiling) and `C2_MIDI = 36`
  (octave-fold floor). The folder name `soprano_hymnal/` is historic;
  what's bundled today is constrained-to-keybed single-voice ABC.
- `render.py` — runs `abcm2ps -g` on each ABC; not needed for the
  tablet build (the WebView renders with abc2svg directly).
- `build_html.py` / `hymnal.html` — companion print-format hymnal,
  not used by the tablet.

To rebundle into the tablet, copy the 287 ABCs out of
`soprano_hymnal/abc/` into `tablet_app/app/src/main/assets/hymns/`,
renaming each from `NNN.abc` (hymn number) to `<idx>.abc` (0-based
position in ascending hymn-number order), and rewrite the inlined
`window.HYMNS = [...]` array in `index.html` to match. The XHR loader
uses `hymns/${state.hymnIdx}.abc` so file names must be the 0-based
index, not the hymn number.

### Drill book

Drill book extraction lives in `harp-drill-book-handoff.tar.gz` — its
own `build_book.py` is the source of truth for the 65 drill ABCs that
get extracted into `assets/drills/0.abc..64.abc`. 24 of those were
originally multi-voice (RH/LH split); `merge_drills.py` (in /tmp during
the soprano pivot) used music21 to merge them into single-voice ABC
patterns. The render pipeline rewrites their `clef=` (whatever's set
in the source) to `clef=C5` so they match the rest of the app.

After regenerating either set, the inlined `window.HYMNS = [...]` /
`window.DRILLS = [...]` blocks in `index.html` also need updating
(search for "window.HYMNS =").

## Status

**Working:**
- All four drill modes render via abc2svg on a single soprano-clef
  staff, correctly centred.
- Chord drill filters persist (localStorage `harpTutorChordFilters`).
- Hymn / drill book flashcard navigation: tap, auto-advance, back-button
  return to picker.
- Polychord LH placement biased to the lowest valid octave; RH biased
  to the highest, with the keyboard range constraint (MIDI 36..72).
- 9th and 13th chord voicings spread across both hands with span <= 10
  per hand.
- Soprano hymnal renders Roman numeral analysis above each chord change
  (the converter labels `"^I"`, `"^V7"`, etc., and the WebView keeps
  those annotations).
- MIDI auto-launch on USB-attach via `UsbMidiAttachActivity` (need to
  accept the Android prompt the first time the SMK is plugged in,
  since `com.tutor.musictutor` is a separate package from HarpHymnal).

**Open / not yet built:**
- The Drill Book "practice" UX is just static rendering — no
  note-by-note progress tracking like a real teacher. Auto-advance
  fires 1.5 s after the last note-on, but doesn't verify the user
  played the right notes.
- The Hymn Practice UX is the same: render + auto-advance, no
  correctness check. To score hymns/drills properly we'd need an
  ABC-to-MIDI-sequence parser and a play-cursor that compares each
  user note against the next expected onset (with tolerance for
  octave-shifted plays since the SMK doesn't reach the full hymn
  range). Hymn/drill screens currently omit the bottom stats toolbar
  for that reason — would clutter without meaningful numbers.
- Stats are tracked only for Note Reading + Chord Recognition (the
  generative drills with a known correct answer).
- abc2svg renders music glyphs via an embedded music font baked into
  the first SVG block per render. We re-inject those `<defs>` /
  `<style>` blocks into subsequent systems via `normalizeAbcSystems`
  so the staff lines don't vanish on later systems of multi-page
  hymns. Watch for regressions if abc2svg is updated.
- Soprano-clef converter (`convert.py`) absorbs the pickup
  (anacrusis) into bar 1 of each hymn — so hymns with a pickup in the
  source appear as N-1 measures instead of N. The musical content is
  all present; the bar count is off by one. Could be fixed by
  teaching the converter to emit a partial first measure when the
  source starts on a non-downbeat. Affects hymns like 029
  ("Lord, Dismiss Us With Thy Blessing").

## Useful files

- `CLAUDE.md` — workspace-level conventions (7-bit ASCII, units, etc.)
- `OpenHymnal.abc` — source hymnal (292 tunes, 1.2 MB).
- `harp-drill-book-handoff.tar.gz` — first-principles drill curriculum
  + the markdown source + `build_book.py` that generates the engraved
  reference book.
- `grandstaff.png` — engraving reference used while tuning the
  grand-staff layout.

## Sibling projects

This project sits in `/home/james.clements/projects/` next to:

- `HarpHymnal/` — the parent project that introduced the tablet
  WebView + Oboe-synth pattern this app reuses. See its `HANDOFF.md`
  for context on the original MIDI playthrough work; the
  `MidiSynthService.java` and `OboeSynth.java` files here are derived
  from there, only the Java package and native library name were
  renamed.
- `c-taos/`, `odin/`, `spectre/`, etc. — unrelated BMD simulation work
  in the same workspace; ignore.
