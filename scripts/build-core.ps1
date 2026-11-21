param([ValidateSet('Release','Debug','RelWithDebInfo')][string]$Configuration='Release', [switch]$Test)
. "$PSScriptRoot\environment.ps1"
$CubeBuild = Join-Path $CubeWork ('build\' + $Configuration.ToLowerInvariant())
& (Join-Path $CubeWork 'tools\bin\cmake.exe') -S $CubeWork -B $CubeBuild -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" '-DCUBE_BUILD_GUI=OFF' '-DCUBE_BUILD_ENGINE=OFF'
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
& (Join-Path $CubeWork 'tools\bin\cmake.exe') --build $CubeBuild --parallel
if ($LASTEXITCODE -ne 0) { throw 'Core build failed' }
if ($Test) {
    & (Join-Path $CubeWork 'tools\bin\ctest.exe') --test-dir $CubeBuild --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Core validation failed' }
}
& "$PSScriptRoot\verify-raw.ps1"
