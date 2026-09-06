# Application source map

`../_SAFC_.cpp` initializes the shared player/editor and selects the GUI or CLI
runtime. The application is compiled as separate translation units; no source
file includes another `.cpp` file.

| Source | Responsibility |
| --- | --- |
| `app_state.cpp` | File settings, merge data, shared application state, alerts, UI lookup, and memory query |
| `app_workers.h` | Worker singleton policy and shared tag types used by submission and shutdown |
| `dialogs.cpp` | Native MIDI, archive, bank, and save dialogs |
| `file_actions.cpp` | File list changes and global processing overrides |
| `file_properties.cpp` | Per-file settings, cut/transpose controls, and volume maps |
| `midi_analysis.cpp` | MIDI information collection, graphs, time maps, and data export |
| `merger.cpp` | Merge startup, progress, and stage completion |
| `settings.cpp` | Application settings and registry restoration |
| `player_controls.cpp` | Playback controls, output selection, and regular MIDI loading |
| `playback_source.cpp` | Compressed/archive source preparation and playback |
| `syncore_settings.cpp` | Embedded synthesizer preferences, bank selection, and status |
| `editor.cpp` | Editor loading/saving, playback, tracks, channels, and editing callbacks |
| `window_layout.cpp` | Player/editor maximize and restore layouts |
| `ui.cpp` | Window construction and callback wiring |
| `gui_runtime.cpp` | GUI startup, GLUT events, rendering, and shutdown |
| `cli_runtime.cpp` | CLI help, JSON configuration, and command-line processing |
| `update.cpp` | Executable version, release checks, downloads, and archive extraction |

Module headers declare callbacks and state used across source files. Implementation
helpers stay local where possible. `app_state.h` also provides the legacy GUI and
MIDI include dependencies; application data has a single definition in
`app_state.cpp`. Shared legacy header objects are inline variables.

Worker tag types must be declared in `app_workers.h` before use. A callback-local
tag would create a different worker from the one stopped by GUI shutdown. Worker
shutdown ordering remains in `gui_runtime.cpp`.

Both CMake entry points and the Visual Studio project explicitly list the same
application sources. The JSON parser's two implementation files are also compiled
separately. Add new sources to all three build definitions.

Configure with `-DSAFC_BUILD_APP_TESTS=ON`, build `safc-app-workers-tests`, and run
`ctest --test-dir <build-directory> -C Release -R safc-app-workers --output-on-failure`
to check worker identity and cancellation across translation units. The existing
video export regression remains available through `SAFC_BUILD_VIDEO_EXPORT_TESTS`.
