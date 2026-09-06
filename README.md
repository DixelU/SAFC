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

You can compile it fairly easily using MSVC 2022 + vcpkg [(dependencies list)](https://github.com/DixelU/SAFC/blob/develop/dependencies.txt) :)

Clone with `--recurse-submodules`, or run `git submodule update --init --recursive`,
so the shared `utility` headers and SYNCore's nested utility dependency are
available before building.

## Embedded SYNCore output

The CMake build includes [SAFSYNCore](https://github.com/DixelU/SAFSYNCore) as
an optional in-process synthesizer. Initialize nested submodules and configure
with the same static vcpkg triplet used by SAFC:

```text
git submodule update --init --recursive
cmake -S _SAFC_ -B build -DSAFC_ENABLE_SYNCORE=ON \
  -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --config Release --target _SAFC_
```

`SAFC_ENABLE_SYNCORE` defaults to `ON`; set it to `OFF` for the traditional
WinMM-only player. The checked-in Visual Studio solution also enables SYNCore
automatically when `SYNCore/SAFSYN/windows_synth.cpp` is present, and falls back
to a WinMM-only build when the submodule is absent.

In the player, select **SYNCore (embedded)** from the output list to use its
built-in sine instrument. Open **Settings... > SYNCore...** to choose an SF2/SFZ
bank (which also selects the embedded output), return to the built-in sine, and
configure sample rate, buffering, cohort limit, render threads, phase mode,
output gain, and the limiter. These preferences and the bank path are restored
on the next run. Player playback, editor playback, and editor note audition all
share the selected output. MP4 export uses the same SYNCore bank and synthesis
preferences; its separate AAC rate controls only the encoded audio stream.
MP4 export also uses the player's overlap removal mode (Overlaps drawn, Naive OR,
or R/t OR). The mode is captured when rendering starts and applies to both the
render preview and the exported video.

Embedded file playback waits for MIDI queue capacity so dense bursts preserve
held notes and later controller automation. Sustained overload can delay playback;
Stop and Pause interrupt capacity waits. Natural completion releases the notes
and lets their decay finish.

## SAST Tools 

[PVS-Studio](https://pvs-studio.com/pvs-studio/?utm_source=website&utm_medium=github&utm_campaign=open_source) - static analyzer for C, C++, C#, and Java code.
