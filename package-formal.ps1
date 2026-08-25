param(
    [string]$Destination = [Environment]::GetFolderPath('Desktop'),
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build-formal.ps1') -Configuration Release
}

$repoRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
$stageRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'out\package-formal'))
if (-not $stageRoot.StartsWith($repoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw '打包临时目录不在项目内部，已停止。'
}
if (Test-Path -LiteralPath $stageRoot) {
    Remove-Item -LiteralPath $stageRoot -Recurse -Force
}

$windowsName = '燧火纪-部落黎明-正式版-Windows-x64'
$sourceName = '燧火纪-部落黎明-正式版-源码-Windows-Mac'
$windowsRoot = Join-Path $stageRoot $windowsName
$sourceRoot = Join-Path $stageRoot $sourceName
New-Item -ItemType Directory -Path $windowsRoot, $sourceRoot -Force | Out-Null

$formalExecutable = Join-Path $PSScriptRoot 'out\Formal-Release\tribe-dawn.exe'
if ($SkipBuild -and -not (Test-Path -LiteralPath $formalExecutable)) {
    $formalExecutable = Join-Path $PSScriptRoot 'out\Release\tribe-dawn.exe'
}
if (-not (Test-Path -LiteralPath $formalExecutable)) {
    throw '找不到已通过测试的 Release 正式版程序。请先运行 build-formal.ps1 或 build.ps1。'
}
Copy-Item -LiteralPath $formalExecutable -Destination $windowsRoot
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'formal\package\开始游戏.cmd') -Destination $windowsRoot
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'formal\package\试玩说明.txt') -Destination $windowsRoot

Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'CMakeLists.txt') -Destination $sourceRoot
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '.gitignore') -Destination $sourceRoot
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'formal') -Destination $sourceRoot -Recurse
New-Item -ItemType Directory -Path (Join-Path $sourceRoot 'tests') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'tests\test_main.cpp') -Destination (Join-Path $sourceRoot 'tests')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'tests\test_harness.hpp') -Destination (Join-Path $sourceRoot 'tests')
foreach ($script in @('build-formal.ps1', 'run-formal.ps1', 'build-formal-macos.sh', 'run-formal-macos.sh', '开始正式版.cmd')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $script) -Destination $sourceRoot
}

New-Item -ItemType Directory -Path $Destination -Force | Out-Null
$windowsZip = Join-Path $Destination "$windowsName.zip"
$sourceZip = Join-Path $Destination "$sourceName.zip"
foreach ($zip in @($windowsZip, $sourceZip)) {
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
}
Compress-Archive -LiteralPath $windowsRoot -DestinationPath $windowsZip -CompressionLevel Optimal
Compress-Archive -LiteralPath $sourceRoot -DestinationPath $sourceZip -CompressionLevel Optimal

Get-Item -LiteralPath $windowsZip, $sourceZip | Select-Object FullName, Length, LastWriteTime
