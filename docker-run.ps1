# Keep UTF-8 BOM: Windows PowerShell 5.1 otherwise misreads Chinese strings.
param(
    [string]$Tag = 'tribe-dawn:latest'
)

$ErrorActionPreference = 'Stop'

if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    throw '找不到 docker 命令，请先安装 Docker Desktop 并启动。'
}

docker image inspect $Tag 2>$null
if ($LASTEXITCODE -ne 0) {
    Write-Host "未找到镜像 $Tag，先执行构建。" -ForegroundColor Yellow
    & (Join-Path $PSScriptRoot 'docker-build.ps1') -Tag $Tag
}

Push-Location $PSScriptRoot
try {
    docker run --rm -it `
        -e TERM=xterm-256color -e LANG=C.UTF-8 `
        -v "${PSScriptRoot}\saves:/game/saves" `
        $Tag
    if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne 130) {
        throw "Docker 容器退出，代码：$LASTEXITCODE"
    }
} finally {
    Pop-Location
}
