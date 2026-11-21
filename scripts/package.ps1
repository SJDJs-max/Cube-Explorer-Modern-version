param([switch]$Rebuild)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'environment.ps1')
$env:PATH = (Join-Path $CubeWork 'tools\Qt\bin') + ';' + $env:PATH
if ($Rebuild) { & (Join-Path $PSScriptRoot 'build.ps1') -Test -BuildMcp }
$CubeRelease = Join-Path $CubeWork "dist\CubeExplorerModern"
New-Item -ItemType Directory -Force -Path $CubeRelease | Out-Null
foreach ($CubeProgram in @('app\CubeExplorerModern.exe','engine\CubeExplorerEngine.exe')) {
    Copy-Item -LiteralPath (Join-Path $CubeWork "build\release\src\$CubeProgram") -Destination $CubeRelease -Force
}
$CubeMcpRelease = Join-Path $CubeRelease 'mcp'
New-Item -ItemType Directory -Force -Path $CubeMcpRelease | Out-Null
Get-ChildItem -LiteralPath (Join-Path $CubeWork 'mcp\dist\CubeExplorerMcp') -Force | Copy-Item -Destination $CubeMcpRelease -Recurse -Force
& (Join-Path $CubeWork 'tools\Qt\bin\windeployqt.exe') --release --no-translations --no-system-d3d-compiler --no-opengl-sw --compiler-runtime --dir $CubeRelease (Join-Path $CubeRelease 'CubeExplorerModern.exe') (Join-Path $CubeRelease 'CubeExplorerEngine.exe')
if ($LASTEXITCODE) { throw 'Qt deployment failed.' }
# Keep the optional offline smoke/screenshot mode available in the portable build.
Copy-Item -LiteralPath (Join-Path $CubeWork 'tools\Qt\plugins\platforms\qoffscreen.dll') -Destination (Join-Path $CubeRelease 'platforms') -Force
& (Join-Path $CubeWork 'tools\python-package\tools\python.exe') (Join-Path $PSScriptRoot 'package_release.py')
if ($LASTEXITCODE) { throw 'Release assembly failed.' }
$CubeOriginal = Get-Content -LiteralPath (Join-Path $CubeWork 'docs\raw-sha256.json') -Raw | ConvertFrom-Json
if (Test-Path -LiteralPath $CubeOriginal[0].Path) { & (Join-Path $PSScriptRoot 'verify-raw.ps1') }
Write-Output "Portable release: $CubeRelease"
