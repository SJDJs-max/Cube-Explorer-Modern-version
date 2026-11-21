param(
    [switch]$CoreOnly,
    [switch]$Offline,
    [switch]$VerifyOnly,
    [switch]$SkipSources,
    [string]$OriginalTableDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'environment.ps1')
$CubeBootstrapRoot = [IO.Path]::GetFullPath($script:CubeWork).TrimEnd('\','/')
$CubeBootstrapPrefix = $CubeBootstrapRoot + [IO.Path]::DirectorySeparatorChar

function Get-CubeLocalPath([string]$Relative) {
    if ([IO.Path]::IsPathRooted($Relative)) { throw "Expected a workspace-relative path: $Relative" }
    $Resolved = [IO.Path]::GetFullPath((Join-Path $CubeBootstrapRoot $Relative))
    if (-not $Resolved.StartsWith($CubeBootstrapPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing an output path outside the workspace: $Resolved"
    }
    return $Resolved
}

function Test-CubeHash([string]$Path, [string]$Expected) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Required file is missing: $Path" }
    if ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ne $Expected) {
        throw "SHA256 mismatch. Refusing to use or overwrite this file: $Path"
    }
}

function Get-CubeArchive([string]$File, [string]$Url, [string]$SHA256) {
    if ([IO.Path]::GetFileName($File) -ne $File) { throw "Archive filename must be a plain filename: $File" }
    $Archive = Get-CubeLocalPath (Join-Path 'downloads' $File)
    if (Test-Path -LiteralPath $Archive -PathType Leaf) {
        Test-CubeHash $Archive $SHA256
        return $Archive
    }
    if ($Offline -or $VerifyOnly) { throw "Missing cached archive in offline/verify mode: $Archive" }
    if (-not $Url.StartsWith('https://', [StringComparison]::OrdinalIgnoreCase)) { throw 'Pinned downloads must use HTTPS.' }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Archive) | Out-Null
    $Partial = $Archive + '.partial'
    Write-Host "Downloading $File"
    $Curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if ($Curl) {
        & $Curl.Source --fail --location --retry 3 --output $Partial $Url
        if ($LASTEXITCODE -ne 0) { throw "Download failed: $Url" }
    } else {
        Invoke-WebRequest -Uri $Url -OutFile $Partial -UseBasicParsing
    }
    Test-CubeHash $Partial $SHA256
    Move-Item -LiteralPath $Partial -Destination $Archive
    return $Archive
}

