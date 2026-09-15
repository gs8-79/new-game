#!/usr/bin/env bash
# 成员3交付物：打包脚本（课程提交 / 存档备份用）。
#
# 作用：把整个交付目录打成 tar.gz 与 zip，并额外附上这些自包含内容：
#   - 交付测试文件的一份副本（GitHub 提交时它在 deliverables/ 内，本项目约定位置也在此，
#     这里再复制一份到包内 tests/ 便于单独查看）；
#   - 一键验收脚本、试玩实录、Mermaid 图、测试日志；
#   - 一个 MANIFEST.txt，列出包内文件与生成时间、以及本次验收结果。
#
# 用法：bash deliverables/member3-map-mission/tools/package-deliverables.sh
# 输出：dist/member3-map-mission-<时间戳>.tar.gz 与 .zip
# 退出码：0 = 打包完成；非 0 = 打包失败。

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
delivery_dir="$(cd -- "$script_dir/.." && pwd)"
repo_root="$(cd -- "$delivery_dir/../.." && pwd)"
stamp="$(date +%Y%m%d-%H%M%S)"
package_name="member3-map-mission-$stamp"
dist_dir="$repo_root/dist"
stage_dir="$dist_dir/$package_name"

mkdir -p "$dist_dir"
rm -rf "$stage_dir"
mkdir -p "$stage_dir"

# 1. 复制交付目录（排除 Python/编辑器缓存等噪声）。
echo "== 1/3 复制交付目录 =="
(cd "$delivery_dir" && tar --exclude='__pycache__' --exclude='.DS_Store' -cf - .) | (cd "$stage_dir" && tar -xf -)

# 2. 附上交付测试与相关生产文件的副本，保证压缩包自包含、可单独评审。
echo "== 2/3 附上测试文件与生产代码快照 =="
mkdir -p "$stage_dir/code-snapshot/formal/include/tribe" "$stage_dir/code-snapshot/formal/src" \
         "$stage_dir/code-snapshot/formal/tests"
cp "$delivery_dir/tests/map_mission_delivery_tests.cpp" \
   "$stage_dir/code-snapshot/formal/tests/map_mission_delivery_tests.cpp"
for file in formal/include/tribe/expansion_game.hpp formal/src/expansion_game.cpp \
            formal/src/world_map_catalog.hpp formal/src/world_map_catalog.cpp formal/src/console_ui.cpp; do
    cp "$repo_root/$file" "$stage_dir/code-snapshot/$file"
done
cp "$repo_root/CMakeLists.txt" "$stage_dir/code-snapshot/CMakeLists.txt"

# 3. 生成 MANIFEST 与压缩包。
echo "== 3/3 生成 MANIFEST 与压缩包 =="
{
    echo "成员3交付物打包清单"
    echo "生成时间：$(date '+%F %T %Z')"
    echo "来源目录：deliverables/member3-map-mission"
    echo
    echo "包内结构："
    echo "  docs/                 交付文档 01-06（分析、地图与路线、任务说明表、优化说明、测试说明、协作与评审）"
    echo "  tests/                交付测试源码（13 例 189 断言）"
    echo "  tools/                试玩工具、路线表、一键验收与打包脚本"
    echo "  artifacts/            测试日志、试玩实录、路线命令表、Mermaid 图"
    echo "  code-snapshot/        本次交付涉及的生产代码快照（便于脱离仓库单独评审）"
    echo
    echo "最近一次验收结果（来自 artifacts/test-run-Release.log）："
    grep -E "tests passed" "$delivery_dir/artifacts/test-run-Release.log" 2>/dev/null || echo "  （未找到测试日志）"
    grep -E "命令总数" "$delivery_dir/artifacts/playthrough-transcript.txt" 2>/dev/null | tail -1 || true
    echo
    echo "包内文件清单："
    (cd "$stage_dir" && find . -type f | sort | sed 's/^/  /')
} > "$stage_dir/MANIFEST.txt"

(cd "$dist_dir" && tar -czf "$package_name.tar.gz" "$package_name")
if command -v zip > /dev/null 2>&1; then
    (cd "$dist_dir" && zip -qr "$package_name.zip" "$package_name")
fi
rm -rf "$stage_dir"

echo "✓ 已生成："
ls -lh "$dist_dir"/"$package_name".* | sed 's/^/    /'
echo "  提示：dist/ 同样不应提交到 Git 仓库（见 UPLOAD-TO-GITHUB.md 第 6 节）。"
