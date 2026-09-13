# Run clang-tidy through clang++ rather than CMake's MSVC co-compile wrapper.
# This avoids the wrapper's Windows exception and UTF-8 argument incompatibilities.
param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot,
    [Parameter(Mandatory = $true)]
    [string]$ClangTidy
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $ClangTidy)) {
    throw "找不到 LLVM 工具：$ClangTidy"
}

$vswherePath = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswherePath)) {
    throw '找不到 vswhere.exe，无法加载 Visual Studio 的 C++ 标准库环境。'
}

$vsInstallPath = & $vswherePath -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $vsInstallPath) {
    throw '找不到包含 MSBuild 的 Visual Studio 安装。'
}

$vsDevCmdPath = Join-Path $vsInstallPath 'Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $vsDevCmdPath)) {
    throw "找不到 Visual Studio 开发环境脚本：$vsDevCmdPath"
}

$devShellCommand = "`"$vsDevCmdPath`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
$environmentLines = & $env:COMSPEC /d /s /c $devShellCommand
if ($LASTEXITCODE -ne 0) {
    throw '无法加载 Visual Studio C++ 开发环境。'
}

$importedNames = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
foreach ($line in $environmentLines) {
    $separator = $line.IndexOf('=')
    if ($separator -gt 0) {
        $name = $line.Substring(0, $separator)
        $value = $line.Substring($separator + 1)
        if ($importedNames.Add($name)) {
            Set-Item -LiteralPath "Env:$name" -Value $value
        }
    }
}

Set-Location -LiteralPath $SourceRoot
$sourceFiles = @(
    'formal/src/application.cpp',
    'formal/src/console_ui.cpp',
    'formal/src/ending_presentation.cpp',
    'formal/src/expansion_game.cpp',
    'formal/src/expansion_types.cpp',
    'formal/src/game_engine.cpp',
    'formal/src/main.cpp',
    'formal/src/save_repository.cpp',
    'formal/tests/current_game_tests.cpp',
    'tests/test_main.cpp'
)

$failedFiles = @()
foreach ($sourceFile in $sourceFiles) {
    Write-Host "[clang-tidy] $sourceFile"
    & $ClangTidy --quiet --config-file=.clang-tidy $sourceFile -- clang++ -x c++ -std=c++17 -fexceptions -finput-charset=UTF-8 -Iformal/include -Itests
    if ($LASTEXITCODE -ne 0) {
        $failedFiles += $sourceFile
    }
}

if ($failedFiles) {
    throw "clang-tidy 无法完整分析：$($failedFiles -join ', ')"
}
