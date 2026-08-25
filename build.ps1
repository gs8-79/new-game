param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'

$vswherePath = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswherePath)) {
    throw '找不到 vswhere.exe，请确认已安装 Visual Studio。'
}

$vsInstallPath = & $vswherePath -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $vsInstallPath) {
    throw '找不到包含 MSBuild 的 Visual Studio 安装。'
}

$cmakePath = Join-Path $vsInstallPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctestPath = Join-Path $vsInstallPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
$ninjaPath = Join-Path $vsInstallPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$vsDevCmdPath = Join-Path $vsInstallPath 'Common7\Tools\VsDevCmd.bat'
if (
    -not (Test-Path -LiteralPath $cmakePath) -or
    -not (Test-Path -LiteralPath $ctestPath) -or
    -not (Test-Path -LiteralPath $ninjaPath) -or
    -not (Test-Path -LiteralPath $vsDevCmdPath)
) {
    throw '找不到 Visual Studio 内置的开发环境、CMake、CTest 或 Ninja。'
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

$buildPath = Join-Path $PSScriptRoot "out\$Configuration"

& $cmakePath -S $PSScriptRoot -B $buildPath -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" "-DCMAKE_MAKE_PROGRAM=$ninjaPath"
if ($LASTEXITCODE -ne 0) {
    throw 'CMake 配置失败。'
}

& $cmakePath --build $buildPath
if ($LASTEXITCODE -ne 0) {
    throw 'C++ 编译失败。'
}

& $ctestPath --test-dir $buildPath --output-on-failure
if ($LASTEXITCODE -ne 0) {
    throw '自动测试失败。'
}

Write-Host "构建和测试通过：$Configuration" -ForegroundColor Green
