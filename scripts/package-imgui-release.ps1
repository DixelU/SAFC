#Requires -Version 5.1
<#
.SYNOPSIS
Builds a local, version-checked SAFC updater archive. Nothing is published.
.EXAMPLE
./scripts/package-imgui-release.ps1 -Executable ./build/release-x64/SAFC.exe -Architecture x64 -Version 2.0.0.0
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $Executable,
    [Parameter(Mandatory = $true)] [ValidateSet('x86', 'x64')] [string] $Architecture,
    [Parameter(Mandatory = $true)] [string] $Version,
    [string] $OutputDirectory,
    [string] $SevenZip
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($Version -notmatch '^[vV]?[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$') {
    throw 'Version must have four numeric components, for example 2.0.0.0 or v2.0.0.0. Prerelease tags are not accepted.'
}
$versionParts = @($Version.TrimStart([char[]]'vV').Split('.') | ForEach-Object {
    $part = [UInt32]::Parse($_, [Globalization.CultureInfo]::InvariantCulture)
    if ($part -gt 65535) { throw 'Each version component must be between 0 and 65535.' }
    $part
})
$numericVersion = $versionParts -join '.'
if ($numericVersion -eq '0.0.0.0') { throw 'A release must have a nonzero version.' }

$source = Get-Item -LiteralPath $Executable
if ($source.PSIsContainer) { throw 'Executable must name a built Windows executable.' }
$sourcePath = $source.FullName
$info = [Diagnostics.FileVersionInfo]::GetVersionInfo($sourcePath)
$productVersion = @($info.ProductMajorPart, $info.ProductMinorPart, $info.ProductBuildPart, $info.ProductPrivatePart) -join '.'
$fileVersion = @($info.FileMajorPart, $info.FileMinorPart, $info.FileBuildPart, $info.FilePrivatePart) -join '.'
if ([string]::IsNullOrWhiteSpace($info.ProductVersion) -or
    $productVersion -ne $numericVersion -or $fileVersion -ne $numericVersion) {
    throw "Version mismatch: requested $numericVersion; executable PRODUCTVERSION=$productVersion, FILEVERSION=$fileVersion. Rebuild with matching version resources."
}

$reader = [IO.BinaryReader]::new([IO.File]::OpenRead($sourcePath))
try {
    if ($reader.BaseStream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5a4d) {
        throw 'The input does not have a Windows DOS/PE header.'
    }
    $reader.BaseStream.Position = 0x3c
    $peOffset = $reader.ReadInt32()
    if ($peOffset -lt 64 -or $peOffset -gt $reader.BaseStream.Length - 26) {
        throw 'The input has an invalid PE header offset.'
    }
    $reader.BaseStream.Position = $peOffset
    if ($reader.ReadUInt32() -ne 0x4550) { throw 'The input has no PE signature.' }
    $machine = $reader.ReadUInt16()
    $reader.BaseStream.Position = $peOffset + 22
    $characteristics = $reader.ReadUInt16()
    $optionalMagic = $reader.ReadUInt16()
    $expectedMachine = if ($Architecture -eq 'x64') { 0x8664 } else { 0x14c }
    $expectedMagic = if ($Architecture -eq 'x64') { 0x20b } else { 0x10b }
    if ($machine -ne $expectedMachine -or $optionalMagic -ne $expectedMagic) {
        throw ('Architecture mismatch: requested {0}; PE machine is 0x{1:x4}.' -f $Architecture, $machine)
    }
    if (($characteristics -band 2) -eq 0 -or ($characteristics -band 0x2000) -ne 0) {
        throw 'The input must be an executable application, not a DLL.'
    }
}
finally { $reader.Dispose() }

if ($SevenZip) {
    $zipPath = (Get-Item -LiteralPath $SevenZip).FullName
} else {
    $zipCommand = Get-Command 7z.exe, 7zz.exe, 7za.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($zipCommand) { $zipPath = $zipCommand.Source }
    else {
        $zipPath = Join-Path $env:ProgramFiles '7-Zip/7z.exe'
        if (-not (Test-Path -LiteralPath $zipPath -PathType Leaf)) {
            throw '7-Zip was not found. Install 7-Zip or provide -SevenZip <path-to-7z.exe>.'
        }
    }
}
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path (Split-Path -Parent $PSScriptRoot) "build/packages/$numericVersion"
}
$outputPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
[IO.Directory]::CreateDirectory($outputPath) | Out-Null
$archiveName = if ($Architecture -eq 'x64') { 'SAFC64.7z' } else { 'SAFC32.7z' }
$archivePath = Join-Path $outputPath $archiveName
if (Test-Path -LiteralPath $archivePath) {
    throw "Output already exists: $archivePath. Choose another output directory to preserve the existing package."
}

# Only this uniquely created directory is cleaned up. Neither the input nor an
# existing release package is ever modified by this script.
$stagingPath = Join-Path $outputPath ('.safc-package-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($stagingPath) | Out-Null
try {
    $stagedExe = Join-Path $stagingPath 'SAFC.exe'
    Copy-Item -LiteralPath $sourcePath -Destination $stagedExe
    $stagedArchive = Join-Path $stagingPath $archiveName
    Push-Location -LiteralPath $stagingPath
    try {
        & $zipPath a -t7z -mx=9 -bd -bso0 -bsp0 $stagedArchive 'SAFC.exe'
        if ($LASTEXITCODE -ne 0) { throw "7-Zip packaging failed with exit code $LASTEXITCODE." }
        & $zipPath t -bd -bso0 -bsp0 $stagedArchive
        if ($LASTEXITCODE -ne 0) { throw "7-Zip integrity test failed with exit code $LASTEXITCODE." }
        $listing = @(& $zipPath l -slt -ba $stagedArchive)
        if ($LASTEXITCODE -ne 0) { throw '7-Zip could not list the generated archive.' }
        $entries = @($listing | Where-Object { $_ -match '^Path = ' })
        if ($entries.Count -ne 1 -or $entries[0] -ne 'Path = SAFC.exe') {
            throw 'The update archive must contain exactly one root entry: SAFC.exe.'
        }
        $verificationPath = Join-Path $stagingPath 'verified'
        & $zipPath x -bd -bso0 -bsp0 -y "-o$verificationPath" $stagedArchive
        if ($LASTEXITCODE -ne 0) { throw '7-Zip could not extract the generated archive.' }
        $sourceHash = (Get-FileHash -LiteralPath $stagedExe -Algorithm SHA256).Hash
        $extractedHash = (Get-FileHash -LiteralPath (Join-Path $verificationPath 'SAFC.exe') -Algorithm SHA256).Hash
        if ($sourceHash -ne $extractedHash) { throw 'The extracted executable does not match the packaged input.' }
    }
    finally { Pop-Location }
    # File.Move fails if another invocation has created the destination meanwhile.
    [IO.File]::Move($stagedArchive, $archivePath)
    [PSCustomObject]@{
        Archive = $archivePath
        Tag = "v$numericVersion"
        Architecture = $Architecture
        ProductVersion = $productVersion
        SHA256 = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
    }
}
finally {
    $resolvedStage = [IO.Path]::GetFullPath($stagingPath)
    $resolvedOutput = [IO.Path]::GetFullPath($outputPath).TrimEnd([char[]]'\/') + [IO.Path]::DirectorySeparatorChar
    if (-not $resolvedStage.StartsWith($resolvedOutput, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolvedStage) -notlike '.safc-package-*') {
        throw 'Refusing to clean up a staging path outside the output directory.'
    }
    Remove-Item -LiteralPath $resolvedStage -Recurse -Force
}
