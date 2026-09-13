# 《燧火纪：部落黎明》Docker 封装
# 构建阶段在干净的 Debian 环境里编译并运行全部自动测试，
# 运行阶段只保留游戏程序，任何装有 Docker 的电脑都能直接运行。

# ---------- 构建 ----------
FROM debian:bookworm-slim AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends cmake g++ ninja-build \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

RUN cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build out \
    && ctest --test-dir out --output-on-failure

# ---------- 运行 ----------
FROM debian:bookworm-slim

# 中文终端显示所需的 UTF-8 环境与 256 色支持
ENV LANG=C.UTF-8 \
    TERM=xterm-256color

RUN apt-get update \
    && apt-get install -y --no-install-recommends libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

# 游戏以当前工作目录为项目目录，存档落在 /game/saves/game，
# 挂载宿主机目录到 /game/saves 即可在容器外保留存档。
WORKDIR /game

COPY --from=builder /build/out/tribe-dawn /usr/local/bin/tribe-dawn

VOLUME ["/game/saves"]

ENTRYPOINT ["tribe-dawn"]
