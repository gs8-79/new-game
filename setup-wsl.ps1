# Keep UTF-8 BOM: Windows PowerShell 5.1 otherwise misreads Chinese strings.
# 启用 WSL2（Docker Desktop 的必需前置）。需要管理员权限，双击会弹 UAC，请点“是”。
$ErrorActionPreference = 'Continue'

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "正在请求管理员权限（请在 UAC 弹窗中点“是”）..." -ForegroundColor Yellow
    Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"{0}"' -f $PSCommandPath))
    exit
}

Write-Host "启用 WSL2 与虚拟机平台..." -ForegroundColor Cyan
& wsl.exe --install --no-distribution
if ($LASTEXITCODE -ne 0) {
    Write-Host "wsl --install 失败，改用 dism 手动启用..." -ForegroundColor Yellow
    dism.exe /online /enable-feature /featurename:Microsoft-Windows-Subsystem-Linux /all /norestart | Out-Null
    dism.exe /online /enable-feature /featurename:VirtualMachinePlatform /all /norestart | Out-Null
    & wsl.exe --update
}

Write-Host ""
Write-Host "==============================================================" -ForegroundColor Green
Write-Host " 完成。请【重启电脑】。"
Write-Host " 重启后：启动 Docker Desktop（等托盘图标变为 Running），"
Write-Host " 然后双击 开始Docker版.cmd 即可进入游戏。"
Write-Host "==============================================================" -ForegroundColor Green
Read-Host "按回车退出"
