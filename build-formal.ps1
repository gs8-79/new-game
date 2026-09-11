# Keep UTF-8 BOM: Windows PowerShell 5.1 otherwise misreads Chinese strings.
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [switch]$Clean,
    [switch]$EnableClangTidy,
    [switch]$UseVcpkg
)

$ErrorActionPreference = 'Stop'

$vswherePath = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswherePath)) {
    throw '找不到 vswhere.exe，请确认已安装 Visual Studio 2026。'
}

$vsInstallPath = & $vswherePath -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $vsInstallPath) {
    # 安装器收尾期间可能暂时把实例标成“不完整”。只有下方所需工具
    # 全部确实存在时才接受该实例，避免把安装器状态误判为组件缺失。
    $vsInstallPath = & $vswherePath -all -products * -property installationPath | Select-Object -First 1
}
if (-not $vsInstallPath) {
    throw '找不到 Visual Studio 2026 安装。'
}

$cmakePath = Join-Path $vsInstallPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctestPath = Join-Path $vsInstallPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
$ninjaPath = Join-Path $vsInstallPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$vsDevCmdPath = Join-Path $vsInstallPath 'Common7\Tools\VsDevCmd.bat'
foreach ($requiredPath in @($cmakePath, $ctestPath, $ninjaPath, $vsDevCmdPath)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Visual Studio 2026 组件不完整，缺少：$requiredPath。如刚完成更新，请先重启电脑。"
    }
}

$devShellCommand = "`"$vsDevCmdPath`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
$environmentLines = & $env:COMSPEC /d /s /c $devShellCommand
if ($LASTEXITCODE -ne 0) {
    throw '无法加载 Visual Studio 2026 C++ 开发环境。'
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

$buildFolder = "Formal-$Configuration"
if ($UseVcpkg) {
    $buildFolder += '-vcpkg'
}
$buildPath = Join-Path $PSScriptRoot "out\$buildFolder"
$cmakeArguments = @(
    '-S', $PSScriptRoot,
    '-B', $buildPath,
    '-G', 'Ninja',
    "-DCMAKE_BUILD_TYPE=$Configuration",
    "-DCMAKE_MAKE_PROGRAM=$ninjaPath"
)

if ($UseVcpkg) {
    $vcpkgCandidates = @('D:\tools\vcpkg')
    if ($env:VCPKG_ROOT) {
        $vcpkgCandidates += $env:VCPKG_ROOT
    }
    $vcpkgRoot = $vcpkgCandidates | Where-Object {
        Test-Path -LiteralPath (Join-Path $_ 'scripts\buildsystems\vcpkg.cmake')
    } | Select-Object -First 1
    if (-not $vcpkgRoot) {
        throw '找不到 vcpkg。请设置 VCPKG_ROOT，或安装到 D:\tools\vcpkg。'
    }
    $vcpkgDownloads = Join-Path $PSScriptRoot 'out\vcpkg-downloads'
    $env:VCPKG_DOWNLOADS = $vcpkgDownloads
    $cmakeArguments += "-DCMAKE_TOOLCHAIN_FILE=$(Join-Path $vcpkgRoot 'scripts\buildsystems\vcpkg.cmake')"
    $cmakeArguments += "-DVCPKG_DOWNLOADS=$vcpkgDownloads"
}

& $cmakePath @cmakeArguments
if ($LASTEXITCODE -ne 0) { throw '正式版 CMake 配置失败。' }

if ($Clean) { & $cmakePath --build $buildPath --clean-first }
else { & $cmakePath --build $buildPath }
if ($LASTEXITCODE -ne 0) { throw '正式版 C++ 编译失败。' }

& $ctestPath --test-dir $buildPath --output-on-failure
if ($LASTEXITCODE -ne 0) { throw '正式版自动测试失败。' }

if ($EnableClangTidy) {
    & $cmakePath --build $buildPath --target tidy
    if ($LASTEXITCODE -ne 0) { throw 'clang-tidy 静态检查未完成。' }
}

Write-Host "《燧火纪》正式版构建和测试通过：$Configuration" -ForegroundColor Green
