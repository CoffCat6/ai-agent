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
# 安装后刷新桌面数据库与图标缓存；工具缺失（极简容器）时静默跳过，
# 与 rpm %post 同一套钩子。
cat > "$staging/DEBIAN/postinst" <<'EOF'
#!/bin/sh
update-desktop-database -q 2>/dev/null || true
gtk-update-icon-cache -q /usr/share/icons/hicolor 2>/dev/null || true
exit 0
EOF
chmod 0755 "$staging/DEBIAN/postinst"
debOut="$OutDir/nexusai_${version}_${arch}.deb"
dpkg-deb --root-owner-group --build "$staging" "$debOut"

# ── rpm ─────────────────────────────────────────────────────
# DEBIAN/ 是 deb 专属元数据，不能出现在 rpm 的 buildroot 里。
rm -rf "$staging/DEBIAN"
rpmbuildDir="$OutDir/rpmbuild"
rm -rf "$rpmbuildDir"
mkdir -p "$rpmbuildDir"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
spec="$rpmbuildDir/SPECS/nexusai.spec"
changelogDate="$(date "+%a %b %d %Y")"
cat > "$spec" <<EOF
Name: nexusai
Version: $version
Release: 1
Summary: Local-first AI agent desktop app
License: MIT
URL: https://github.com/CoffCat6/ai-agent
Group: Applications/Productivity
AutoReqProv: no
# 依赖用 soname 文件名而非发行版包名：Fedora 的 mesa-libGL/libX11 与
# openSUSE 的 Mesa-libGL1/libX11-6 命名不同，包名硬列在跨发行版安装时会
# 变成不可满足依赖；soname（libX11.so.6()(64bit) 形态）在所有 rpm 系上都
# 存在且由 file-provides 天然满足。
Requires: libc.so.6()(64bit), libstdc++.so.6()(64bit), libgcc_s.so.1()(64bit), libGL.so.1()(64bit), libEGL.so.1()(64bit), libfontconfig.so.1()(64bit), libX11.so.6()(64bit), libxcb.so.1()(64bit), libxkbcommon.so.0()(64bit), libxkbcommon-x11.so.0()(64bit), libsecret-1.so.0()(64bit), libdbus-1.so.3()(64bit)
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

%post
# 与 deb postinst 同一套钩子；极简环境缺工具时静默跳过。
update-desktop-database -q 2>/dev/null || true
gtk-update-icon-cache -q /usr/share/icons/hicolor 2>/dev/null || true

%postun
update-desktop-database -q 2>/dev/null || true
gtk-update-icon-cache -q /usr/share/icons/hicolor 2>/dev/null || true

%changelog
* $changelogDate NexusAI project <noreply@github.com> - ${version}-1
- Local-first AI agent desktop app (automated release build).
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
