param([switch]$InstallDependencies)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'environment.ps1')
$McpRoot = Join-Path $script:CubeWork 'mcp'
$McpPython = Join-Path $McpRoot '.venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $McpPython)) {
    & (Join-Path $script:CubeWork 'tools\python-package\tools\python.exe') -m venv (Join-Path $McpRoot '.venv')
    if ($LASTEXITCODE -ne 0) { throw 'MCP virtual environment creation failed.' }
    $InstallDependencies = $true
}
if ($InstallDependencies) {
    & $McpPython -m pip install --no-deps -r (Join-Path $McpRoot 'requirements.lock')
    if ($LASTEXITCODE -ne 0) { throw 'MCP locked dependency installation failed.' }
}
& $McpPython -m pip check
if ($LASTEXITCODE -ne 0) { throw 'MCP dependency consistency check failed.' }
& $McpPython -m PyInstaller --noconfirm --clean --onedir --console --name CubeExplorerMcp `
    --distpath (Join-Path $McpRoot 'dist') --workpath (Join-Path $McpRoot 'build') `
    --specpath $McpRoot --paths $McpRoot --collect-submodules mcp.server --collect-submodules mcp.shared --collect-submodules mcp_types `
    --recursive-copy-metadata mcp (Join-Path $McpRoot 'server.py')
if ($LASTEXITCODE -ne 0) { throw 'MCP EXE packaging failed.' }
$McpDelivery = Join-Path $McpRoot 'dist\CubeExplorerMcp'
$McpLicenses = Join-Path $McpDelivery 'licenses'
& $McpPython (Join-Path $McpRoot 'collect_licenses.py') --output $McpLicenses
if ($LASTEXITCODE -ne 0) { throw 'MCP dependency license collection failed.' }
Write-Output (Join-Path $McpRoot 'dist\CubeExplorerMcp\CubeExplorerMcp.exe')
