#pragma once

#include "ITool.h"

#include <QString>
#include <QStringList>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QStandardPaths>

namespace NexusAI {

// Sandbox security validator - enforces path and tool-level restrictions.
//
// docs/17 A1（定稿 v2）判定顺序（优先级从高到低）：
//   1. 系统目录硬封锁（任何策略下不可读写）；
//   2. 黑名单 = 绝对拒绝层：命中即拒，优先于一切允许规则（含 FullDisk
//      与项目目录）——防止"导入整个盘当项目"把 ~/.ssh 暴露给模型；
//   3. 允许层（读）= 用户目录白名单 ∪ 当前项目目录（∪ FullDisk 全盘）；
//      允许层（写）= 白名单 ∪ Home（项目目录只读，不进写允许层）。
//
// 黑名单条目两种形态：目录前缀（native 路径，含其子树）与 glob 模式
//（QDir::match 语义，如 *.kdbx，对完整路径与文件名都尝试）。匹配统一在
// canonicalize 之后进行（validatePath 入口已做），".." 与 symlink 无法
// 绕过。默认黑名单编译期内置（保守清单，用户已确认）；设置键
// security/readDenyList 只存用户追加项。
class Sandbox {
public:
    enum class AccessMode {
        Read,
        Write
    };

    // Read policy (audit P1-8): UserDirectories restricts reads to the
    // user-accessible allowlist; FullDisk restores the legacy behavior of
    // only blocking system directories. Opt-in via settings, default off.
    enum class ReadPolicy {
        UserDirectories,
        FullDisk
    };
    static void setReadPolicy(ReadPolicy policy) { s_readPolicy = policy; }
    static ReadPolicy readPolicy() { return s_readPolicy; }

    // docs/17 A1: 项目目录（当前活动项目的 root_path；空 = 无项目）。
    // 只影响读取允许层；写策略不因项目而扩大。
    static void setProjectDir(const QString& path) {
        s_projectDir = path.isEmpty()
            ? QString()
            : QDir::toNativeSeparators(path);
    }
    static QString projectDir() { return s_projectDir; }

    // docs/17 A1: 用户自定义黑名单（与内置默认清单合并生效）。
    static void setUserDenyList(const QStringList& entries) {
        s_userDenyList.clear();
        for (const QString& entry : entries) {
            const QString trimmed = entry.trimmed();
            if (!trimmed.isEmpty())
                s_userDenyList.append(trimmed);
        }
    }

    // validatePath 的拒绝原因（A5：提示区分被哪一层拒绝）。
    enum class DenyReason {
        None,          // 放行
        SystemPath,    // 系统目录硬封锁
        DenyList,      // 黑名单命中
        ReadPolicy,    // 不在读取允许层
        WritePolicy    // 不在写入允许层
    };

    // Validate that a file path is within allowed directories.
    // The path is canonicalized before matching, so ".." segments, relative
    // paths and (for existing files) symlinks cannot be used to escape the
    // policy. Prefix matches are boundary-aware: an allowed base directory
    // does not authorize a sibling such as "Documents2".
    // `reason`（可空出参）在拒绝时返回具体层级，供工具拼接提示。
    static bool validatePath(const QString& path, bool isWrite,
                             DenyReason* reason = nullptr) {
        const auto deny = [reason](DenyReason r) {
            if (reason) *reason = r;
            return false;
        };
        if (path.isEmpty()) return deny(isWrite ? DenyReason::WritePolicy
                                                : DenyReason::ReadPolicy);

        const QString normalizedPath = cleanPath(path);

        // ── 1. 系统目录硬封锁 ──
        static const QStringList blockedPaths = {
            "/etc", "/usr", "/bin", "/sbin", "/boot", "/sys", "/proc",
            "C:\\Windows", "C:\\Program Files", "C:\\Program Files (x86)"
        };
        for (const auto& blocked : blockedPaths) {
            if (isWithin(normalizedPath, QDir::toNativeSeparators(blocked)))
                return deny(DenyReason::SystemPath);
        }

        // ── 2. 黑名单（绝对拒绝层，先于任何允许规则）──
        if (isDenied(normalizedPath))
            return deny(DenyReason::DenyList);

        // User-accessible directories. READS use a narrower list than
        // writes: allowing all of Home kept the audited exposure alive
        // (e.g. ~/.ssh, browser profiles live under the home directory).
        static const QStringList readDirs = []() {
            QStringList dirs;
            dirs << QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                 << QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
                 << QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
                 << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            return dirs;
        }();

        const auto withinUserDir = [](const QString& p, const QStringList& dirs) {
            for (const auto& allowed : dirs) {
                if (!allowed.isEmpty()
                        && isWithin(p, QDir::toNativeSeparators(allowed))) {
                    return true;
                }
            }
            return false;
        };

        // ── 3. 允许层 ──
        if (isWrite) {
            // Writes additionally allow the whole home directory (users may
            // create files anywhere they own them). 项目目录只读：不进写层。
            QStringList writeDirs = readDirs;
            writeDirs << QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
            if (withinUserDir(normalizedPath, writeDirs)) {
                if (reason) *reason = DenyReason::None;
                return true;
            }
            return deny(DenyReason::WritePolicy);
        }

        // Reads: user allowlist ∪ 当前项目目录；FullDisk（黑名单仍生效）
        // 恢复"只拦系统目录"的行为。
        if (s_readPolicy == ReadPolicy::FullDisk) {
            if (reason) *reason = DenyReason::None;
            return true;
        }
        if (withinUserDir(normalizedPath, readDirs)
                || (!s_projectDir.isEmpty()
                    && isWithin(normalizedPath, s_projectDir))) {
            if (reason) *reason = DenyReason::None;
            return true;
        }
        return deny(DenyReason::ReadPolicy);
    }

