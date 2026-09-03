# Player Video Renderer Review and Remediation Checklist

This document records the review findings for the initial combined MIDI video/audio
renderer implementation. It is intentionally kept beside the implementation so the
requirements, tradeoffs, and remaining verification work survive conversational context.

Status values used below are `open`, `in progress`, `fixed`, and `accepted limitation`.

## Correctness and lifecycle findings

1. **AAC settings exceed the Media Foundation encoder contract** — `fixed`
   - The UI/exporter accepts sample rates from 8–96 kHz and arbitrary audio bitrates from
     24–1024 kbps.
   - The Windows AAC encoder used here accepts 44.1 or 48 kHz PCM and the discrete stereo
     bitrates 96, 128, 160, and 192 kbps.
   - Reproduced failures: 96 kHz and 200 kbps both fail with `0xc00d36b4`.
   - Resolution: export now has an explicit AAC sample rate, accepts only 44.1/48 kHz and
     96/128/160/192 kbps, constrains the UI to those values, and rejects invalid API input
     before creating a temporary or destination file.
   - Evidence: integration tests cover unsupported rate/bitrate rejection and destination
     preservation; the 44.1 kHz/96 kbps SMPTE export succeeds.

2. **SMPTE MIDI audio and visual timing diverge** — `fixed`
   - SYNCore's SMF scheduler supports SMPTE division, while `simple_player` interprets the
     raw division word as PPQ.
   - Resolution: regular-SMF audio and visuals now consume the same SYNCore scheduling
     semantics. The visual path receives scheduled microsecond events rather than
     reinterpreting the division word.
   - Evidence: a real SMPTE -25/40 fixture renders the expected one-second program plus
     three-second lead-in, with matching audio and video durations.

3. **Renderer clears caller-owned cancellation** — `fixed`
   - `render_simple_player_video` resets the supplied atomic flag at entry, losing a cancel
     request made immediately after task submission.
   - Resolution: the caller token is read-only. A separate internal token coordinates peer
     cancellation without clearing or setting caller-owned state.
   - Evidence: pre-cancel and mid-render cancellation tests assert both preservation of the
     caller token and preservation of an existing destination.

4. **Application shutdown does not cancel an active export** — `fixed`
   - The singleton worker drains by default, the task ignores its stop token, and
     `gl_close()` does not request video-render cancellation.
   - Resolution: the dedicated UI module owns the worker, bridges its stop token into the
     renderer cancellation state, and `gl_close()` cancels and joins it before player,
     ImGui, OpenGL, or window teardown.
   - Evidence boundary: the shutdown path compiles in the full application and its ordering
     was source-reviewed; closing the real GUI during an active encode remains a manual QA
     item.

5. **Media Foundation child threads use COM without per-thread initialization** — `fixed`
   - Only the parent worker thread calls `CoInitializeEx`; both encoder threads use COM
     interfaces.
   - Resolution: the parent and both encoder threads use balanced MTA COM guards. Media
     Foundation startup/shutdown remains owned by the parent scope, and sink-writer calls
     are serialized.
   - Evidence: muxed H.264/AAC integration exports pass, and the x64 Release application
     build includes the same path.

6. **Failed/cancelled exports damage the selected destination** — `fixed`
   - The final path is opened before encoder negotiation. Reproduced invalid settings leave
     a zero-byte file, and cancellation skips finalization.
   - Resolution: source/output/bank collisions and parent/extension constraints are
     validated first. Rendering targets a unique sibling temporary file; only a finalized
     file is atomically moved over the destination, and failure removes the temporary.
   - Evidence: invalid-settings, collision, pre-cancel, and mid-render-cancel tests verify
     that a pre-existing destination is unchanged.

7. **Progress reporting bypasses its own event throttle** — `fixed`
   - Two callbacks are emitted around every same-sample MIDI batch. The small 10.8-second
     demo generated 416 callbacks.
   - Resolution: ordinary progress delivery is time-throttled to 100 ms; only explicit
     stage boundaries and preview frames force delivery.
   - Evidence: the SMPTE integration export asserts a bounded callback count.

8. **Offline export silently inherits the live cohort-stealing ceiling** — `fixed`
   - Dense output may lose voices while still returning success.
   - Resolution: offline export defaults to dynamic cohort growth. The UI exposes an
     optional explicit maximum, and any capacity steals are returned and shown as a
     warning.
   - Evidence: a constrained prepared-event export verifies both the steal metric and
     visible warning result.

