# SAFC Documentation

## Table of Contents
- [General Info](#general-info)
- [Main Window](#main-window)
- [Some Basics](#some-basics)
- [Props. and Sets. Window](#props-and-sets-window)
- [Volume Map Window](#volume-map-window)
- [Cut & Transpose Tool](#cut--transpose-tool)
- [App Settings](#app-settings)
- [SMRP Container](#smrp-container)
- [MIDI Info Collector](#midi-info-collector)
- [Additionals](#additionals)

---

## General Info

- SAFC has a multiwindowed, draggable interface — behaviour is the same as in MS Windows.
- Only the "Top" window is manipulable.
- Most UI elements are active only when hovered, including input fields and maps. To type into an input field or manipulate a map with the keyboard, you must hover it with the mouse.
- Checkboxes are colored squares without text: **green** = checked, **red** = unchecked.
- Most buttons, input fields, and checkboxes have a tooltip that appears on hover. If one is missing, please report it.
- `"* is applied to @"` means the value * will be saved to @'s settings.

---

## Main Window

The main interface of SAFC.

**MIDI List area:**

| # | Element | Description |
|---|---------|-------------|
| 1 | MIDI list | Lists all added MIDIs. Left-click to select; right-click to open "Props. and sets." window. Scroll with mouse wheel. |
| 2 | Up arrow | Scrolls the list up. |
| 3 | Selected MIDI | Currently selected (or hovered) MIDI entry — they look identical. |
| 4 | Down arrow | Scrolls the list down. |

**Buttons:**

| Button | Description |
|--------|-------------|
| Add MIDI | Opens a file dialog and adds the selected MIDI to the list. |
| Remove selected | Removes the currently selected MIDI. Does nothing if none is selected. |
| Remove all | Clears the entire MIDI list. |
| Global PPQN | Prompts for a value. If non-zero, applies it to every MIDI. If zero, assigns the default (max of each MIDI's original PPQ). |
| Global offset | Prompts for a value and applies it to every MIDI. |
| Global tempo | Prompts for a value and applies it to every MIDI. |
| Remove vol. maps | Removes the volume map module from every MIDI. |
| Remove C&Ts | Removes the Cut & Transpose module from every MIDI. |
| Remove p. maps | Intended to remove pitch map modules. Currently throws an alert — pitch maps are not yet implemented. |
| Remove modules | Removes all module types from every MIDI. |
| Settings... | Opens the App Settings window. |
| Save as... | Opens a save dialog and stores the save path to settings. |
| Start merge | Opens the SMRP Container window and begins processing. |

> **Default save path:** First MIDI's file path with `.AfterSAFC.mid` appended.

---

## Some Basics

### What is PPQN / PPQ?
The maximum possible number of minimal, non-zero-length, non-overlapping notes in a single beat. Also called "MIDI Quality" or "Timebase" in some apps. Can be changed manually in per-MIDI settings.

### What is Offset?
The amount of time from the absolute MIDI beginning to a specific MIDI's beginning. Measured in postprocessed ticks (i.e. ticks at the new recalculated PPQ, not the original).

### What is Tempo?
Technically BPM. For a single MIDI, you can:
- Leave tempo events untouched,
- Replace the BPM value of every event with a specific one,
- Ignore all tempo events.

The last two can be mixed — for example, set a new tempo and enable "Ignore tempos" to reduce MIDI size.

### What is a Volume Map?
A module that surjectively converts note velocities using a mathematical partially-linear converter and a user-defined chart/graph. See the [Volume Map Window](#volume-map-window) section for details.

### What is the Cut & Transpose Tool?
A module that cuts notes outside a specified range and transposes notes in a MIDI. See the [Cut & Transpose Tool](#cut--transpose-tool) section for details.

### What is a Pitch Map?
Currently unavailable. Intended to surjectively convert pitch bend values similarly to the volume map. No GUI is available yet.

### How Does Merging Work?

1. **PPQ recalculation stage** — Events are deleted, modules are called, running status bytes are converted to normal events, and PPQ is recalculated into an intermediate MIDI file.
2. **Merge stage** — Splits into two threads:
   - *Slow thread:* processes MIDIs with Inplace merge enabled (close to real-time playback algorithm).
   - *Fast thread:* runs the standard merge algorithm on remaining MIDIs.
   - Outputs of both threads are then merged using the regular method.

### What is Inplace Merge?
A merge technique that minimizes the output track count. The first tracks of every MIDI are combined into one track, then the second tracks, then the third, and so on. If a MIDI runs out of tracks, it is ignored while others continue. The output has as many tracks as the input with the most tracks.

### What Events Does SAFC Count as "Important"?
All non-meta events, plus end-of-track and tempo events.

### What is a Merge Remnant?
Temporary files created during PPQ recalculation. They normally have a postfix of `[number]_.mid`. Two additional remnant types exist:
- **Inplace merge remnant** — postfix `.I`
- **Regular merge remnant** — postfix `.R`

### What is a Selection?
For a given MIDI, a selection defines a tick range:
- Notes **fully inside** the selection are kept untouched.
- Notes **partially inside** are cut to fit within it.
- Notes **outside** are ignored, as if "Ignore note events" were enabled for that region.

---

## Props. and Sets. Window

Opened by **right-clicking** a MIDI in the list. Shows and edits settings for that specific MIDI.

| # | Element | Description |
|---|---------|-------------|
| 1 | Filename | Displays the MIDI's filename. |
| 2 | Per-MIDI PPQ override | Overrides PPQ for this MIDI. Not recommended to change. If zero, PPQ will not be recalculated. |
| 3 | Tempo override | If zero, no tempo events are overridden. Otherwise, all tempo events are replaced and an additional tempo event is added to the first track — even if "Ignore tempos" is enabled. |
| 4 | Offset input field | Sets the offset for this MIDI. |
| 4a | Offset timing | Determines whether PPQ recalculation is applied before or after the offset. |
| 5 | Group ID | Identifier of the processing thread for this MIDI. Updates automatically when a MIDI is added. |
| 6 | Remove empty tracks | Deletes tracks that have no important events. |
| 7 | Remove merge remnants | Deletes remnant files after processing. |
| 8 | Replace all instruments with piano | Replaces all program change events with piano (program 0). |
| 9 | Ignore tempo events | Tempo events will not pass the PPQ recalculation stage. |
| 10 | Ignore pitch bending events | Pitch bend events are discarded. |
| 11 | Ignore note events | Note events are discarded. |
| 12 | Ignore everything except specified | Only notes, pitches, and tempos are kept. |
| 13 | Multichannel split | Splits multichannel tracks into separate single-channel tracks. |
| 14 | RSB Compression | Compresses most note events from 4 bytes to 3 bytes using the MIDI standard. Cannot be used with Inplace merge. |
| 15 | A2A (Apply to All) | Applies the current checkbox settings to every MIDI in the list. |
| 16 | Inplace merge toggle | Enables or disables Inplace merge for this MIDI. |
| 17 | Volume map... | Creates a volume map module and opens its GUI. |
| 18 | Cut & Transpose... | Creates a C&T module and opens its GUI. |
| 19 | Collapse MIDI | Merges all tracks within this MIDI into a single track (or 16 tracks if Multichannel split is active). |
| 20 | Pitch map... | Future feature — not yet implemented. |
| 21 | Collect Info | Opens the Single MIDI Info Collector window for this MIDI. |
| 22 | Apply | Applies all settings in this window to the MIDI. |
| 23 | Selection start tick | Marks the beginning of the selection range. |
| 24 | Selection length | Sets the selection length in ticks. A negative value disables the selection. |
| 25 | Allow sysex events | Permits sysex events to pass through (filtered by default, as they cause issues in most MIDI players). |
| 26 | Enable legacy RSB/Meta behaviour | Disables RSB byte reset after meta events in the input MIDI. Highly discouraged by the standard; may be useful for very old pieces. |
| 27 | Text info field | Informational text display. |

---

## Volume Map Window

Allows surjective conversion of note velocities using a user-defined graph.

The core mechanism is a **partially linear converter** — it interpolates linearly between user-placed points on a 2D map.

> **Note on wrapping:** If a line segment hits a horizontal border of the map, it wraps to the opposite side and continues in the same direction. This is a side effect of the template-based core and makes detecting variable overflows impractical.

| # | Element | Description |
|---|---------|-------------|
| 1 | Map visualiser | The main editing area. In Single mode: click to place a point. In Double mode: click to start/end a line segment. |
| 2 | Coordinate display | Shows the coordinates of the currently hovered point on the plane. |
| 3 | Standard velocity range indicator | A green square marking the standard velocity range (0–127). Values outside are non-standard. |
| 4 | Hovered point indicator | Highlights the hovered point and draws perpendiculars from each border to it. |
| 5 | Shape x^y | Draws the function x^y on the map, where y is taken from field 8. |
| 6 | Erase points | Clears all points from the map. |
| 7 | Delete map | Removes the volume map module from this MIDI entirely. |
| 8 | Degree input field | The exponent y used by the x^y shape function (item 5). |
| 9 | Simplify map | Removes points that lie on (or nearly on) the same line as their neighbours. |
| 10 | Trace map | For every possible input value, calculates the output and places the exact resulting point on the map. |
| 11 | Mode switcher | Toggles between Single (place individual points) and Double (draw line segments) modes. |

---

## Cut & Transpose Tool

Transposes notes and cuts them to a specified pitch range.

**Keyboard controls** (hover the active area first):

| Key | Action |
|-----|--------|
| W / S | Transpose up / down |
| Q / E | Move upper cut limit up / down |
| A / D | Move lower cut limit up / down |

Notes within the cut frame are kept and transposed to their new position. If a transposed note falls outside the keyboard entirely, it is deleted.

| # | Element | Description |
|---|---------|-------------|
| 1 | C&T active area | The main keyboard visualiser and interaction area. |
| 2 | Cut frame | The range of keys that will be kept. |
| 3 | Reset | Sets transpose to 0 and allows all keys through. |
| 4 | Transpose ±128 | Transposes the MIDI by 128 keys. |
| 5 | Cut down to 128 | Sets transpose to 0 and removes all notes outside the standard range. This is the default state. |
| 6 | Copy | Copies the current C&T settings to an internal variable. |
| 7 | Paste | Applies the copied C&T settings to the currently active C&T module. |
| 9 | Delete | Removes the C&T module from this MIDI. |

---

## App Settings

Global application settings.

| # | Element | Description |
|---|---------|-------------|
| 1 | Default checkbox settings | Default values for checkboxes (except item 2), mirroring the options in Props. and Sets. Applied to newly added MIDIs. |
| 2 | Max threads | Maximum number of threads used during PPQ recalculation. |
| 3 | Rotation angle | UI rotation angle in degrees, counter-clockwise. |
| 4 | Background ID | Visual theme. Values 1–5 = Blue-Orange background; values above 5 = Gray background. |
| 5 | Font name input | Name of the font to use in fonted rendering mode. Press Enter to apply. Note: the font name is sensitive to typos, and even in fonted mode text renders with sticks. |
| 6 | Check for autoupdates | Toggles SAFC's automatic self-updating routine. |
| 6 | Character height-to-width ratio | Adjust with mouse wheel on the gray plate or via the input field (press Enter). |
| 7 | Character height | Sets the character height for text rendering. |
| 8 | Enable / Disable fonts | Toggles font-based text rendering. **Requires a restart to take effect.** |
| 9 | Apply | Applies all current settings. |

---

## SMRP Container

A temporary window that appears automatically when processing begins. It cannot be opened manually and cannot be closed. It is designed so that its corners, edges, and title bar are never reachable.

**Stage 1 — PPQ recalculation:**
One SMRP visualiser appears per processing thread. Each shows:
- A progress percentage at the top.
- The first 30 characters of the current MIDI's filename (visible on hover over the central waiting circle).
- Three status lines: log, warning, and error.

**Stage 2 — Merge:**
Two simplified SMRP visualisers show progress (measured in processed tracks):
- Left: Inplace merge progress.
- Right: Regular merge progress.

**Stage 3 — Finalization:**
A minimal visualiser confirms the process is still running.

When all processing is complete, the Container window closes and the Main window reappears.

---

## MIDI Info Collector

Opened from the Props. and Sets. window via **Collect Info**. Reads a MIDI file and collects analytical data about it.

| # | Element | Description |
|---|---------|-------------|
| 1 | Info box | Describes the currently performing operation. |
| 2 | Tempo graph (A) | Displays tempo over time. Move and scale with WASD. |
| 3 | Polyphony derivative graph (B) | Shows finite polyphony difference — crash points appear as large spikes relative to the surrounding noise. |
| 4 | Export tempo | Exports tempo information to a CSV file. |
| 5 | Enable graph A | Toggles visibility of the tempo graph. |
| 6 | Enable graph B | Toggles visibility of the polyphony derivative graph. |
| 7 | Export all | Exports per-tick data including note on/off counts, polyphony, current tempo, and elapsed time in seconds. |
| 8 | Minutes field | End time (minutes) for tick integration. |
| 8a | Seconds field | End time (seconds) for tick integration. |
| 9 | Milliseconds field | End time (milliseconds, 1/1000s) for tick integration. Invalid input like `1:75:0` is truncated to `2:15:0`. |
| 10 | Integrate ticks | Converts the specified end time to ticks and displays the result in field 17. |
| 11 | Reset graph A | Restores the tempo graph to its initial view settings. |
| 12 | Format selector | Chooses export format: `.csv` (standard) or `.atraw` (for a specific external app). |
| 13 | Delimiter | CSV delimiter character. Default is semicolon `;` (Excel dislikes commas as separators). |
| 14 | End tick field | The tick value to convert into time for time integration. |
| 15 | Integrate time | Converts the specified end tick to a time value and displays the result in field 17. |
| 16 | Reset graph B | Restores the polyphony derivative graph to its initial view settings. |
| 17 | Integration answer field | Displays the result of the most recent tick-to-time or time-to-tick conversion. |
| 18 | Additional info textbox | Reserved for future use. |

---

## Additionals

1. **Multiplying mode** *(hidden, experimental)* — Allows multiple instances of the same MIDI in the list. The output MIDI is handled correctly regardless. Combined with Inplace merge, this can become a powerful feature.

2. **Note event integrity checks** — SAFC verifies that note events are properly paired. If there are fewer note-on events than expected, additional note-off events are appended at the end of the track. Orphaned note-off events (with no preceding note-on to match) are silently ignored.

3. **Pitch maps** — Not yet available. The intended behaviour mirrors the volume map, but the GUI design is unresolved.