    // docs/17 A5: 拒绝提示文案（工具层直接拼进 ToolResult::error）。
    static QString denialHint(DenyReason reason) {
        switch (reason) {
        case DenyReason::SystemPath:
            return QStringLiteral(
                "Path blocked: system directories are never accessible");
        case DenyReason::DenyList:
            return QStringLiteral(
                "Path blocked by the deny list (sensitive files/directories). "
                "Review it under Settings > Security");
        case DenyReason::ReadPolicy:
            return QStringLiteral(
                "Path blocked by sandbox policy: reads are limited to user "
                "directories and the active project. Grant access under "
                "Settings > Security (read policy) or import the folder as "
                "a project");
        case DenyReason::WritePolicy:
            return QStringLiteral(
                "Path blocked by sandbox policy: writes are only allowed in "
                "user directories");
        case DenyReason::None:
            break;
        }
        return QString();
    }

private:
    Sandbox() = delete; // Static utility class

    // docs/17 A1: 默认黑名单（保守清单：明确凭据/密钥，不激进入门）。
    // 目录条目按 Home 展开；glob 条目对完整路径与文件名匹配。
    static QStringList defaultDenyList() {
        const QString home =
            QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
        return {
            home + "/.ssh",      // SSH 私钥/known_hosts
            home + "/.gnupg",    // GPG 私钥环
            home + "/.aws",      // AWS 凭据
            "*.pem",             // 证书/私钥（任意目录）
            "*.key",
            "*.ppk",             // PuTTY 私钥
            "*.kdbx",            // KeePass 加密库
        };
    }

    // 黑名单匹配：目录条目做边界感知前缀匹配；glob 条目用 QDir::match
    // 同时尝试完整路径与文件名（Windows 下大小写不敏感）。
    static bool isDenied(const QString& normalizedPath) {
        QStringList entries = defaultDenyList();
        entries.append(s_userDenyList);
        const QString fileName =
            QFileInfo(normalizedPath).fileName();
        for (const QString& entry : std::as_const(entries)) {
            if (entry.contains(QLatin1Char('*')) || entry.contains(QLatin1Char('?'))) {
                if (QDir::match(entry, normalizedPath)
                        || QDir::match(entry, fileName))
                    return true;
                continue;
            }
            const bool absolute = entry.size() > 1
                && (entry.at(1) == QLatin1Char(':')
                    || entry.startsWith(QLatin1Char('/'))
                    || entry.startsWith(QLatin1Char('\\')));
            if (absolute) {
                // 绝对目录条目：边界感知前缀匹配（含其子树）。
                if (isWithin(normalizedPath, QDir::toNativeSeparators(entry)))
                    return true;
            } else {
                // 相对名条目（如 ".git"）：命中路径中任意一级目录段。
                const QStringList parts =
                    normalizedPath.split(QDir::separator(), Qt::SkipEmptyParts);
                for (const QString& part : parts) {
                    if (part.compare(entry, Qt::CaseInsensitive) == 0)
                        return true;
                }
            }
        }
        return false;
    }

    // Resolve symlinks when the target exists, otherwise lexically collapse
    // "."/".." segments (works for files that are about to be created).
    static QString cleanPath(const QString& path) {
        const QFileInfo info(path);
        const QString canonical = info.canonicalFilePath();
        return QDir::toNativeSeparators(canonical.isEmpty()
            ? info.absoluteFilePath() : canonical);
    }

    // True when path equals base or lies underneath it
    static bool isWithin(const QString& path, const QString& base) {
        if (base.isEmpty()) return false;
        if (!path.startsWith(base, Qt::CaseInsensitive)) return false;
        if (path.size() == base.size()) return true;
        const QChar next = path.at(base.size());
        return next == '/' || next == '\\'
            || base.endsWith('/') || base.endsWith('\\');
    }

    inline static ReadPolicy s_readPolicy{ReadPolicy::UserDirectories};
    inline static QString s_projectDir;
    inline static QStringList s_userDenyList;
};

} // namespace NexusAI
