# Keep UTF-8 BOM: Windows PowerShell 5.1 otherwise misreads Chinese strings.
param(
    [string]$Tag = 'tribe-dawn:latest'
)

# 用 Continue：docker 的输出会写到 stderr，Stop 会误判为失败。成败看 $LASTEXITCODE。
$ErrorActionPreference = 'Continue'
. (Join-Path $PSScriptRoot 'docker-common.ps1')

$docker = Resolve-Docker

& $docker image inspect $Tag *> $null
if ($LASTEXITCODE -ne 0) {
    Write-Host "未找到镜像 $Tag，先执行构建。" -ForegroundColor Yellow
    & (Join-Path $PSScriptRoot 'docker-build.ps1') -Tag $Tag
    if ($LASTEXITCODE -ne 0) { throw '镜像构建失败，无法启动游戏。' }
}

Push-Location $PSScriptRoot
try {
    & $docker run --rm -it `
        -e TERM=xterm-256color -e LANG=C.UTF-8 `
        -v "${PSScriptRoot}\saves:/game/saves" `
        $Tag
    if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne 130) {
        throw "Docker 容器退出，代码：$LASTEXITCODE"
    }
} finally {
    Pop-Location
}
