param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build-formal.ps1') -Configuration $Configuration
}

$executable = Join-Path $PSScriptRoot "out\Formal-$Configuration\tribe-dawn.exe"
if (-not (Test-Path -LiteralPath $executable)) {
    throw "找不到正式版程序：$executable。请先运行 build-formal.ps1。"
}

Push-Location $PSScriptRoot
try {
    & $executable
    if ($LASTEXITCODE -ne 0) { throw "正式版程序异常退出，代码：$LASTEXITCODE" }
} finally {
    Pop-Location
}