9. **Concurrent error capture can hide the originating failure as cancellation** — `fixed`
   - The shared cancel flag is set before the real exception is recorded, so the peer can
     record a synthetic cancellation first.
   - Resolution: substantive exceptions are recorded before internal peer cancellation is
     broadcast, and cancellation consequences cannot replace a substantive failure.
   - Evidence: a throwing prepared source verifies that its diagnostic wins over peer
     cancellation.

10. **Render status text jumps into other windows while either window moves** — `fixed`
   - `text_box` retained a reference to a mutable `single_text_line_settings` cursor. Most
     controls use the same `system_white` preset, so moving one window changed the layout
     cursor used while another textbox rebuilt its lines.
   - The render worker also called `safe_string_replace` on the GUI control directly. That
     made formatting race the UI thread's draw and window-move paths.
   - Resolution: each textbox now owns its layout cursor, and the worker only publishes a
     status string/serial under the preview-state lock. A dedicated status pane applies
     pending text during its UI-thread `draw()`.
   - Evidence: the focused test checks that two textboxes constructed from one preset own
     distinct layout cursors, and the x64 Release application compiles and links with the
     UI-thread handoff. Moving the real windows remains a manual confirmation item.

11. **Video export needlessly starts on software-only rendering paths** — `fixed`
   - The offscreen context requested `PFD_DRAW_TO_BITMAP`, which normally selects the
     generic software OpenGL implementation. Every pixel was also channel-swizzled in C++,
     and the render loop issued redundant flush/finish calls before synchronous readback.
   - The Media Foundation sink writer was created without opting into hardware transforms,
     so a hardware H.264 encoder could not be selected by the sink-writer path.
   - Resolution: export first creates a hidden window-backed WGL context and reports whether
     its selected pixel format is accelerated; the bitmap context remains a supported
     fallback. BGRA-capable contexts now use row copies, redundant synchronization was
     removed, and the sink writer enables hardware transforms while retaining normal
     software fallback behavior.
   - Evidence: the integration export reports accelerated OpenGL on this machine and renders
     240 nonblank 640x360 frames at 60 fps into a valid muxed file. This proves path selection
     and correctness, not a speedup for the user's long/dense MIDI or selection of a hardware
     H.264 MFT.

## Overbuilt or unfinished areas

1. **Duplicated SMF audio rendering loop** — `fixed`
   - The exporter duplicates scheduling, dispatch, tail, mastering, statistics, and
     cancellation logic from `SYNCore/SAFSYN/smf_renderer.cpp`.
   - Resolution: SYNCore now owns a reusable timed-MIDI-to-PCM core. Both its WAV renderer
     and the MP4 exporter use that scheduling, dispatch, phase preparation, mastering,
     tail, progress, cancellation, and metrics implementation.

2. **Duplicated visual MIDI parser** — `fixed`
   - The offline visual parser repeats the live parser and has already diverged in timing
     behavior.
   - Resolution: deterministic offline visuals now consume `playback_event_source` events.
     Regular SMF export adapts SYNCore's scheduler, while prepared archives use independent
     cursors over their existing page store.

3. **Unused or redundant state/API** — `fixed`
   - `manual_visual_clock`, `offline_visual_render_done()`, `progress_serial`, an unused
     render-settings parameter, and several progress statistics have no effective consumer.
   - Resolution: the unused manual clock, completion query, progress serial/state, render
     parameter, statistics, and special exporter-only draw overload were removed.

4. **Video-render UI is embedded in the monolithic application source** — `fixed`
   - Preview/state/settings code adds hundreds of lines to `_SAFC_.cpp`.
   - Resolution: renderer settings, preview, worker lifecycle, status, validation, and file
     dialog behavior live in `SAFCGUIF_Local/player_video_render_ui.*`; `_SAFC_.cpp` retains
     application wiring only.

5. **Large-MIDI path is not bounded and prepared archives are unsupported** — `fixed`
   - The regular MIDI is copied into SYNCore-owned memory and separately mapped for visual
     parsing. Prepared archives are rejected.
   - Resolution: regular SMF uses one parsed/scheduled representation rather than a second
     visual parser. Prepared compressed page stores can fork independent streaming readers
     for audio and visuals without copying raw events. Offline visual state rejects more
     than 2,000,000 buffered notes instead of growing without limit.
   - Accepted limitations: SYNCore's standard-SMF scheduler still owns its parsed event
     list in memory, and the prepared compressed format still rejects SMPTE files at import.

