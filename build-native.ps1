# Keep UTF-8 BOM: Windows PowerShell 5.1 otherwise misreads Chinese strings.
# 用免安装的 w64devkit (GCC) 直接编译 Windows 本机版，无需 Visual Studio / CMake / Docker。
# 找不到工具链时会自动下载（约 59 MB，来自 GitHub）。
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

# 用 Continue：g++ 的编译警告会写到 stderr，Stop 会误判为失败；成败只看 $LASTEXITCODE。
$ErrorActionPreference = 'Continue'

$w64devkitVersion = '2.9.1'
$candidates = @(
    'D:\tool\w64devkit\w64devkit\bin',
    'D:\tool\w64devkit\bin',
    (Join-Path $env:LOCALAPPDATA 'w64devkit\w64devkit\bin'),
    (Join-Path $env:LOCALAPPDATA 'w64devkit\bin'),
    (Join-Path $PSScriptRoot 'toolchain\w64devkit\bin')
)

function Find-Toolchain {
    foreach ($cand in $candidates) {
        if (Test-Path -LiteralPath (Join-Path $cand 'g++.exe')) { return $cand }
    }
    return $null
}

$toolchainBin = Find-Toolchain
if (-not $toolchainBin) {
    Write-Host "未找到编译器，自动下载 w64devkit 工具链（约 59 MB）..." -ForegroundColor Yellow
    $destRoot = Join-Path $env:LOCALAPPDATA 'w64devkit'
    $sfx = Join-Path $env:TEMP "w64devkit-x64-$w64devkitVersion.7z.exe"
    $url = "https://github.com/skeeto/w64devkit/releases/download/v$w64devkitVersion/w64devkit-x64-$w64devkitVersion.7z.exe"
    curl.exe -L --ssl-no-revoke --retry 5 --retry-delay 2 -o $sfx $url
    if ($LASTEXITCODE -ne 0) { throw '工具链下载失败，请检查网络后重试。' }
    New-Item -ItemType Directory -Force -Path $destRoot | Out-Null
    & $sfx "-o$destRoot" -y | Out-Null
    Remove-Item $sfx -Force -ErrorAction SilentlyContinue
    $toolchainBin = Find-Toolchain
    if (-not $toolchainBin) { throw '工具链解压后仍未找到 g++.exe。' }
}

# 工具链 bin 必须排在 PATH 最前，g++ 才能找到 as/ld 等配套程序
$env:Path = "$toolchainBin;$env:Path"

$srcs = @(
    'formal\src\expansion_types.cpp',
    'formal\src\expansion_game.cpp',
    'formal\src\game_engine.cpp',
    'formal\src\save_repository.cpp',
    'formal\src\ending_presentation.cpp',
    'formal\src\console_ui.cpp',
    'formal\src\application.cpp',
    'formal\src\main.cpp'
)

$outDir = "out\Native-$Configuration"
$exe = Join-Path $outDir 'tribe-dawn.exe'

$flags = @('-std=c++17', '-Wall', '-Wextra', '-I', 'formal\include', '-static', '-static-libgcc', '-static-libstdc++')
if ($Configuration -eq 'Release') { $flags += @('-O2', '-DNDEBUG') }
else { $flags += @('-O0', '-g') }

Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    & (Join-Path $toolchainBin 'g++.exe') @flags -o $exe @srcs
    if ($LASTEXITCODE -ne 0) { throw "本机版编译失败（$Configuration）。" }
} finally {
    Pop-Location
}

Write-Host "《燧火纪》本机版编译成功：$(Join-Path $PSScriptRoot $exe)" -ForegroundColor Green
