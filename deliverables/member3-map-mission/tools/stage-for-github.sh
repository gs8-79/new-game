#!/usr/bin/env bash
# 成员3交付物：安全暂存脚本（把改动精确放进 Git 暂存区，避免误提交构建产物）。
#
# 作用：
#   1) 确认当前目录是 Git 仓库；
#   2) 只 git add 我负责的文件与交付目录；
#   3) 检查暂存区里有没有 out/、saves/、.DS_Store、二进制等不该提交的东西，有就直接失败；
#   4) 打印暂存清单与下一步命令。
#
# 用法：bash deliverables/member3-map-mission/tools/stage-for-github.sh
# 退出码：0 = 已安全暂存；非 0 = 环境不对或发现不该提交的文件。

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
delivery_dir="$(cd -- "$script_dir/.." && pwd)"
repo_root="$(cd -- "$delivery_dir/../.." && pwd)"

cd "$repo_root"

if ! git rev-parse --is-inside-work-tree > /dev/null 2>&1; then
    cat >&2 <<'MESSAGE'
✗ 当前目录不是 Git 仓库。
  说明：你手上的可能是原始工程包。请先克隆团队仓库：
      git clone <团队仓库地址> && cd <仓库目录>
  然后按 UPLOAD-TO-GITHUB.md 第 5 节的清单，把文件按相同相对路径复制进去，
  再重新运行本脚本。
MESSAGE
    exit 1
fi

# 我负责的文件 = 生产代码 4 个 + 交付目录（另外两个共享文件单独列出，便于按需取舍）。
owned_paths=(
    "formal/include/tribe/expansion_game.hpp"
    "formal/src/expansion_game.cpp"
    "formal/src/world_map_catalog.hpp"
    "formal/src/world_map_catalog.cpp"
    "deliverables/member3-map-mission"
)
shared_paths=(
    "formal/src/console_ui.cpp"   # 成员4 的文件：地图页结算点星标与提示行
    "CMakeLists.txt"              # 成员1/成员5 的文件：测试源注册与可选工具目标
)

echo "== 1/3 暂存我负责的模块 =="
for path in "${owned_paths[@]}"; do
    if [[ -e "$path" ]]; then
        git add -- "$path"
        echo "  + $path"
    else
        echo "  ! 跳过（不存在）：$path" >&2
    fi
done

echo
echo "== 2/3 暂存需要队友确认的共享文件（如已完成沟通，可继续；否则先取消暂存） =="
for path in "${shared_paths[@]}"; do
    if [[ -e "$path" ]]; then
        git add -- "$path"
        echo "  + $path"
    else
        echo "  ! 跳过（不存在）：$path" >&2
    fi
done

echo
echo "== 3/3 检查暂存区是否混入构建产物 =="
staged="$(git diff --cached --name-only)"
if [[ -z "$staged" ]]; then
    echo "✗ 暂存区为空：请确认改动是否还在，或先运行 run-delivery-checks.sh。" >&2
    exit 1
fi

forbidden="$(printf '%s\n' "$staged" | grep -E '^(out/|saves/|dist/)|\.DS_Store$|\.(o|a|obj|exe|dSYM|dll|so|dylib)$|(^|/)(tribe-dawn|tribe-formal-tests|tribe-map-playthrough)$' || true)"

# .DS_Store 是 macOS 目录元数据，纯噪声且会自动再生：自动清理，不算提交错误。
ds_store="$(printf '%s\n' "$forbidden" | grep -E '\.DS_Store$' || true)"
if [[ -n "$ds_store" ]]; then
    echo "  ! 发现 macOS 目录元数据，已自动清理（不会进入提交）："
    printf '%s\n' "$ds_store" | sed 's/^/      /'
    while IFS= read -r noise; do
        [[ -n "$noise" ]] || continue
        git rm --cached --quiet -- "$noise" 2>/dev/null || true
        rm -f -- "$noise"
    done <<< "$ds_store"
    forbidden="$(printf '%s\n' "$forbidden" | grep -vE '\.DS_Store$' || true)"
fi

if [[ -n "$forbidden" ]]; then
    cat >&2 <<MESSAGE
✗ 暂存区里有不该提交的文件：
$forbidden

处理方式：
    git reset                                   # 全部取消暂存
    git reset -- <上面的路径>                    # 或只取消其中几个
然后重新运行本脚本；构建产物与本地存档都不应进入仓库。
MESSAGE
    exit 1
fi

echo "  ✓ 暂存区没有 out/、saves/、.DS_Store 或二进制文件"
echo
echo "== 暂存清单（$(printf '%s\n' "$staged" | wc -l | tr -d ' ') 个文件） =="
printf '%s\n' "$staged" | sed 's/^/  /'

cat <<'NEXT'

== 下一步 ==
  1) 目视确认上面的清单（应只有 6 个生产/构建文件 + deliverables/member3-map-mission/ 下的文件）
  2) 提交（提交信息可直接用 UPLOAD-TO-GITHUB.md 第 3 节）：
       git commit -m "feat(map): 补齐地图任务路线提示、地点档案与失败路径一致性"
  3) 推送并开 PR（PR 正文见 UPLOAD-TO-GITHUB.md 第 4 节，可直接粘贴）
       git push -u origin <你的分支名>

  注意：如果这次只想提交“我自己的模块”，请先执行 git reset，再删掉本脚本里 shared_paths 的两项。
NEXT
