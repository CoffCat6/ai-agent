#!/usr/bin/env bash
# docs/49 阶段 1：Linux 一键部署（AppImage）。
#   ./scripts/deploy.sh                                   -> 仓库根产出 NexusAI-<ver>-linux64.AppImage
#   ./scripts/deploy.sh -BuildDir build -OutDir deploy
#
# 流程：build -> cmake --install --prefix <OutDir>/AppDir/usr
#       （构建目录需以 -DNEXUS_LINUX_APPIMAGE=ON 配置，install(SCRIPT) 挂载的
#       Qt deploy 脚本同步捆绑 Qt 库/插件/QML 模块并改写 rpath，依赖 patchelf）
#       -> 组装 AppDir 根（AppRun / .DirIcon / desktop / 图标）
#       -> appimagetool 纯封装为 AppImage -> 完整性校验。
#
# 与 Windows deploy.ps1 的分工一致：Qt 运行时捆绑只走 Qt 官方 deploy 脚本
# （项目 QML 全部 qrc 内嵌，linuxdeploy-plugin-qt 的 AppDir 扫描判不出依赖），
# appimagetool 不做任何依赖分析。
#
# 前置：已配置的构建目录（-DNEXUS_LINUX_APPIMAGE=ON）、patchelf、curl。
# appimagetool 缺失时自动下载 continuous 版到 <OutDir>/tools/；
# APPIMAGETOOL_BIN 指定本地副本，APPIMAGETOOL_URL 覆盖下载地址
# （aarch64 机器需换 aarch64 的 URL）。
set -euo pipefail

BuildDir="build"
OutDir="deploy"
while [ $# -gt 0 ]; do
    case "$1" in
        -BuildDir|-builddir) BuildDir="${2:?deploy: -BuildDir needs a value}"; shift 2 ;;
        -OutDir|-outdir)     OutDir="${2:?deploy: -OutDir needs a value}"; shift 2 ;;
        *) echo "deploy: unknown argument: $1 (usage: deploy.sh [-BuildDir DIR] [-OutDir DIR])" >&2; exit 2 ;;
    esac
done

die() { echo "deploy: $*" >&2; exit 1; }

repoRoot="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repoRoot"

# 与 deploy.ps1 Assert-SafeOutDir 同语义：部署脚本只允许写自己拥有的目录。
# 拒绝：文件系统根、仓库根本身、仓库之外的 $HOME 及其子树。仓库工作区
# 内部（默认 OutDir = <repo>/deploy）始终放行——CI runner 的 workspace
# 就在 $HOME 下（Windows deploy.ps1 不受影响：workspace 在别的盘）。
assertSafeOutDir() {
    local dir
    dir="$(readlink -m "$1")"
    [ "$dir" = "/" ] && die "refusing to write into the filesystem root: $dir"
    [ "$dir" = "$repoRoot" ] && die "refusing to write into the repository root: $dir"
    case "$dir/" in
        "$repoRoot/"*) printf '%s' "$dir"; return 0 ;;
    esac
    if [ -n "${HOME:-}" ]; then
        [ "$dir" = "$HOME" ] && die "refusing to write into the user profile: $dir"
        case "$dir/" in
            "$HOME/"*) die "refusing to write into the user profile subtree: $dir (choose a dedicated deployment directory outside \$HOME)" ;;
        esac
    fi
    printf '%s' "$dir"
}

OutDir="$(assertSafeOutDir "$OutDir")"
AppDir="$OutDir/AppDir"

[ -f "$BuildDir/CMakeCache.txt" ] || die "build directory '$BuildDir' has no CMakeCache.txt - configure first: cmake -B $BuildDir -G Ninja -DNEXUS_LINUX_APPIMAGE=ON"
# Qt deploy 脚本生成在 .qt/deploy_qml_app_nexus_ai_<hash>.cmake（文件名带
# 哈希后缀，不能按名检查）——直接查缓存里的开关，这才是权威状态。
grep -q "^NEXUS_LINUX_APPIMAGE:BOOL=ON$" "$BuildDir/CMakeCache.txt" \
    || die "build dir '$BuildDir' was not configured with -DNEXUS_LINUX_APPIMAGE=ON - the Qt runtime would not be bundled"
command -v patchelf >/dev/null 2>&1 || die "patchelf not found - the Qt deploy script needs it to rewrite rpaths on Linux (apt install patchelf)"
command -v curl >/dev/null 2>&1 || die "curl not found - needed to fetch appimagetool"

version="$(sed -n 's/^project([[:space:]]*NexusAI[[:space:]]\+VERSION[[:space:]]\+\([0-9.]\+\).*/\1/p' CMakeLists.txt | head -1)"
version="${version:-0.0.0}"
outImage="$repoRoot/NexusAI-${version}-linux64.AppImage"

echo "== Building =="
cmake --build "$BuildDir"

# AppDir 是本脚本独占的暂存区（OutDir 已通过安全校验），整体重建以避免
# 旧产物残留——与 deploy.ps1 的 -Clean 同一动机。
rm -rf "$AppDir"
mkdir -p "$AppDir"

echo "== Installing to $AppDir/usr =="
cmake --install "$BuildDir" --prefix "$AppDir/usr"

