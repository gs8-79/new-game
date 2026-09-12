# Keep UTF-8 BOM: Windows PowerShell 5.1 otherwise misreads Chinese strings.
# docker-build.ps1 / docker-run.ps1 共用的辅助函数。

function Find-DockerCli {
    $onPath = Get-Command docker -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    $candidates = @(
        (Join-Path $env:LOCALAPPDATA 'Programs\DockerDesktop\resources\bin\docker.exe'),
        (Join-Path $env:ProgramFiles 'Docker\Docker\resources\bin\docker.exe')
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    return $null
}

function Test-DockerEngine {
    param([string]$Docker)
    & $Docker info --format '{{.ServerVersion}}' *> $null
    return ($LASTEXITCODE -eq 0)
}

# 返回可用的 docker CLI 路径，并确保引擎在运行（必要时自动启动 Docker Desktop 并等待）。
function Resolve-Docker {
    $docker = Find-DockerCli
    if (-not $docker) {
        throw '找不到 docker 命令。请先安装 Docker Desktop：https://www.docker.com/products/docker-desktop/'
    }
    if (Test-DockerEngine $docker) { return $docker }

    Write-Host 'Docker 引擎未运行，正在启动 Docker Desktop…' -ForegroundColor Yellow
    $desktopCandidates = @(
        (Join-Path $env:LOCALAPPDATA 'Programs\DockerDesktop\Docker Desktop.exe'),
        (Join-Path $env:ProgramFiles 'Docker\Docker\Docker Desktop.exe')
    )
    $desktop = $desktopCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if ($desktop) { Start-Process -FilePath $desktop | Out-Null }

    for ($i = 1; $i -le 40; $i++) {
        Start-Sleep -Seconds 5
        if (Test-DockerEngine $docker) {
            Write-Host 'Docker 引擎已就绪。' -ForegroundColor Green
            return $docker
        }
        Write-Host ("  等待 Docker 引擎启动…（{0} 秒）" -f ($i * 5))
    }
    throw '等待 Docker 引擎超时。请手动打开 Docker Desktop 查看提示；若提示缺少 WSL2，可先运行 安装WSL2.cmd 并重启电脑。'
}
