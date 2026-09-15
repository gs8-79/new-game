#!/usr/bin/env bash
# 成员3交付物一键验收脚本。
#
# 作用：重新配置并构建正式版、跑完整测试套件、重新生成试玩实录与文档用路线表，
#       最后把结果汇总打印，便于交付验收与队友复现。
#
# 用法：bash deliverables/member3-map-mission/tools/run-delivery-checks.sh [Release|Debug]
# 退出码：0 = 构建、测试、路线实录全部通过；非 0 = 任一步失败。
# 构建目录：默认 out/macos-formal-<Config>（项目自带脚本的约定），可用 TRIBE_BUILD_DIR 覆盖；
#           若检测到 Windows/Ninja 约定的 out/Formal-<Config>，也自动沿用。

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
delivery_dir="$(cd -- "$script_dir/.." && pwd)"
repo_root="$(cd -- "$delivery_dir/../.." && pwd)"
configuration="${1:-Release}"

case "$configuration" in
  Debug|Release) ;;
  *)
    echo "用法：bash run-delivery-checks.sh [Debug|Release]" >&2
    exit 2
    ;;
esac

build_dir="${TRIBE_BUILD_DIR:-$repo_root/out/macos-formal-$configuration}"
if [[ -n "${TRIBE_BUILD_DIR:-}" ]]; then
  build_dir="$TRIBE_BUILD_DIR"
elif [[ -d "$repo_root/out/Formal-$configuration" && ! -d "$build_dir" ]]; then
  build_dir="$repo_root/out/Formal-$configuration"
fi
artifacts="$delivery_dir/artifacts"
mkdir -p "$artifacts"

echo "== 1/5 配置与构建（$configuration，构建目录 $build_dir） =="
# TRIBE_BUILD_DELIVERY_TOOLS=ON 才会构建 tribe-map-playthrough；默认构建行为因此与其他成员保持一致。
cmake -S "$repo_root" -B "$build_dir" -DCMAKE_BUILD_TYPE="$configuration" -DTRIBE_BUILD_DELIVERY_TOOLS=ON
cmake --build "$build_dir" --parallel

echo
echo "== 2/4 运行完整测试套件（原有回归 + 成员3交付测试） =="
"$build_dir/tribe-formal-tests" | tee "$artifacts/test-run-$configuration.log"
# 任何 FAIL 都会让测试程序返回非 0，set -e 会在这里中止脚本。
grep -q "tests passed" "$artifacts/test-run-$configuration.log"

echo
echo "== 3/5 生成文档用路线命令表 =="
"$build_dir/tribe-map-playthrough" --route-markdown | tee "$artifacts/route-tables.md" > /dev/null
echo "已写入 $artifacts/route-tables.md"

echo
echo "== 4/5 生成 Mermaid 地图与路线图（图示与代码同源，路线不合法时会失败） =="
"$build_dir/tribe-map-playthrough" --mermaid | tee "$artifacts/map-diagrams.md" > /dev/null
echo "已写入 $artifacts/map-diagrams.md"

echo
echo "== 5/5 用真实引擎跑完整试玩路线并生成实录 =="
"$build_dir/tribe-map-playthrough" | tee "$artifacts/playthrough-transcript.txt" > /dev/null
echo "已写入 $artifacts/playthrough-transcript.txt"

echo
echo "== 汇总 =="
grep -E "tests passed" "$artifacts/test-run-$configuration.log" || true
tail -n 6 "$artifacts/playthrough-transcript.txt"
echo
echo "成员3交付验收完成：构建通过、测试全绿、试玩路线全部成功。"
