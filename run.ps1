param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration
}

$exePath = Join-Path $PSScriptRoot "out\$Configuration\mud-demos.exe"
if (-not (Test-Path -LiteralPath $exePath)) {
    throw "找不到程序：$exePath，请先运行 build.ps1。"
}

Push-Location $PSScriptRoot
try {
    & $exePath
} finally {
    Pop-Location
}
