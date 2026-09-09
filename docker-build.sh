#!/usr/bin/env bash
# 《燧火纪：部落黎明》Docker 镜像构建（macOS / Linux）
set -euo pipefail

TAG="${1:-tribe-dawn:latest}"

if ! command -v docker >/dev/null 2>&1; then
    echo "找不到 docker 命令，请先安装 Docker（macOS 用 Docker Desktop，Linux 用 Docker Engine）。" >&2
    exit 1
fi

cd "$(dirname "$0")"
docker build -t "$TAG" .
echo "《燧火纪》Docker 镜像构建完成：$TAG"
