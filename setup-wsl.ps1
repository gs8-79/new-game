# Keep UTF-8 BOM: Windows PowerShell 5.1 otherwise misreads Chinese strings.
# 启用 WSL2（Docker Desktop 的必需前置）。需要管理员权限，双击会弹 UAC，请点“是”。
$ErrorActionPreference = 'Continue'

# 仅接受 Windows 的成功或“需要重启”退出码；其余结果必须停止，不能误报安装完成。
function Assert-SetupSucceeded {
    param(
        [int]$ExitCode,
        [string]$Step
    )

    if ($ExitCode -eq 0) { return $false }
    if ($ExitCode -eq 3010) { return $true }
    throw "$Step 失败，退出代码：$ExitCode"
}

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "正在请求管理员权限（请在 UAC 弹窗中点“是”）..." -ForegroundColor Yellow
    Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"{0}"' -f $PSCommandPath))
    exit
}

Write-Host "启用 WSL2 与虚拟机平台..." -ForegroundColor Cyan
& wsl.exe --install --no-distribution
$wslInstallExitCode = $LASTEXITCODE
$restartRequired = $false
if ($wslInstallExitCode -ne 0 -and $wslInstallExitCode -ne 3010) {
    Write-Host "wsl --install 失败，改用 dism 手动启用..." -ForegroundColor Yellow
    dism.exe /online /enable-feature /featurename:Microsoft-Windows-Subsystem-Linux /all /norestart | Out-Null
    $restartRequired = Assert-SetupSucceeded $LASTEXITCODE '启用 Microsoft-Windows-Subsystem-Linux'
    dism.exe /online /enable-feature /featurename:VirtualMachinePlatform /all /norestart | Out-Null
    if (Assert-SetupSucceeded $LASTEXITCODE '启用 VirtualMachinePlatform') { $restartRequired = $true }
    if (-not $restartRequired) {
        & wsl.exe --update
        [void](Assert-SetupSucceeded $LASTEXITCODE 'wsl --update')
    }
} else {
    $restartRequired = Assert-SetupSucceeded $wslInstallExitCode 'wsl --install --no-distribution'
    if ($restartRequired) { Write-Host "WSL2 组件已启用，需要重启后继续。" -ForegroundColor Yellow }
}

Write-Host ""
Write-Host "==============================================================" -ForegroundColor Green
Write-Host " 已成功配置所需 Windows 组件。请【重启电脑】。"
Write-Host " 重启后：启动 Docker Desktop（等托盘图标变为 Running），"
Write-Host " 然后双击 开始Docker版.cmd 即可进入游戏。"
Write-Host "==============================================================" -ForegroundColor Green
Read-Host "按回车退出"