# qtkeychain 实文件名（如 .so.0.15.0）与 exe 需要的 SONAME（如 .so.1）
# 在发行版包里靠符号链接衔接——install(FILES) 只拷了最终文件，SONAME
# 名字不存在则加载器回退系统路径（run16 实测）。按 exe 的 DT_NEEDED
# 重建相对符号链接，保证 $ORIGIN 内可解析。
soname="$(objdump -p "$AppDir/usr/bin/nexus_ai" | awk '/NEEDED/ {print $2}' | grep '^libqt6keychain' | head -1)"
if [ -n "$soname" ] && [ ! -e "$AppDir/usr/lib/$soname" ]; then
    real="$(find "$AppDir/usr/lib" -maxdepth 1 -name 'libqt6keychain.so.*' -type f | head -1)"
    [ -n "$real" ] || die "qtkeychain runtime library missing in AppDir/usr/lib - check the NEXUS_LINUX_APPIMAGE install rule"
    ln -s "$(basename "$real")" "$AppDir/usr/lib/$soname"
fi

# Qt deploy 脚本只改写 exe 与 Qt 库的 rpath，不碰显式捆绑的第三方库——
# qtkeychain（install 规则放入 usr/lib）的 Qt 依赖必须同样解析进 AppDir，
# 否则它的 libQt6DBus/libQt6Core 会走系统路径（版本可能与捆绑的 Qt 不符）。
for kclib in "$AppDir/usr/lib/"libqt6keychain.so*; do
    [ -e "$kclib" ] || continue
    patchelf --set-rpath '$ORIGIN' "$kclib"
done

echo "== Sanity checks =="
exe="$AppDir/usr/bin/nexus_ai"
[ -x "$exe" ] || die "usr/bin/nexus_ai missing (or not executable) in $AppDir"
[ -f "$AppDir/usr/share/applications/nexusai.desktop" ] || die "usr/share/applications/nexusai.desktop missing - desktop integration would be broken"

# Qt 运行时必须已捆绑进 AppDir：deploy 脚本没跑（配置开关漏了）或
# patchelf 缺失都会在这里暴露，而不是等用户拿到 AppImage 启动失败。
find "$AppDir/usr" -name 'libQt6Core.so.*' -print -quit | grep -q . \
    || die "libQt6Core was not bundled into $AppDir - did the Qt deploy script run?"

# rpath 改写成功的判据：exe 的 Qt/qtkeychain 依赖（含传递闭包，ldd 全列）
# 全部解析到 AppDir 内部。解析到构建机的路径 = 拿到别的机器上必死；
# "not found" 同样拦截。-i 同时覆盖 libQt6* 与 libqt6keychain。
# 例外：glibc/libstdc++/libdbus 等 C 运行时按 AppImage 惯例交给目标系统。
ldd "$exe" | grep -i "libqt6" | grep -qv "$AppDir" && {
    echo "ERROR: some Qt/keychain libraries resolve outside $AppDir:" >&2
    ldd "$exe" | grep -i "libqt6" | grep -v "$AppDir" >&2
    die "rpath rewrite failed (patchelf missing or deploy script skipped runtime dependencies)"
}

# QML 模块（QtQuick 等）必须随捆绑出现：项目 QML 走 qrc，但 Qt 的
# QML 插件模块（QtQuick/QtQuick/...）是运行时按 import 名解析的磁盘目录。
find "$AppDir/usr" -type d -name QtQuick -print -quit | grep -q . \
    || die "QtQuick QML module missing in $AppDir - GUI would fail at startup"

# AppDir 根布局（appimagetool 约定）：AppRun、根级 desktop、根级图标 + .DirIcon。
# AppRun 只做 exec：rpath 已由 deploy 脚本改为 $ORIGIN 相对，无需额外环境。
cat > "$AppDir/AppRun" <<'EOF'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
exec "$HERE/usr/bin/nexus_ai" "$@"
EOF
chmod +x "$AppDir/AppRun"
cp "$AppDir/usr/share/applications/nexusai.desktop" "$AppDir/nexusai.desktop"
cp "$AppDir/usr/share/icons/hicolor/256x256/apps/nexusai.png" "$AppDir/nexusai.png"
cp "$AppDir/nexusai.png" "$AppDir/.DirIcon"

echo "== Packing AppImage =="
if [ -n "${APPIMAGETOOL_BIN:-}" ] && [ -x "$APPIMAGETOOL_BIN" ]; then
    appimagetool="$APPIMAGETOOL_BIN"
else
    toolsDir="$OutDir/tools"
    mkdir -p "$toolsDir"
    appimagetool="$toolsDir/appimagetool-x86_64.AppImage"
    if [ ! -x "$appimagetool" ]; then
        url="${APPIMAGETOOL_URL:-https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage}"
        echo "  downloading appimagetool from $url"
        curl -fL --retry 3 "$url" -o "$appimagetool"
        chmod +x "$appimagetool"
    fi
fi
# --appimage-extract-and-run：CI/容器普遍没有 libfuse2，解包执行绕开 FUSE 依赖。
rm -f "$outImage"
"$appimagetool" --appimage-extract-and-run "$AppDir" "$outImage"

[ -f "$outImage" ] || die "appimagetool produced no output at $outImage"
echo "AppImage: $outImage"
echo "Deployed OK: $(find "$AppDir" -type f | wc -l) files staged, $(du -sh "$outImage" | cut -f1) AppImage"
