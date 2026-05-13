# HANDOFF — Music Tutor

Picking up on a different machine: this is the full state of the
project, what it does, how it's built, and what's open.

## What this app is

An Android tablet WebView app that turns a USB-MIDI keyboard into a
sight-reading and chord drill machine for a 33-string lever harp
student. Four drill modes accessible from the home screen:

1. **Note Reading** — random natural note (MIDI 36-72, C2-C5) on a grand
   staff; play the exact note to advance. Tracks correct / attempts /
   median ms-to-correct / current streak.
2. **Chord Recognition** — random chord shown as engraved notation on a
   grand staff. Filter chips above the staff toggle which chord families
   appear (Triads, 7ths, 9ths, 13ths), which inversions (Root, 1st, 2nd,
   3rd), and whether polychords are included. LH and RH are always on
   opposite sides of middle C; LH biased low and RH biased high so
   polychords have a wide gap and don't sound muddy. 9ths and 13ths use
   a spread voicing (root + 5th in LH, upper structure in RH).
3. **Hymn Practice** — pick any of 292 OpenHymnal hymns; renders one
   "system" (one staff line) at a time as a flashcard. Tap the left
   half of the stage to go back, right half to advance. Also
   auto-advances 1.5 s after the last MIDI note-on.
4. **Drill Book** — pick any of 65 drills from the harp-drill-book
   curriculum (`harp-drill-book-handoff.tar.gz`); same flashcard UI.
   Tap-left/right navigates between *drills* (not pages within a
   drill), since most drills are a single system.

## Target hardware

- **Tablet:** Lenovo P90 (serial `P90YPDU16Y251200164`), Android.
  Currently the cable goes USB-C to either the laptop (for
  `installDebug`) or the MIDI keyboard (for play) — not both.
- **MIDI keyboard:** SMK-37 PRO, 37 keys (assumed C2..C5 = MIDI 36..72).
  All randomly-generated drills (Note Reading, Chord Recognition) stay
  within this range. Hymns and Drill Book content render as authored
  (which may exceed the keyboard range; user octave-shifts as needed).

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
│       ├── hymns/0.abc .. 291.abc  one file per OpenHymnal tune
│       └── drills/0.abc .. 64.abc  one file per drill-book exercise
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
[topbar: Back | Prompt (e.g. "Play: C#m7") | MIDI status]
[chord-filter chips, chord screen only — Triads · 7ths · 9ths · 13ths · Root · 1st · 2nd · 3rd · Polychord]
[stage: abc2svg-rendered grand staff, fills most of the screen]
[held-keys pill strip, hidden when nothing is pressed]
[toolbar: inline stats (correct / attempts / median ms / streak) ........ Skip · Reset stats]
```

The grand staff stays vertically centred via `centerBraceCusp()` in JS:
finds the leftmost long vertical path (the system's joining bar),
treats its midpoint as the brace cusp, and applies a per-render
`translateY` so the cusp sits at the stage's vertical centre. Works
for single-staff drills (falls back to the SVG content bbox) and
multi-system hymns alike.

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

If `OpenHymnal.abc` or the drill book changes, regenerate the per-tune
asset files:

```bash
# Hymns: 292 .abc files into assets/hymns/ + an inlined window.HYMNS
# manifest baked into index.html. The python block that does this is
# in HANDOFF history; rerun on changes.
python3 - <<'PY'
import json, re, os
src = "OpenHymnal.abc"
dst = "tablet_app/app/src/main/assets/hymns"
os.makedirs(dst, exist_ok=True)
with open(src, encoding="utf-8", errors="replace") as f: text = f.read()
parts  = re.split(r"(?m)^(?=X:\s*\d)", text)
header = parts[0]
for fn in os.listdir(dst): os.remove(os.path.join(dst, fn))
hymns = []
for idx, body in enumerate(parts[1:]):
    open(f"{dst}/{idx}.abc", "w").write(header + body)
    m = re.search(r"^X:\s*(\d+)", body, re.M)
    t = re.search(r"^T:\s*(.+?)\s*$", body, re.M)
    if m and t: hymns.append({"n": int(m.group(1)), "idx": idx, "title": t.group(1).strip()})
json.dump(hymns, open("tablet_app/app/src/main/assets/hymns.json","w"), ensure_ascii=True)
PY
```

Drill book extraction lives in `harp-drill-book-handoff.tar.gz` — its
own `build_book.py` is the source of truth for the 65 drill ABCs that
get extracted into `assets/drills/0.abc..64.abc`.

After regenerating, the inlined `window.HYMNS = [...]` / `window.DRILLS = [...]`
blocks in `index.html` also need updating (search for "window.HYMNS =").

## Status

**Working:**
- All four drill modes render via abc2svg, correctly centred.
- Chord drill filters persist (localStorage `harpTutorChordFilters`).
- Hymn / drill book flashcard navigation: tap, auto-advance, back-button
  return to picker.
- Polychord LH placement biased to the lowest valid octave; RH biased
  to the highest, with the keyboard range constraint (MIDI 36..72).
- 9th and 13th chord voicings spread across both hands with span <= 10
  per hand.
- MIDI auto-launch on USB-attach via `UsbMidiAttachActivity` (need to
  accept the Android prompt the first time the SMK is plugged in,
  since `com.tutor.musictutor` is a separate package from HarpHymnal).

**Open / not yet built:**
- The Drill Book "practice" UX is just static rendering — no
  note-by-note progress tracking like a real teacher. Auto-advance
  fires 1.5 s after the last note-on, but doesn't verify the user
  played the right notes.
- The Hymn Practice UX is the same: render + auto-advance, no
  correctness check.
- Stats are tracked only for Note Reading + Chord Recognition (the
  generative drills with a known correct answer).
- abc2svg renders music glyphs via an embedded music font baked into
  the first SVG block per render. We re-inject those `<defs>` /
  `<style>` blocks into subsequent systems via `normalizeAbcSystems`
  so the staff lines don't vanish on later systems of multi-page
  hymns. Watch for regressions if abc2svg is updated.

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
