#!/usr/bin/env bash
# 《燧火纪：部落黎明》Docker 启动脚本（macOS / Linux）
set -euo pipefail

TAG="${1:-tribe-dawn:latest}"

if ! docker image inspect "$TAG" >/dev/null 2>&1; then
    echo "未找到镜像 $TAG，先执行构建。"
    bash "$(dirname "$0")/docker-build.sh" "$TAG"
fi

cd "$(dirname "$0")"
docker run --rm -it \
    -e TERM=xterm-256color -e LANG=C.UTF-8 \
    -v "$(pwd)/saves:/game/saves" \
    "$TAG"