6. **Exporter-specific automated coverage is absent** — `fixed`
   - Required coverage: SMPTE timing, supported/unsupported AAC settings, pre-cancel and
     mid-render cancel, shutdown, output preservation, exception precedence, bounded
     progress callbacks, cohort policy, and muxed stream duration/content.
   - Added coverage: a Windows integration target exercises actual compressed page-store
     preparation/forking, invalid AAC settings, cancellation, collisions, output
     preservation, SMPTE scheduling, muxed H.264/AAC markers and durations, progress
     throttling, cohort warnings, and exception precedence.
   - Remaining manual coverage: closing the GUI during an active render, preview behavior,
     listening quality, heavy/black-MIDI throughput, and all phase modes.

7. **Representative end-to-end performance evidence is still absent** — `open`
   - The exporter has functional and medium-resolution coverage, but no checked-in benchmark
     represents the user's long/dense MIDI, selected bank, phase mode, resolution, and audio
     tail. Audio synthesis, video drawing/readback, and H.264 encoding are only reported as
     broad progress stages, so the dominant cost is not yet observable from the UI.
   - Next evidence: rerun the reported file with the rebuilt application, record wall time
     and whether the UI says accelerated or software OpenGL, then compare stage progress and
     inspect/listen to the finalized MP4. Add finer timing or ETA only if that run remains
     unexpectedly slow; do not infer player-versus-export parity because offline rendering
     also encodes every video frame and synthesizes/writes all audio samples.

## Additional soundness hardening found during remediation

- Prepared/timed event sources now continue synthesizing from their final channel event to
  their declared duration before all-notes-off is applied. This prevents early release when
  a prepared stream ends before its declared program duration; a SYNCore regression test
  checks for audible energy near that boundary.
- Audio/video timestamp arithmetic now uses quotient/remainder scaling, validates the
  Media Foundation signed timestamp range before reserving output, and avoids multiplying
  a frame index by one million before division.

## Changes that are justified and should remain

- Embedded Media Foundation H.264/AAC output without an external FFmpeg process.
- Offscreen OpenGL rendering for the existing player visuals.
- The general three-second player lead-in and the matching approximately 4.2-second visible
  window.
- The render preview concept, after throttling and modularization.
- Direct `<set>` inclusion and the Media Foundation project/link configuration.

## Platform contract references

- [AAC Encoder](https://learn.microsoft.com/en-us/windows/win32/medfound/aac-encoder)
  documents the supported sample-rate/bitrate combinations used by validation.
- [The COM Library](https://learn.microsoft.com/en-us/windows/win32/com/the-com-library)
  documents per-thread COM initialization and balanced uninitialization.
- [`IMFSinkWriter::WriteSample`](https://learn.microsoft.com/en-us/windows/win32/api/mfreadwrite/nf-mfreadwrite-imfsinkwriter-writesample)
  documents sink-writer input throttling, which remains enabled.
- [`IMFSinkWriter::Finalize`](https://learn.microsoft.com/en-us/windows/win32/api/mfreadwrite/nf-mfreadwrite-imfsinkwriter-finalize)
  documents why only a successfully finalized temporary is committed as destination.
- [`MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS`](https://learn.microsoft.com/en-us/windows/win32/medfound/mf-readwrite-enable-hardware-transforms)
  documents the explicit sink-writer opt-in required for hardware encoders.
- [`PIXELFORMATDESCRIPTOR`](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-pixelformatdescriptor)
  documents the generic-software and generic-accelerated pixel-format flags used to report
  the active OpenGL path.

## Verification baseline

- Clean `_SAFC_.sln` x64 Release rebuild passed before remediation.
- Default 48 kHz/192 kbps smoke export produced an H.264/AAC MP4 with 108 video frames and
  513,981 audio frames.
- This baseline did not prove GUI interaction, listening quality, heavy/black-MIDI
  performance, prepared archives, or every phase mode.

## Verification after remediation

- `safc-video-export-tests`: 1/1 passed. This single integration executable contains the
  focused exporter cases listed above and performs real Media Foundation MP4 writes.
- The SMPTE case now renders 240 frames at 640x360/60 fps, verifies a nonblank preview, and
  reported `accelerated OpenGL` on this machine. It also covers textbox layout-cursor
  isolation.
- SYNCore CTest suite: 8/8 passed, including playback, phase, cohorts, mastering, SMF,
  demo render, and coherent-render hash tests.
- `_SAFC_.sln` x64 Release compiled through the final link step. Because the user's active
  render held `x64/Release/SAFC.exe` open, the same build linked successfully to the isolated
  verification output `build/codex-full-release/SAFC.exe` without interrupting that render.
- `git diff --check` passed; Git reported only the checkout's expected LF-to-CRLF notices.
- Not run: interactive GUI cancellation/preview, subjective listening, device playback,
  moving both affected windows in the rebuilt executable, heavy/black-MIDI performance, or
  exhaustive phase-mode runtime tests.
