# Static executable size

The normal SAFC2 Release build keeps the static CRT and static third-party
libraries. Whole-program optimization and link-time code generation are enabled
for Release; they do not change archive support or introduce runtime DLLs.

## Measurements on 2026-09-15

These are x64 MSVC Release executable sizes, not compressed package sizes.

| Build | Bytes | MiB |
| --- | ---: | ---: |
| Existing SAFC2 artifact built 2026-09-08 | 8,314,880 | 7.930 |
| Current-source SAFC2 with whole-program optimization | 8,291,328 | 7.907 |
| Same object files, libarchive using Windows crypto | 4,014,080 | 3.828 |

The first two rows also differ in source revision, so their small difference
does not isolate the effect of whole-program optimization. The last two rows
use identical SAFC object and resource files: changing the static libarchive
dependency saved 4,277,248 bytes (51.6%), leaving 48.4% of the original size.
Both used `/O2`, all existing archive format/filter registration, and both icon
resources.

The older 2026-09-03 experiment reached 3,251,712 bytes from 7,922,688 bytes
(41.0% remaining). It also disabled archive features, narrowed registration,
and used `/O1`. Neither experiment demonstrated an executable at 15% of its
original size.

## Why libarchive matters

The installed vcpkg libarchive 3.8.1 port enables its `crypto` feature by default,
selects OpenSSL, and explicitly disables Windows CNG. That pulls a substantial
part of static `libcrypto` into SAFC. Merely listing fewer `.lib` files does not
remove code that the selected archive implementation references.

Libarchive can instead use the Windows crypto APIs for MD5, SHA hashes, HMAC,
PBKDF2 and AES ZIP encryption/decryption. This still links libarchive and the
compression libraries statically. `bcrypt.dll` is a Windows system component;
no third-party DLL needs to accompany the executable.

This is an optional dependency experiment, not the default dependency setup.
The recipe below does not overwrite the installed vcpkg libraries. A future
vcpkg overlay would need to update the port's crypto dependency, CNG setting,
and generated CMake dependency wrapper together.

## Reproduce the native Windows crypto library

Use an x64 Visual Studio developer PowerShell with CMake 3.30+ and Ninja on
`PATH`. Run from the repository root. Point `$archiveSource` at the existing,
vcpkg-patched libarchive 3.8.1 source used by the installed dependency; adjust
the example paths for your workstation. No sources are downloaded by these
commands.

```powershell
$vcpkg = 'C:/vcpkg'
$archiveSource = "$vcpkg/buildtrees/libarchive/src/v3.8.1-e54dad0fb1.clean"
$probe = Join-Path (Get-Location) 'build/native-archive-probe'
$archiveBuild = Join-Path $probe 'libarchive'

$configure = @(
    '-S', $archiveSource, '-B', $archiveBuild, '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release',
    "-DCMAKE_TOOLCHAIN_FILE=$vcpkg/scripts/buildsystems/vcpkg.cmake",
    '-DVCPKG_TARGET_TRIPLET=x64-windows-static', '-DVCPKG_MANIFEST_MODE=OFF',
    '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded', '-DBUILD_SHARED_LIBS=OFF',
    '-DWINDOWS_VERSION=WIN10',
    '-DENABLE_OPENSSL=OFF', '-DENABLE_CNG=ON',
    '-DENABLE_BZip2=ON', '-DENABLE_LZ4=ON', '-DENABLE_LZMA=ON',
    '-DENABLE_ZSTD=ON', '-DENABLE_ZLIB=ON', '-DENABLE_WIN32_XMLLITE=ON',
    '-DENABLE_LIBXML2=OFF', '-DENABLE_MBEDTLS=OFF', '-DENABLE_NETTLE=OFF',
    '-DENABLE_EXPAT=OFF', '-DENABLE_LZO=OFF', '-DENABLE_LIBB2=OFF',
    '-DENABLE_PCREPOSIX=OFF', '-DENABLE_PCRE2POSIX=OFF', '-DPOSIX_REGEX_LIB=NONE',
    '-DENABLE_ICONV=OFF', '-DENABLE_ACL=OFF', '-DENABLE_XATTR=OFF',
    '-DENABLE_UNZIP=OFF', '-DENABLE_TAR=OFF', '-DENABLE_CPIO=OFF',
    '-DENABLE_CAT=OFF', '-DENABLE_TEST=ON', '-DENABLE_WERROR=OFF'
)
cmake @configure
if ($LASTEXITCODE) { throw 'Libarchive configuration failed' }
cmake --build $archiveBuild --target archive_static libarchive_test --parallel 4
if ($LASTEXITCODE) { throw 'Libarchive build failed' }

& "$archiveBuild/bin/libarchive_test.exe" -r "$archiveSource/libarchive/test" `
    test_archive_md5 'test_archive_sha*' 'test_read_format_7zip*' `
    'test_read_format_zip*' 'test_read_format_xar*'
if ($LASTEXITCODE) { throw 'Libarchive tests failed' }
```

