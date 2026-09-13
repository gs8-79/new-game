# Keep UTF-8 BOM: Windows PowerShell 5.1 otherwise misreads Chinese strings.
param(
    [string]$Tag = 'tribe-dawn:latest'
)

# 用 Continue：docker 的构建进度会写到 stderr，Stop 会误判为失败。成败看 $LASTEXITCODE。
$ErrorActionPreference = 'Continue'
. (Join-Path $PSScriptRoot 'docker-common.ps1')

$docker = Resolve-Docker

& $docker build -t $Tag $PSScriptRoot
if ($LASTEXITCODE -ne 0) { throw 'Docker 镜像构建失败。' }

Write-Host "《燧火纪》Docker 镜像构建完成：$Tag" -ForegroundColor Green
