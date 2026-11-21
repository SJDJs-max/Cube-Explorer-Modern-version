param([switch]$Test, [switch]$BuildMcp, [switch]$InstallMcpDependencies)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'environment.ps1')
$env:PATH = (Join-Path $CubeWork 'tools\Qt\bin') + ';' + $env:PATH
$CubeCMake = Join-Path $CubeWork 'tools\bin\cmake.exe'
$CubeBuild = Join-Path $CubeWork 'build\release'
if (-not (Test-Path -LiteralPath $CubeCMake)) { throw 'Run scripts/bootstrap.ps1 first.' }
& $CubeCMake -S $CubeWork -B $CubeBuild -G Ninja '-DCMAKE_BUILD_TYPE=Release' '-DCUBE_BUILD_ENGINE=ON' '-DCUBE_BUILD_GUI=ON' "-DCMAKE_PREFIX_PATH=$CubeWork\tools\Qt"
if ($LASTEXITCODE) { throw 'CMake configuration failed.' }
& $CubeCMake --build $CubeBuild --parallel
if ($LASTEXITCODE) { throw 'C++ build failed.' }
if ($BuildMcp -or $InstallMcpDependencies) { & (Join-Path $PSScriptRoot 'build-mcp.ps1') -InstallDependencies:$InstallMcpDependencies }
if ($Test) {
    & (Join-Path $CubeWork 'tools\bin\ctest.exe') --test-dir $CubeBuild --output-on-failure
    if ($LASTEXITCODE) { throw 'Core tests failed.' }
    & (Join-Path $CubeWork 'tools\python-package\tools\python.exe') (Join-Path $CubeWork 'tests\engine_integration.py')
    if ($LASTEXITCODE) { throw 'Engine integration failed.' }
    $CubeOldPlatform = $env:QT_QPA_PLATFORM
    try {
        $env:QT_QPA_PLATFORM = 'offscreen'
        $CubeProcess = Start-Process -FilePath (Join-Path $CubeBuild 'src\app\CubeExplorerModern.exe') -ArgumentList @('--smoke-test',"--smoke-log=`"$CubeWork\logs\ui-release-smoke.txt`"") -WorkingDirectory $CubeWork -WindowStyle Hidden -PassThru
        if (-not $CubeProcess.WaitForExit(60000)) { $CubeProcess.Kill(); throw 'UI smoke timed out.' }
        if ($CubeProcess.ExitCode) { throw 'UI smoke failed; see logs/ui-release-smoke.txt.' }
        Get-Content -LiteralPath (Join-Path $CubeWork 'logs\ui-release-smoke.txt')
    } finally { $env:QT_QPA_PLATFORM = $CubeOldPlatform }
    $CubeMcpPython = Join-Path $CubeWork 'mcp\.venv\Scripts\python.exe'
    if (Test-Path -LiteralPath $CubeMcpPython) {
        foreach ($CubeTestName in @('mcp_bridge_tests.py','mcp_integration_tests.py')) {
            & $CubeMcpPython (Join-Path $CubeWork "tests\$CubeTestName")
            if ($LASTEXITCODE) { throw "MCP test failed: $CubeTestName" }
        }
    }
}
$CubeOriginal = Get-Content -LiteralPath (Join-Path $CubeWork 'docs\raw-sha256.json') -Raw | ConvertFrom-Json
if (Test-Path -LiteralPath $CubeOriginal[0].Path) { & (Join-Path $PSScriptRoot 'verify-raw.ps1') }
else { Write-Output 'Original reference is not present here; the migrated source builds independently.' }
Write-Output "Built GUI and engine in $CubeBuild."
