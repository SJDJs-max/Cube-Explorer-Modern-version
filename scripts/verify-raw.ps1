$ErrorActionPreference = 'Stop'
$CubeWork = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$CubeManifest = Get-Content -LiteralPath (Join-Path $CubeWork 'docs\raw-sha256.json') -Raw | ConvertFrom-Json
$CubeChanges = @()
foreach ($CubeEntry in $CubeManifest) {
    if (-not (Test-Path -LiteralPath $CubeEntry.Path -PathType Leaf)) {
        $CubeChanges += $CubeEntry.Path
    } elseif ((Get-FileHash -LiteralPath $CubeEntry.Path -Algorithm SHA256).Hash -ne $CubeEntry.SHA256) {
        $CubeChanges += $CubeEntry.Path
    }
}
if ($CubeChanges.Count) { throw ('Original source changed: ' + ($CubeChanges -join ', ')) }
Write-Output ("Verified {0} original files: unchanged." -f $CubeManifest.Count)
