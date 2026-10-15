# Deque audit, October 15, 2026

The audit covered the application, SYNCore, the utility submodules, historical
utility versions, and implicit `std::queue`/`std::stack` containers. The two
remaining high-frequency candidates were the synth's per-sample mastering
windows and the editor's note-pairing queues during file loading. Only the
mastering replacement was retained after measurement.
The player already hands events between threads through `buffered_queue_spsc`;
its event heaps use vectors. SYNCore's pending MIDI ring was changed in the
previous pass.

## Inventory

| Location | Work performed | Decision |
|---|---|---|
| `SYNCore/SAFSYN/mastering.h`: `pending_`, `maximums_` | Stereo lookahead delay and sliding peak maximum, updated for every sample frame during live playback and mastered file rendering | Replace with contiguous windows allocated once for the configured lookahead |
| `midi_editor.h`: `active_notes` | Pair note-on/off events in FIFO order while loading each MIDI track | Retain the deque: alternatives helped ordinary notes but regressed sustained overlaps |
| `single_midi_info_collector.h`: `recent_note_hits` | One-second notes-per-second window, updated once per distinct tick during a file-analysis job | Retain the existing Boost deque with 64 KiB blocks; this is not the synth's analytic rendering mode |
| `simple_player_video_export.cpp`: `audio_queue_`, `video_queue_` | Merge encoded-input samples by timestamp; bounded to 16 audio blocks and four video frames | Retain; operations occur per block/frame alongside buffer creation, copying and Media Foundation encoding |
| `utility/include/background_worker.h`: `queue_` | Schedule whole load/save/playback jobs | Retain; this does not carry individual MIDI events or audio samples |
| `SAFGUIF/editbox.h`: `words` | Legacy text editing; cursor state includes a container iterator | Retain; not part of the ImGui playback path, and changing storage requires a separate cursor-validity review |
| `SAFGUIF/snp_list.h`: three selector containers | Legacy file/device/track lists; drawing traverses visible entries | Retain; no demonstrated playback bottleneck |
| `utility/src/mctx_path_detail.h`, `utility/include/mctx_path.h` | Context-path segments and optional result container | No active application or synth hot-path calls found |
| `utility/versions/**`, mirrored `SYNCore/third_party/utility/**` historical sources | Older thread-pool, background-worker, context and logging implementations | Not active playback implementations; leave untouched |

`app_state.h` also includes `<deque>` without declaring one. Benchmark and test
reference deques remain intentionally. There are no `std::queue` or `std::stack`
instances hidden behind their default deque storage; the remaining
`std::priority_queue` instances use vector storage.

Replacing every occurrence would add custom ownership and iterator machinery
to paths where container overhead has not been shown to matter. This pass keeps
the changes local, without additional threads, SIMD requirements, settings, or
changes to playback timing policy.

## Implementation

Mastering now allocates two vectors when the processor is constructed, rounding
`lookahead_frames + 1` up to a power of two. The delayed samples and monotonic
peak window reuse those buffers; wraparound uses a mask. Peak ties, expiry,
gain calculation, output delay and final tail
flushing retain their existing order. No extra allocation is needed in these
windows while processing. Both direct and analytic synthesis feed this same
mastering stage. Zero-lookahead processing sends the current frame directly
through the shared gain calculation, covering inactive and gain-only settings.

The full mastering benchmark measures 960,000 stereo frames (20 seconds at
48 kHz), including processing and tail flushing. Delayed limiting takes 13-29%
less time in the tested x64/x86 cases; zero-window cases take 33-53% less.
For random peaks and 5 ms lookahead, x64 falls from 17.777 to 15.065 ms and
x86 from 20.491 to 15.898 ms. All audio checksums and measured statistics match.
These are small absolute savings in one stage, not equivalent improvements in
whole-player CPU use. See [the SYNCore measurements](../../SYNCore/docs/MIXER_PERFORMANCE.md#mastering-windows-october-15-2026)
for the methodology and raw data.

## Editor alternatives rejected

The editor currently destroys each map entry and its deque once that pitch's
pending notes are exhausted. This makes allocation cost conspicuous in tracks
with repeated non-overlapping notes. Compacting vector FIFOs, circular vectors
and a compacting FIFO with inline storage were compared against the existing
parser. The final inline-storage candidate roughly halved a synthetic
500,000-note non-overlapping file's full load time. However, sustained overlapping
queues at depths 1 and 64 repeatedly took 7-8% longer on x64. Output content and
note IDs matched, but that performance tradeoff is unsuitable for this project's
dense MIDI workloads. None of those editor storage changes are retained.

The last candidate's x64 complete-load medians were:

| Sustained overlap depth per key | Original (ms) | Rejected candidate (ms) |
|---:|---:|---:|
| 0 | 77.480 | 38.149 |
| 1 | 24.694 | 26.522 |
| 64 | 25.119 | 27.226 |
| 4096 | 27.584 | 27.023 |

These are one before/after process pair with seven timed loads per case,
pinned to logical CPU 0 on the same i5-13600K, MSVC `/O2 /std:c++latest /MT`.
Earlier vector variants also regressed sustained cases in repeated comparisons.
The timings justify rejecting these implementations; they are not a universal
estimate for real MIDI files. The supplied multi-gigabyte files were not loaded
into the editor during this experiment.

The optional `safc-editor-load-benchmark` target remains available under
`SAFC_BUILD_VISUAL_BENCHMARKS`. With no arguments it generates files with
500,000 note pairs and sustained overlap depths 0, 1, 64 and 4096, warms each
case, and reports median complete load time over seven repetitions. Checksums
include note identities and are calculated outside timing. It also accepts a
MIDI path for a three-repetition real-file comparison. These measurements cover
loading, not per-frame drawing or live event scheduling.

## Validation

All nine SYNCore Release CTest suites pass on x64 and x86, including live
playback, mastering, both phase modes and the unchanged coherent-render hash.
All six focused application integration suites pass (visual algorithms,
editor, playback, piano rendering, smoke and workflows). Both native ImGui
applications and the optional editor-load benchmark build successfully.

Muted two-second WASAPI checks pass in direct mode on x64 and analytic mode on
x86, with zero underruns, missing frames, rejected MIDI or empty device buffers.
These device checks use the built-in test instrument and verify integration;
they do not establish dense real-file playback performance.