This retains the installed port's zlib, bzip2, lzma/xz, lz4, zstd and XML/XAR
capabilities, with CNG replacing OpenSSL. The other disabled features match the
installed port's feature selection. The output is `libarchive/archive.lib`.

## Compare the same SAFC object files

First build SAFC2 x64 Release normally with MSBuild. The following reads its
recorded link command, redirects every output to the probe directory, replaces
the archive library and OpenSSL dependency, and relinks serially. It does not
modify the project or the normal executable. Set `$tlog` to the corresponding
path if the original build used an overridden `IntDir`.

```powershell
$tlog = 'build/msbuild/x64/Release/obj/SAFC2/SAFC2.tlog/link.command.1.tlog'
$candidate = Join-Path $probe 'SAFC2'
[IO.Directory]::CreateDirectory($candidate) | Out-Null
$response = (Get-Content -LiteralPath $tlog | Select-Object -Skip 1) -join "`r`n"
foreach ($output in @(
    @('OUT', 'SAFC.exe'), @('PDB', 'SAFC.pdb'),
    @('IMPLIB', 'SAFC.lib'), @('LTCGOUT', 'SAFC.iobj')
)) {
    $replacement = '/' + $output[0] + ':"' + (Join-Path $candidate $output[1]) + '"'
    $response = $response -replace ('/' + $output[0] + ':"[^"]+"'), $replacement
}
$response = $response -replace '\bARCHIVE\.LIB\b', ('"' + $archiveBuild + '/libarchive/archive.lib"')
$response = $response -replace '\bLIBCRYPTO\.LIB\b', 'bcrypt.lib'
$response += ' /MAP:"' + (Join-Path $candidate 'SAFC.map') + '"'
$responseFile = Join-Path $probe 'link-native.rsp'
[IO.File]::WriteAllText($responseFile, $response, [Text.Encoding]::Unicode)
link "@$responseFile"
if ($LASTEXITCODE) { throw 'SAFC probe link failed' }
Get-Item -LiteralPath (Join-Path $candidate 'SAFC.exe') | Select-Object Length
dumpbin /dependents (Join-Path $candidate 'SAFC.exe')
& (Join-Path $candidate 'SAFC.exe') --help
```

## Validation and remaining checks

The x64 native-crypto probe passed 111 upstream digest, 7zip, ZIP and XAR read
tests, with 1,625,528 assertions and no failures. This included AES-128 and
AES-256 ZIP decryption. Two filename-conversion tests requiring an unavailable
EUC-JP locale were skipped. The relinked SAFC executable passed `--help`, and
`dumpbin /dependents` showed only Windows system DLLs.

This does not establish complete libarchive API equivalence: the Windows
backend lacks RIPEMD160 digest generation for mtree writing, which SAFC's
archive reader does not use. It also does not add support for encrypted
archive formats that libarchive already rejects.

Before adopting this dependency in releases, run SAFC's archive playback,
nested-member selection, export and updater extraction tests against it, and
validate x86 and Debug builds as well as GUI startup. The application-level
checks were not part of this dependency probe. Keep the source version and
feature selection pinned when turning the recipe into a maintained build.
