Simple AF Complex (SAFC)
==========================

Allows merging completely different midis in multitude of ways!
Also aims at restoring badly damaged/formatted midi files that other editors, players and tools cannot parse!
Has additional embedded midis player and piano roll editor.

And other funky stuff :P

## Important notice: 
**Currently is being supported as legacy project.** Update including a complete overhaul is ~~coming soon~~ delayed for indeterminate amount of time.

## Feature Manual
Has partially been migrated to github wiki - https://github.com/DixelU/SAFC/wiki

## Other stuff
There's discord server, where SAF apps (SAFC included) were publishing all that time since July/August of 2018: https://discord.gg/CsgEW4P

You can compile it using MSVC + vcpkg ([dependencies list](./dependencies.txt)).

Clone with `--recurse-submodules`, or run `git submodule update --init --recursive`,
so the shared `utility` headers and SYNCore's nested utility dependency are
available before building.

The default CMake application now uses a complete [Dear ImGui workspace](./_SAFC_/imgui/README.md)
for processing, playback, editing, analysis, transform maps, and video export,
while retaining SAFC's folded panels and partial outlines. `SAFC` is the primary
target; `SAFCImGui` remains an equivalent executable for preview-era scripts.
Shared domain code and the classic UI modules are organized under
[`_SAFC_/app`](./_SAFC_/app/README.md). [SAFC2.sln](./SAFC2.sln) builds both
interfaces through native Visual C++ projects. The original `_SAFC_.sln` builds
the classic interface; CMake can also build it with
`SAFC_BUILD_LEGACY_GUI=ON` as `SAFCLegacy`.

## Visual Studio / MSBuild

Open `SAFC2.sln` in Visual Studio 2026 with the C++ desktop workload, v145
toolset, and Windows SDK 10.0.26100.0. Install the static vcpkg packages in
`dependencies.txt` and enable MSBuild integration with `vcpkg integrate install`.
The solution contains `SAFC2` (Dear ImGui) and `SAFCLegacy` (SAFGUIF), with
Debug/Release configurations for x64 and x86. CMake is not required for this path.

From a Visual Studio developer shell at the repository root:

```powershell
MSBuild.exe SAFC2.sln /m /p:Configuration=Release /p:Platform=x64
```

Both projects retain the executable name `SAFC.exe`, in separate directories:

- ImGui: `build/msbuild/x64/Release/SAFC2/SAFC.exe`
- Classic: `build/msbuild/x64/Release/SAFCLegacy/SAFC.exe`

Use `/p:Platform=x86` for 32-bit builds (the output directory is `Win32`), or
`/p:Configuration=Debug` for Debug. Add `/t:SAFC2` or `/t:SAFCLegacy` to build
one frontend. In Visual Studio, set `SAFC2` as the startup project to run the
new interface. Both executables embed `_SAFC_/_SAFC_.rc`, including its version
and icon. The original `_SAFC_.sln` retains its existing output directories.

## Embedded SYNCore output

The CMake build includes [SAFSYNCore](https://github.com/DixelU/SAFSYNCore) as
an optional in-process synthesizer. Initialize nested submodules and configure
with the same static vcpkg triplet used by SAFC:

```text
git submodule update --init --recursive
cmake -S _SAFC_ -B build -DSAFC_ENABLE_SYNCORE=ON \
  -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --config Release --target SAFC
```

`SAFC_ENABLE_SYNCORE` defaults to `ON`; set it to `OFF` for the traditional
WinMM-only player. The checked-in Visual Studio solution also enables SYNCore
automatically when `SYNCore/SAFSYN/windows_synth.cpp` is present, and falls back
to a WinMM-only build when the submodule is absent.

In the player, select **SYNCore (embedded)** from the output list to use its
built-in sine instrument. Open **Settings... > SYNCore...** to choose an SF2/SFZ
bank (which also selects the embedded output), return to the built-in sine, and
configure sample rate, buffering, cohort limit, render threads, phase mode,
output gain, the limiter, and overload shedding. These preferences and the bank
path are restored on the next run. Player playback, editor playback, and editor note audition all
share the selected output. Video export uses the same SYNCore bank and synthesis
preferences; its separate audio rate controls only the encoded audio stream.
It encodes through the installed Windows Media Foundation encoders: H.264, HEVC,
or AV1 in MP4 with AAC, ALAC, or FLAC audio, and WMV 9 or VC-1 in WMV with WMA,
WMA Pro, or PCM audio. Besides CBR and VBR it offers Quality and constant-QP rate
control and a flat background colour for keying. **Advanced encoder options**
choose GPU or software encoders, the H.264 profile, 4:2:2/4:4:4 chroma and
10-bit depth (HEVC and AV1 on capable GPUs), the colour matrix and range, the
chroma filter, the speed/quality preset, keyframe interval, B-frames, QP limits,
and MP4 fast start. Settings an encoder rejects are named before rendering;
settings it ignores are listed with the result.
Video export also uses the player's overlap removal mode (Overlaps drawn, Naive OR,
or R/t OR). The mode is captured when rendering starts and applies to both the
render preview and the exported video.

Embedded file playback waits for MIDI queue capacity so dense bursts preserve
held notes and later controller automation. Sustained overload can delay playback;
Stop and Pause interrupt capacity waits. Natural completion releases the notes
and lets their decay finish.

The player keeps its falling notes in GPU buffer objects and uploads only the
notes that arrived or ended since the last frame, scans tracks in parallel
when a file is opened, and seeks from checkpoints instead of replaying from the
start. **Parallel decode** in the player panel additionally decodes plain MIDI
files on a thread pool for the next open; files whose track layout cannot be
verified keep the repair-tolerant serial parser. See
[PLAYER_PERFORMANCE.md](./_SAFC_/SAFC_InnerModules/PLAYER_PERFORMANCE.md) for the
measurements and design.

**Shed quiet notes when overloaded** (off by default) trades completeness for
timing in realtime playback: once the synth has used up half of its audio
buffer, the quietest notes are dropped, loudest last, until it catches up.
Controllers, program changes, and pitch bends are never dropped. Video export
always renders every note.

## SAST Tools 

[PVS-Studio](https://pvs-studio.com/pvs-studio/?utm_source=website&utm_medium=github&utm_campaign=open_source) - static analyzer for C, C++, C#, and Java code.
