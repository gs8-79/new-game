# Keep UTF-8 BOM: Windows PowerShell 5.1 otherwise misreads Chinese strings.
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build-native.ps1') -Configuration $Configuration
}

$exe = Join-Path $PSScriptRoot "out\Native-$Configuration\tribe-dawn.exe"
if (-not (Test-Path -LiteralPath $exe)) {
    throw "找不到本机版程序：$exe"
}

Push-Location $PSScriptRoot
try {
    & $exe
    if ($LASTEXITCODE -ne 0) { throw "本机版程序异常退出，代码：$LASTEXITCODE" }
} finally {
    Pop-Location
}
