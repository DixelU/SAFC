# Publishing an ImGui release

The ImGui application can replace the classic application through the existing
SAFC update feed. Changing GitHub's default branch does not distribute a build:
the updater consumes published versions and binary release assets. This document
prepares a future 2.0 release; it does not select a version or publish anything.

## Compatibility with existing installations

Classic 1.x clients query `https://api.github.com/repos/DixelU/SAFC/tags`, use the
**first returned tag**, and compare its numeric version with the running
executable's `PRODUCTVERSION`. They then request an exact asset name from that
tag's GitHub release:

| Installed architecture | Required release asset | Required archive entry |
| --- | --- | --- |
| x86 (32-bit) | `SAFC32.7z` | `SAFC.exe` at archive root |
| x64 (64-bit) | `SAFC64.7z` | `SAFC.exe` at archive root |

Use `SAFC.exe`, even if a build came from the equivalent `SAFCImGui` target. Do
not put the executable inside a versioned folder. The maintained ImGui updater
validates and installs the embedded-resource executable; the packaging helper
therefore produces a single-executable archive. Build with static dependencies
and verify that it runs without the build machine's dependency directories on
`PATH`. A package that needs accompanying DLLs requires corresponding installer
support before it can be offered through this feed.

The ImGui preferences retain the DWORD `AUTOUPDATECHECK` under
`HKCU\Software\SAFC`. Its default is enabled; an existing disabled value remains
disabled. Users who opted out, cannot reach GitHub, or cannot write their install
directory need a manual update. Shipping only `SAFC64.7z` does not migrate users
of the 32-bit application, including 32-bit applications running on 64-bit Windows.

The new updater uses GitHub's latest published stable release and exact numeric
versions. Existing clients still use the tag feed until they have upgraded.

## Prepare the build

1. Choose the release commit and a four-component numeric version, for example
   `2.0.0.0`. Use the matching tag `v2.0.0.0`. Do not bump or tag a preview solely
   to exercise the updater.
2. Update all four version declarations in `../_SAFC_.rc`: `FILEVERSION`,
   `PRODUCTVERSION`, and the `FileVersion` / `ProductVersion` strings. Both
   executable targets include this resource and the application icon. Rebuild
   after changing it; the updater reads the executable's embedded version.
3. Initialize nested submodules and install the dependencies from
   `../../dependencies.txt`, using `x64-windows-static` and
   `x86-windows-static` in separate build directories.
4. Configure the ImGui application with CMake, build Release `SAFC`, and run its
   tests for each architecture. Use an x64 developer environment for the x64
   Ninja build and an x86 developer environment for the x86 Ninja build; do not
   reuse a CMake cache across architectures. The checked-in Visual Studio
   solution builds the classic UI and is not the ImGui release build.
5. Check ordinary startup, playback, editor changes/save, archive loading,
   processing, video export, shutdown, and update staging in the release builds.
   Test startup from a shortcut with a different working directory as well.

The source does not declare the ImGui frontend x64-only. The dependency list
includes both triplets and SYNCore's inspected SIMD paths have architecture
guards with fallback code. That establishes a build route, not proof of a
working x86 release: a successful 32-bit build and runtime checks are still
required before promising migration for all existing installations. Account
for the smaller 32-bit address space when checking dense MIDI files.

## Create local assets

From the repository root, use PowerShell 5.1 or later and installed 7-Zip:

```powershell
./scripts/package-imgui-release.ps1 `
    -Executable ./build/release-x64/SAFC.exe -Architecture x64 -Version 2.0.0.0
./scripts/package-imgui-release.ps1 `
    -Executable ./build/release-x86/SAFC.exe -Architecture x86 -Version 2.0.0.0
```

These are examples for after the version decision and builds. To exercise
packaging before that decision, pass the actual current embedded version.

The helper checks the PE architecture, numeric file and product versions,
archive root, 7-Zip integrity, and the hash of the extracted executable. It
writes `build/packages/<version>/SAFC64.7z` or `SAFC32.7z` and reports its SHA-256.
It refuses to overwrite an existing package. `-OutputDirectory` selects another
destination; `-SevenZip` selects a specific `7z.exe`. It does not alter source,
version resources, tags, branches, or GitHub releases, and does not include a
user's settings, SoundFont, or project files.

Check the package independently on a clean Windows setup. The helper validates
packaging, not playback or the presence of every runtime dependency.

## Publish without exposing an incomplete update

1. Finish and validate both archives before exposing a new numeric tag.
2. Prepare a draft GitHub release with the intended tag and commit. Attach both
   final archives while it remains a draft. Avoid separately pushing the numeric
   tag early: legacy clients can discover it before the download URLs are ready.
   Verify that the tag is not exposed through the public tag feed while staging.
3. Check the assets, release commit, tag, embedded versions, and architecture
   names once more. Publish the completed release as a stable release and mark
   it **Latest**. This is the distribution step; a default-branch change is
   optional repository organization.
4. Immediately verify the public `/tags` first entry and `/releases/latest`
   identify the intended version and both download URLs work. The two feeds
   serve different generations of clients. Useful read-only checks:

   ```powershell
   gh api 'repos/DixelU/SAFC/tags?per_page=1' --jq '.[0].name'
   gh api repos/DixelU/SAFC/releases/latest --jq '{tag_name,draft,prerelease,assets:[.assets[].name]}'
   ```

5. Verify a disposable copy of the prior production build upgrades into the new
   executable with its preferences preserved. Verify the new updater's next
   version path using the offline updater regression fixtures; do not publish
   a dummy higher version to test production users' installations.

### Legacy preview-tag hazard

Classic clients do not read a release's prerelease flag. Their numeric parser
also accepts numeric prefixes: a tag such as `v2.0.0-beta` can be treated as
`2.0.0.0`. Marking that release **Prerelease** does not protect existing users
from its assets, and a tag without ready assets can make automatic updates fail.

Keep development builds untagged on branches, or distribute previews outside
the production repository/feed until intentionally releasing to existing
clients. A nonnumeric tag is not a dependable alternate channel either: if it
becomes the first tag, older clients may stop parsing before reaching a stable
release. Do not rely on tag creation order alone; inspect the actual API result.

GitHub's [release API documentation](https://docs.github.com/en/rest/releases/releases)
describes draft visibility, stable latest releases, and the `make_latest` option.
The compatibility details above come from SAFC's `../app/update.cpp`; old
installations keep that behavior until their executable has been replaced.
