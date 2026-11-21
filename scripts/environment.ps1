$ErrorActionPreference = 'Stop'
$script:CubeWork = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$env:TEMP = Join-Path $CubeWork 'temp'
$env:TMP = $env:TEMP
$env:PIP_CACHE_DIR = Join-Path $CubeWork 'cache\pip'
$env:PYTHONPYCACHEPREFIX = Join-Path $CubeWork 'cache\pycache'
$env:PYTHONNOUSERSITE = '1'
$env:PYINSTALLER_CONFIG_DIR = Join-Path $CubeWork 'cache\pyinstaller'
$env:UV_CACHE_DIR = Join-Path $CubeWork 'cache\uv'
$env:PATH = (Join-Path $CubeWork 'tools\bin') + ';' + (Join-Path $CubeWork 'tools\Ninja') + ';' + $env:PATH
foreach ($CubeCache in @($env:TEMP,$env:PIP_CACHE_DIR,$env:PYTHONPYCACHEPREFIX,$env:PYINSTALLER_CONFIG_DIR)) {
    New-Item -ItemType Directory -Force -Path $CubeCache | Out-Null
}
