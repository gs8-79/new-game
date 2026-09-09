# Keep UTF-8 BOM: Windows PowerShell 5.1 otherwise misreads Chinese strings.
param(
    [string]$Tag = 'tribe-dawn:latest'
)

$ErrorActionPreference = 'Stop'

if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    throw '找不到 docker 命令，请先安装 Docker Desktop 并启动。'
}

docker build -t $Tag $PSScriptRoot
if ($LASTEXITCODE -ne 0) { throw 'Docker 镜像构建失败。' }

Write-Host "《燧火纪》Docker 镜像构建完成：$Tag" -ForegroundColor Green