function Expand-CubeArchive([string]$Archive, [string]$RelativeTarget) {
    $Target = Get-CubeLocalPath $RelativeTarget
    New-Item -ItemType Directory -Force -Path $Target | Out-Null
    $Tar = Get-Command tar.exe -ErrorAction SilentlyContinue
    if ($Tar) {
        & $Tar.Source -xf $Archive -C $Target
        if ($LASTEXITCODE -eq 0) { return }
        Write-Host 'This tar build could not extract the archive; checking for an existing 7-Zip.'
    }
    $SevenZip = Get-Command 7z.exe -ErrorAction SilentlyContinue
    $SevenZipPath = if ($SevenZip) { $SevenZip.Source } else { $null }
    if (-not $SevenZipPath) {
        $Candidates = @((Get-CubeLocalPath 'tools\7zip\7z.exe'))
        if ($env:ProgramFiles) { $Candidates += Join-Path $env:ProgramFiles '7-Zip\7z.exe' }
        foreach ($Candidate in $Candidates) {
            if (Test-Path -LiteralPath $Candidate -PathType Leaf) { $SevenZipPath = $Candidate; break }
        }
    }
    if (-not $SevenZipPath) {
        throw 'No compatible archive reader was found. Use Windows tar/libarchive with 7z support or place 7z.exe and its required files in tools\7zip. Bootstrap never installs a global archiver.'
    }
    if ($Archive.EndsWith('.tar.xz', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The Qt source tar.xz archive requires Windows tar/libarchive. Use -SkipSources to prepare binaries only, then extract the pinned source archive with a compatible tar.'
    }
    & $SevenZipPath x -y $Archive "-o$Target"
    if ($LASTEXITCODE -ne 0) { throw "Archive extraction failed: $Archive" }
}

$CubeToolchain = @(Get-Content -LiteralPath (Get-CubeLocalPath 'downloads\toolchain-lock.json') -Raw | ConvertFrom-Json)
$CubeQt = Get-Content -LiteralPath (Get-CubeLocalPath 'downloads\qtbase-source.json') -Raw | ConvertFrom-Json
$CubeLock = Get-Content -LiteralPath (Get-CubeLocalPath 'downloads\bootstrap-lock.json') -Raw | ConvertFrom-Json
if ($CubeLock.Format -ne 1) { throw 'Unsupported bootstrap lock format.' }

foreach ($Tool in $CubeLock.Tools) {
    if ($CoreOnly -and -not $Tool.Core) { continue }
    $Source = switch ($Tool.Lock) {
        'toolchain' { $CubeMatches = @($CubeToolchain | Where-Object { $_.File -eq $Tool.Archive }); if ($CubeMatches.Count -ne 1) { throw "Ambiguous toolchain lock for $($Tool.Archive)" }; $CubeMatches[0] }
        'qtbase' { $CubeQt }
        'self' { $Tool }
        default { throw "Unknown lock source: $($Tool.Lock)" }
    }
    $Sentinel = Get-CubeLocalPath $Tool.Sentinel
    $Present = Test-Path -LiteralPath $Sentinel -PathType Leaf
    foreach ($Required in $Tool.Required) { $Present = $Present -and (Test-Path -LiteralPath (Get-CubeLocalPath $Required) -PathType Leaf) }
    $CachedArchive = Get-CubeLocalPath (Join-Path 'downloads' $Tool.Archive)
    if (Test-Path -LiteralPath $CachedArchive -PathType Leaf) { Test-CubeHash $CachedArchive $Source.SHA256 }
    if (-not $Present) {
        if ($VerifyOnly) { throw "Portable tool is missing or incomplete: $($Tool.Name)" }
        $Archive = Get-CubeArchive $Tool.Archive $Source.Url $Source.SHA256
        Expand-CubeArchive $Archive $Tool.Target
    }
    Test-CubeHash $Sentinel $Tool.SentinelSHA256
    foreach ($Required in $Tool.Required) {
        if (-not (Test-Path -LiteralPath (Get-CubeLocalPath $Required) -PathType Leaf)) { throw "Tool archive lacks required file: $Required" }
    }
    Write-Host "Verified $($Tool.Name)"
}

if (-not $CoreOnly -and -not $SkipSources) {
    $Source = $CubeLock.QtSource
    $Sentinel = Get-CubeLocalPath $Source.Sentinel
    $CachedArchive = Get-CubeLocalPath (Join-Path 'downloads' $Source.Archive)
    if (Test-Path -LiteralPath $CachedArchive -PathType Leaf) { Test-CubeHash $CachedArchive $Source.SHA256 }
    if (-not (Test-Path -LiteralPath $Sentinel -PathType Leaf)) {
        if ($VerifyOnly) { throw 'The matching Qt source tree is missing.' }
        $Archive = Get-CubeArchive $Source.Archive $Source.Url $Source.SHA256
        Expand-CubeArchive $Archive $Source.Target
    }
    Write-Host 'Verified matching Qt source availability.'
}

if (-not $OriginalTableDirectory) { $OriginalTableDirectory = Split-Path -Parent $CubeBootstrapRoot }
$OriginalTableDirectory = [IO.Path]::GetFullPath($OriginalTableDirectory)
foreach ($Table in $CubeLock.OriginalTables) {
    if ([IO.Path]::GetFileName($Table.File) -ne $Table.File) { throw 'Invalid table filename in lock.' }
    $Destination = Get-CubeLocalPath (Join-Path 'tables\original' $Table.File)
    if (-not (Test-Path -LiteralPath $Destination -PathType Leaf)) {
        if ($VerifyOnly) { throw "Original table copy is missing: $Destination" }
        $Original = Join-Path $OriginalTableDirectory $Table.File
        Test-CubeHash $Original $Table.SHA256
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
        Copy-Item -LiteralPath $Original -Destination $Destination
    }
    Test-CubeHash $Destination $Table.SHA256
    if ((Get-Item -LiteralPath $Destination).Length -ne $Table.Length) { throw "Wrong table length: $Destination" }
}
Write-Host ("Verified {0} original HTM table copies; original files were read only." -f @($CubeLock.OriginalTables).Count)
Write-Host "Portable tool preparation complete: $CubeBootstrapRoot"
