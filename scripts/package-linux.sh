#!/usr/bin/env bash
# NexusAI Linux 安装包（deb + rpm，docs/49 发布矩阵）。
#   ./scripts/package-linux.sh -AppDir deploy/AppDir -OutDir deploy
#
# 输入是 deploy.sh 留下的 AppDir（Qt 运行时已捆绑、rpath 已改写）——deb/rpm
# 与 AppImage 同源同构，只是布局不同：应用本体进 /opt/NexusAI（Qt6 按
# libQt6Core 位置自定位，prefix/plugins/qml 随目录树整体迁移仍然成立），
# /usr/bin 放符号链接（.desktop 的 Exec=nexus_ai 经 PATH 解析），
# /usr/share 放桌面集成文件。C 运行时与桌面栈（GL/X11/fontconfig/
# libsecret 等）按 AppImage 同一策略交给目标系统。
#
# 依赖：dpkg-deb（deb 构建）、rpmbuild（rpm 构建，Ubuntu 上 apt install rpm）。
set -euo pipefail

AppDir=""
OutDir="deploy"
while [ $# -gt 0 ]; do
    case "$1" in
        -AppDir|-appdir)   AppDir="${2:?package: -AppDir needs a value}"; shift 2 ;;
        -OutDir|-outdir)   OutDir="${2:?package: -OutDir needs a value}"; shift 2 ;;
        *) echo "package: unknown argument: $1 (usage: package-linux.sh -AppDir DIR [-OutDir DIR])" >&2; exit 2 ;;
    esac
done

die() { echo "package: $*" >&2; exit 1; }

repoRoot="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repoRoot"

[ -n "$AppDir" ] || die "missing -AppDir (deploy.sh leaves it at <OutDir>/AppDir)"
AppDir="$(readlink -m "$AppDir")"
OutDir="$(readlink -m "$OutDir")"
[ -x "$AppDir/usr/bin/nexus_ai" ] || die "$AppDir/usr/bin/nexus_ai missing - not a deploy.sh AppDir?"

command -v dpkg-deb >/dev/null 2>&1 || die "dpkg-deb not found (build the deb on a Debian-family system)"
command -v rpmbuild  >/dev/null 2>&1 || die "rpmbuild not found (apt install rpm)"

version="$(sed -n 's/^project([[:space:]]*NexusAI[[:space:]]\+VERSION[[:space:]]\+\([0-9.]\+\).*/\1/p' CMakeLists.txt | head -1)"
version="${version:-0.0.0}"
arch="$(dpkg --print-architecture)"

# ── 组装安装树 ──────────────────────────────────────────────
# /opt/NexusAI：AppDir/usr 除 share 外的全部内容；/usr/share：桌面集成。
staging="$OutDir/pkg-staging"
rm -rf "$staging"
mkdir -p "$staging/opt/NexusAI" "$staging/usr/bin" "$staging/usr/share"
cp -a "$AppDir/usr/." "$staging/opt/NexusAI/"
rm -rf "$staging/opt/NexusAI/share"
cp -a "$AppDir/usr/share/." "$staging/usr/share/"
# 绝对符号链接：应用位置 /opt/NexusAI 是包布局的一部分，不随重定位。
ln -s /opt/NexusAI/bin/nexus_ai "$staging/usr/bin/nexus_ai"

# ── deb ─────────────────────────────────────────────────────
# Depends 只列跨发行版稳定的核心名：桌面栈（GL/X11/libsecret 等）与
# AppImage 同策略交给目标系统（t64 命名过渡期各发行版包名不一致，
# 硬列反而会在部分版本上变成不可满足依赖）。
mkdir -p "$staging/DEBIAN"
cat > "$staging/DEBIAN/control" <<EOF
Package: nexusai
Version: $version
Section: utils
Priority: optional
Architecture: $arch
Depends: libc6, libstdc++6, libgcc-s1
Maintainer: NexusAI project <noreply@github.com>
Description: Local-first AI agent desktop app
 Qt 6 Quick based AI agent with tool calling, sandboxing, MCP support
 and a local knowledge base. Bundles its own Qt runtime under
 /opt/NexusAI; expects a standard desktop stack (GL/X11/fontconfig).
EOF
debOut="$OutDir/nexusai_${version}_${arch}.deb"
dpkg-deb --root-owner-group --build "$staging" "$debOut"

# ── rpm ─────────────────────────────────────────────────────
# DEBIAN/ 是 deb 专属元数据，不能出现在 rpm 的 buildroot 里。
rm -rf "$staging/DEBIAN"
rpmbuildDir="$OutDir/rpmbuild"
rm -rf "$rpmbuildDir"
mkdir -p "$rpmbuildDir"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
spec="$rpmbuildDir/SPECS/nexusai.spec"
cat > "$spec" <<EOF
Name: nexusai
Version: $version
Release: 1
Summary: Local-first AI agent desktop app
License: MIT
AutoReqProv: no
Requires: glibc, libstdc++, libgcc, mesa-libGL, mesa-libEGL, fontconfig, libX11, libxcb, libxkbcommon, libxkbcommon-x11, libsecret, dbus-libs
%define debug_package %{nil}

%description
Qt 6 Quick based AI agent desktop app with tool calling, sandboxing,
MCP support and a local knowledge base. Bundles its own Qt runtime
under /opt/NexusAI; expects a standard desktop stack.

%files
/opt/NexusAI
/usr/bin/nexus_ai
/usr/share/applications/nexusai.desktop
/usr/share/doc/nexusai
/usr/share/icons/hicolor/*/apps/nexusai.png
EOF
rpmbuild -bb "$spec" \
    --define "_topdir $rpmbuildDir" \
    --buildroot "$staging"
# rpmbuild 产物落在 RPMS/<目标架构>/ 下，统一搬到 OutDir 根。
rpmOut="$OutDir/nexusai-${version}-1.$(uname -m).rpm"
find "$rpmbuildDir/RPMS" -name '*.rpm' -exec mv {} "$rpmOut" \;

echo "deb: $debOut"
echo "rpm: $rpmOut"
echo "Packaged OK: $(find "$staging" -type f | wc -l) files staged"
