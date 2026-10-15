# Player pipeline performance, October 15, 2026

This pass targets dense ("black") MIDI playback: files with millions of notes
per minute. The measurements below come from standalone ports of the player's
hot loops run on a 4-core 2.8 GHz Xeon under Linux (ratios matter, not the
absolute numbers), on a synthetic 16-track file of 2.26 million note pairs
with running status and zero-delta chords. The Nuker 4 class target is about
2.4 million notes per second, so about 4.8 million events per second.

| Stage | Before | After this pass |
|---|---|---|
| open() scan, 13.5 MB file | 16.5 ms, 0.8 GB/s, one thread | 5 ms, 2.6 GB/s, four threads |
| parse + heap merge + queue push | 12.7 M events/s (6.3 M notes/s), one thread | unchanged file path; prepared source decodes at 100 M events/s on four threads, serial merge 35 M events/s |
| sender to SYNCore, per event | ~280 ns with the synth thread popping concurrently (3.5 M events/s ceiling) | one clock read, one adapter lock and one synth reference per same-time run |
| falling notes per frame | every buffered note visited and re-emitted as client-array quads | pages that changed uploaded to buffer objects; the GPU scrolls |
| seek | replay from tick zero | fast-forward from the latest checkpoint at or before the target |

## What changed

### Sender runs (`simple_player::sender_thread_func`)

Events sharing a timestamp form a run. The sender waits once per run and
hands the run to the output through `syncore_output::try_send_short_messages`,
which takes the adapter lock and the synth reference once per span. The
playback clock snapshot is copied under its mutex only when
`set_playback_clock()` bumped `playback_state::clock_generation`. The
per-event mutex in `WindowsSynth::try_send_short_message` and the per-event
ring push live in the SYNCore repository and are the remaining per-event
costs on that path; a span enqueue there is the natural follow-up.

### Parallel open scan (`midi_track_scanner.h`)

Track chunks are located from their declared MTrk lengths and walked
concurrently (largest first). A chunk whose walk does not end exactly at its
declared end with an end-of-track meta is reported as not clean; the
unchanged serial walk in `read_through_one_track` resumes from that chunk's
header, so damaged files keep their legacy resynchronisation, size mismatch
warnings and alerts. Files under 1 MB are walked on the calling thread.

### Seek checkpoints

While walking, each track records checkpoints about 1/64 of the file apart
across all tracks (256 KiB to 8 MiB per track): byte position of a delta
time, tick, running status, held notes in note-on order, and the last value
of every program, pitch bend, channel pressure and controller with the tick it
was set at. `parser_thread_func` starts each track at its latest checkpoint
at or before the target tick. Held notes seed the seek tracker; channel state
is replayed through a virtual heap track at its original ticks, so later real
events override it in order.

The snapshot keeps the last value per controller number. A registered or
non-registered parameter sequence whose data entry controllers were reused
afterwards is replayed with its final values only; a replay from tick zero
would send every intermediate step.

### GPU-resident falling notes (`player_visuals.h`, `draw_data`)

Notes live in two start-ordered rings (white keys, black keys). Each ring is
mirrored into one buffer object of quads whose y is time in seconds from a
per-ring epoch; the modelview matrix scrolls them and two clip planes bound
the note area. A frame uploads only the 256-note pages holding notes that
arrived or ended since the last frame; growth, compaction, an epoch rebase
(every 1024 s) or a keyboard geometry change re-upload a ring once.

Expired notes are marked dead and released from the ring tail; when the dead
dominate the retained range the ring compacts, so a long held note cannot
pin what came after it. Key colours come from per-key monotone stacks that
admit notes as time passes and find the latest-started sounding note without
walking spans. The outline pass darkens quad edges to a quarter of what they
cover and is skipped above `draw_data::gpu_outline_note_limit` notes.

`draw_data::gpu_notes_enabled` (default on) selects the path. The CPU path
remains for contexts without buffer objects and is the one the overlap
removal modes apply to; with the GPU path, overlapping notes overdraw in
start order.

### Prepared file source (`prepared_midi_event_source`)

An opt-in `playback_event_source` for plain MIDI files ("Parallel decode" in
the player panel). Opening runs the parallel scanner; playback decodes every
track into compact 16-byte event pages on a thread pool, a bounded number of
pages ahead per track, and `next()` merges the pages by tick. Memory stays
bounded by the pages in flight. A file the scanner cannot verify end to end
is refused and the serial parser is used instead. Seeks start every track at
its checkpoint and yield the snapshot state at its original ticks, so the
external fast-forward reconstructs held notes and channel state exactly as a
replay from tick zero would.

The prepared source is opt-in because the measured bottleneck is downstream
of the parser: the serial file parser already exceeds the target event rate,
and the sender and synth set the ceiling.

## Tests

Portable regression tests (no Windows, Boost or GL): `safc-midi-scanner-tests`
compares the scanner with a port of the serial walk on clean, oversized,
undersized, junk-separated, truncated and corrupt fixtures and validates every
checkpoint against an independent replay; `safc-player-visuals-tests` checks
note matching, reclamation, ring growth, wrap-around, compaction, dirty pages
and the key-press stacks against brute-force models;
`safc-prepared-source-tests` checks the prepared stream, rewind, forked
readers and seeks against a port of the serial parser. The Windows piano
render test exercises both note paths.
