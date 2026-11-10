#pragma once

#include "ITool.h"

#include <QString>
#include <QStringList>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QReadWriteLock>
#include <QStandardPaths>
#include <QThreadStorage>

namespace NexusAI {

// docs/22 F4 (Windows only, defined in SandboxWin.cpp): resolves the
// deepest existing component of the path through NTFS reparse points
// (junctions/symlinks) via GetFinalPathNameByHandle; empty when nothing on
// the chain exists or opens. Qt's canonicalFilePath() does NOT traverse
// junctions on Windows, so the sandbox needs the native resolver.
#ifdef Q_OS_WIN
QString sandboxNativeFinalPath(const QString& path);

// docs/36 B4-D（B4 收口记录）: Windows system directories resolved from the OS
// instead of a hard-coded "C:\". Reads SystemRoot / windir / ProgramFiles /
// ProgramFiles(x86) / ProgramW6432 / ProgramData (all drive-agnostic and
// aware of a custom SystemRoot), with GetWindowsDirectoryW as a fallback.
// Empty/unset entries are skipped. Returned paths are native-separated.
QStringList sandboxWindowsSystemDirectories();
#endif

// Sandbox security validator - enforces path and tool-level restrictions.
//
// docs/17 A1（定稿 v2）判定顺序（优先级从高到低）：
//   1. 系统目录硬封锁（任何策略下不可读写）；
//   2. 黑名单 = 绝对拒绝层：命中即拒，优先于一切允许规则（含 FullDisk
//      与项目目录）——防止"导入整个盘当项目"把 ~/.ssh 暴露给模型；
//   3. 允许层（读）= 用户目录白名单 ∪ 当前项目目录（∪ FullDisk 全盘）；
//      允许层（写）= 白名单 ∪ Home（∪ 项目目录，仅在安全页显式开启
//      projectWrite 后——默认项目只读）。
//
// 黑名单条目两种形态：目录前缀（native 路径，含其子树）与 glob 模式
//（QDir::match 语义，如 *.kdbx，对完整路径与文件名都尝试）。匹配统一在
// canonicalize 之后进行（validatePath 入口已做），".." 与 symlink 无法
// 绕过。默认黑名单编译期内置（保守清单，用户已确认）；设置键
// security/readDenyList 只存用户追加项。
//
// "当前项目目录"的生效值（currentProjectDir）：工具执行线程上的快照
//（ExecutionScope，AIAgent 派发工具时钉住任务开始时的根）优先，回落到
// 全局活动项目（AgentViewModel 切换时写入）。后者供无任务上下文的调用
// 方（测试、非工具路径）使用。
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
    static void setReadPolicy(ReadPolicy policy) {
        QWriteLocker lock(&s_policyLock);   // docs/24 H8
        s_readPolicy = policy;
    }
    static ReadPolicy readPolicy() {
        QReadLocker lock(&s_policyLock);
        return s_readPolicy;
    }

    // docs/17 A1: 项目目录（当前活动项目的 root_path；空 = 无项目）。
    // 只影响读取允许层；写策略仅在 projectWrite 开启时扩大。
    static void setProjectDir(const QString& path) {
        QWriteLocker lock(&s_policyLock);
        s_projectDir = path.isEmpty()
            ? QString()
            : QDir::toNativeSeparators(path);
    }
    static QString projectDir() {
        QReadLocker lock(&s_policyLock);
        return s_projectDir;
    }

    // docs/17 P1-5: 工具执行域。构造时把 projectRoot 钉到当前线程的
    // 局部槽上，析构清除；必须构造在执行工具的那个池线程里（AIAgent 的
    // QtConcurrent lambda 内）。任务进行中用户切换活动项目不再影响
    // 进行中的工具调用。
    class ExecutionScope {
    public:
        explicit ExecutionScope(const QString& projectRoot) {
            s_execProjectDir.setLocalData(projectRoot.isEmpty()
                ? QString()
                : QDir::toNativeSeparators(projectRoot));
        }
        ~ExecutionScope() { s_execProjectDir.setLocalData(QString()); }
        ExecutionScope(const ExecutionScope&) = delete;
        ExecutionScope& operator=(const ExecutionScope&) = delete;
    };

    // 当前生效的项目目录：执行线程快照（非空时）优先，否则全局活动项目。
    static QString currentProjectDir() {
        // docs/24 H8: 读写 s_projectDir 需持锁；s_execProjectDir 是本线程
        // 槽，无需同步。（validatePath 持读锁进入后递归到此——锁为递归模式。）
        QReadLocker lock(&s_policyLock);
        if (s_execProjectDir.hasLocalData()) {
            const QString snapshot = s_execProjectDir.localData();
            if (!snapshot.isEmpty()) return snapshot;
        }
        return s_projectDir;
    }

    // docs/17 P1-4: 项目目录写入放行开关（安全页 security/allowProjectWrite，
    // 默认关——A1 的"项目只读"默认语义保留）。
    static void setProjectWriteEnabled(bool enabled) {
        QWriteLocker lock(&s_policyLock);
        s_projectWrite = enabled;
    }
    static bool projectWriteEnabled() {
        QReadLocker lock(&s_policyLock);
        return s_projectWrite;
    }

    // docs/17 P0-2: 相对路径锚定。项目激活时相对路径以项目根为基准解析，
    // 否则保持原语义（QFile/QDir 按进程 CWD 解析——模型给相对路径时读到
    // 的往往是应用启动目录下的同名文件，"读不到正确信息"的直接来源）。
    // 工具层与 validatePath 统一走这里；返回值再经 cleanPath canonicalize，
    // ".." 与 symlink 无法借道绕过沙箱。
    static QString resolvePath(const QString& path) {
        // docs/24 H8: 读 currentProjectDir()（内部持锁，递归模式安全）。
        const QString root = currentProjectDir();
        if (root.isEmpty())
            return path;   // 无项目：保持原语义（由调用方判断是否可用）
        // 无项目以外的两种"指向项目根"的写法：空串与 "."（含 "./"）。
        // 模型常把它们当作"当前目录"，空串此前会原样透传成空、随后落到
        // 进程 CWD，或触发调用方的 "Path is required"。
        if (path.isEmpty() || path == QLatin1String("."))
            return root;
        if (QDir::isRelativePath(path))
            return QDir(root).absoluteFilePath(path);
        return path;
    }

    // docs/17 A1: 用户自定义黑名单（与内置默认清单合并生效）。
    static void setUserDenyList(const QStringList& entries) {
        QWriteLocker lock(&s_policyLock);
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
        Traversal,     // 路径穿越（".." 段，拒绝先于词法折叠）
        InvalidPath,   // 非法路径（Windows 保留设备名）
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
        return validatePathResolved(path, isWrite, nullptr, reason);
    }

    // docs/36 B4-D（B4 收口记录）: same policy decision as validatePath, but also returns
    // the canonical path that was actually evaluated (reparse points /
    // symlinks resolved, deepest existing ancestor + lexical tail for
    // not-yet-existing targets). A write caller MUST open the returned path
    // instead of its raw input: if an attacker swaps a junction between
    // validate and open, the raw link text would follow the new target while
    // this canonical path stays pinned to the checked destination.
    // `resolvedPath` is cleared on every denial.
    static bool validatePathResolved(const QString& path, bool isWrite,
                                     QString* resolvedPath,
                                     DenyReason* reason = nullptr) {
        if (resolvedPath) resolvedPath->clear();
        const auto deny = [reason](DenyReason r) {
            if (reason) *reason = r;
            return false;
        };
        if (path.isEmpty()) return deny(isWrite ? DenyReason::WritePolicy
                                                : DenyReason::ReadPolicy);

        // docs/24 H8: 一次读锁覆盖整次判定——四个策略静态量由 GUI 线程写、
        // 池线程读，此前是无同步的裸静态（递归模式允许内部辅助再入读锁）。
        // 锁内包含文件系统解析（cleanPath），毫秒级；setter 稀少，无争用。
        QReadLocker policyLock(&s_policyLock);

        // docs/22 F4: device / extended-length prefixes bypass every
        // normalization Qt can perform (canonical or ancestor resolution
        // both fail on them) — reject before anything else.
        if (path.startsWith(QStringLiteral("\\\\.\\"))
                || path.startsWith(QStringLiteral("\\\\?\\")))
            return deny(DenyReason::SystemPath);

        // P0-2: 先锚定相对路径（项目激活时以项目根为基准），再 canonicalize。
        const QString anchored = resolvePath(path);

        // docs/36 B4-D（B4 收口记录）: QDir::cleanPath collapses ".." lexically BEFORE any
        // reparse point is resolved, which is exactly the wrong order on
        // Windows: opening "C:\proj\junction\..\evil" resolves the junction
        // first, then "..", and can land outside the project even though the
        // lexical clean form looks safe. Reject parent segments outright
        // (both read and write) instead of trusting lexical collapse.
        if (containsParentSegment(anchored))
            return deny(DenyReason::Traversal);

        const QString normalizedPath = cleanPath(anchored);

        // ── 1. 系统目录硬封锁（由 OS 环境/GetWindowsDirectory 提供，
        //    不假设盘符；大小写不敏感比较）──
        const QStringList blockedPaths = systemBlockedPaths();
        for (const auto& blocked : blockedPaths) {
            if (isWithin(normalizedPath, QDir::toNativeSeparators(blocked)))
                return deny(DenyReason::SystemPath);
        }

        // Windows 保留设备名（CON/NUL/COM1…）无法创建为普通文件，且某些
        // 写法会被 Win32 解析到设备对象，绕过目录策略。
        if (isReservedDeviceName(normalizedPath))
            return deny(DenyReason::InvalidPath);

        const auto allow = [&]() {
            if (reason) *reason = DenyReason::None;
            if (resolvedPath) *resolvedPath = normalizedPath;
            return true;
        };

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
            // create files anywhere they own them). 项目目录默认只读；
            // 仅当安全页显式开启 projectWrite（P1-4）才进入写允许层。
            QStringList writeDirs = readDirs;
            writeDirs << QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
            if (s_projectWrite) {
                const QString project = currentProjectDir();
                if (!project.isEmpty()) writeDirs << project;
            }
            if (withinUserDir(normalizedPath, writeDirs))
                return allow();
            return deny(DenyReason::WritePolicy);
        }

        // Reads: user allowlist ∪ 当前项目目录；FullDisk（黑名单仍生效）
        // 恢复"只拦系统目录"的行为。
        if (s_readPolicy == ReadPolicy::FullDisk)
            return allow();
        const QString activeProject = currentProjectDir();
        if (withinUserDir(normalizedPath, readDirs)
                || (!activeProject.isEmpty()
                    && isWithin(normalizedPath, activeProject)))
            return allow();
        return deny(DenyReason::ReadPolicy);
    }

    // docs/17 A5: 拒绝提示文案（工具层直接拼进 ToolResult::error）。
    static QString denialHint(DenyReason reason) {
        switch (reason) {
        case DenyReason::SystemPath:
            return QStringLiteral(
                "Path blocked: system directories are never accessible");
        case DenyReason::Traversal:
            return QStringLiteral(
                "Path escapes the project root (parent '..' segments are not "
                "accepted)");
        case DenyReason::InvalidPath:
            return QStringLiteral(
                "Path blocked: invalid path (Windows reserved device names "
                "are not accepted)");
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
                "user directories. Enable project writes under Settings > "
                "Security if the file belongs to the active project");
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
        QReadLocker lock(&s_policyLock);   // docs/24 H8: s_userDenyList
        static const QStringList defaultEntries = defaultDenyList();
        QStringList entries = defaultEntries;
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

    // docs/36 B4-D（B4 收口记录）: 系统目录来源。Windows 由 OS 提供（SystemRoot 等，
    // 支持非 C: 系统盘与自定义 SystemRoot）；Unix 用固定 FHS 目录。
    // 每次调用重新读取环境，确保运行期安装盘变化/测试注入都能生效。
    static QStringList systemBlockedPaths() {
#ifdef Q_OS_WIN
        return sandboxWindowsSystemDirectories();
#else
        static const QStringList kUnixSystemDirs = {
            QStringLiteral("/etc"), QStringLiteral("/usr"),
            QStringLiteral("/bin"), QStringLiteral("/sbin"),
            QStringLiteral("/boot"), QStringLiteral("/sys"),
            QStringLiteral("/proc")
        };
        return kUnixSystemDirs;
#endif
    }

    // True when the path carries a ".." segment. Checked on the anchored
    // input BEFORE QDir::cleanPath so a junction followed by ".." can never
    // be silently re-ordered into a safe-looking lexical path.
    static bool containsParentSegment(const QString& path) {
        const QString unified = QDir::fromNativeSeparators(path);
        const QStringList parts =
            unified.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        for (const QString& part : parts) {
            if (part == QLatin1String(".."))
                return true;
        }
        return false;
    }

    // Win32 reserved device names are not creatable as ordinary files and can
    // be interpreted as device objects by some APIs. Check every segment with
    // the extension/trailing-dot form normalized away (CON.txt == CON).
    static bool isReservedDeviceName(const QString& path) {
#ifdef Q_OS_WIN
        const QStringList parts = QDir::fromNativeSeparators(path)
            .split(QLatin1Char('/'), Qt::SkipEmptyParts);
        static const QStringList kFixed = {
            QStringLiteral("CON"), QStringLiteral("PRN"),
            QStringLiteral("AUX"), QStringLiteral("NUL"),
            QStringLiteral("CLOCK$")
        };
        for (QString part : parts) {
            while (!part.isEmpty()
                   && (part.endsWith(QLatin1Char('.'))
                       || part.endsWith(QLatin1Char(' ')))) {
                part.chop(1);
            }
            const int dot = part.indexOf(QLatin1Char('.'));
            const QString base =
                (dot < 0 ? part : part.left(dot)).toUpper();
            if (kFixed.contains(base))
                return true;
            if (base.size() == 4
                    && (base.startsWith(QLatin1String("COM"))
                        || base.startsWith(QLatin1String("LPT")))
                    && base.at(3) >= QLatin1Char('1')
                    && base.at(3) <= QLatin1Char('9')) {
                return true;
            }
        }
#else
        Q_UNUSED(path);
#endif
        return false;
    }

    // Resolve the real location of `path` for policy matching.
    //
    // docs/22 F4: links only live in directories that already exist, so the
    // resolver finds the deepest EXISTING component, resolves it to its
    // final location (Windows: GetFinalPathNameByHandle, which traverses
    // junctions/symlinks — Qt's canonicalFilePath() does not; POSIX:
    // canonicalFilePath, which does), then re-joins the not-yet-existing
    // tail lexically (".." is already collapsed by the absolute path).
    // This keeps the core invariant "validated path == actually touched
    // path" on both the write path (target usually absent) and the read
    // path (existing targets behind a planted junction).
    static QString cleanPath(const QString& path) {
        const QFileInfo info(path);
        const QString absolute = QDir::cleanPath(info.absoluteFilePath());
#ifdef Q_OS_WIN
        QDir dir(absolute);
        QStringList tail;   // not-yet-existing segments below the anchor
        while (!dir.isRoot()) {
            const QString resolved = sandboxNativeFinalPath(dir.absolutePath());
            if (!resolved.isEmpty()) {
                const QString joined = tail.isEmpty()
                    ? resolved
                    : resolved + QLatin1Char('/')
                        + tail.join(QLatin1Char('/'));
                return QDir::toNativeSeparators(QDir::cleanPath(joined));
            }
            tail.prepend(dir.dirName());
            dir.setPath(QDir::cleanPath(
                dir.absoluteFilePath(QStringLiteral(".."))));
        }
        return QDir::toNativeSeparators(absolute);
#else
        const QString canonical = info.canonicalFilePath();
        if (!canonical.isEmpty())
            return QDir::toNativeSeparators(canonical);
        QDir dir(absolute);
        QStringList tail;
        while (!dir.isRoot()) {
            const QString parentPath = QDir::cleanPath(
                dir.absoluteFilePath(QStringLiteral("..")));
            const QString parentCanon =
                QFileInfo(parentPath).canonicalFilePath();
            if (!parentCanon.isEmpty()) {
                // dir itself does not exist (its own canonicalFilePath()
                // came back empty), so dir.dirName() is the first missing
                // segment; tail holds the ones below it.
                QStringList segments;
                segments.append(dir.dirName());
                segments.append(tail);
                const QString joined = parentCanon + QLatin1Char('/')
                    + segments.join(QLatin1Char('/'));
                return QDir::toNativeSeparators(QDir::cleanPath(joined));
            }
            tail.prepend(dir.dirName());
            dir.setPath(parentPath);
        }
        return QDir::toNativeSeparators(absolute);
#endif
    }

    // True when path equals base or lies underneath it. Prefix matching
    // follows the platform filesystem's case convention — the same split
    // GitService/SkillRegistry/ProjectFileIndex already apply (docs/49):
    // Windows is insensitive, case-sensitive filesystems must not treat
    // /home/u/Documents and /home/u/documents as the same directory.
    static bool isWithin(const QString& path, const QString& base) {
        if (base.isEmpty()) return false;
#ifdef Q_OS_WIN
        const Qt::CaseSensitivity pathCase = Qt::CaseInsensitive;
#else
        const Qt::CaseSensitivity pathCase = Qt::CaseSensitive;
#endif
        if (!path.startsWith(base, pathCase)) return false;
        if (path.size() == base.size()) return true;
        const QChar next = path.at(base.size());
        return next == '/' || next == '\\'
            || base.endsWith('/') || base.endsWith('\\');
    }

    inline static ReadPolicy s_readPolicy{ReadPolicy::UserDirectories};
    inline static QString s_projectDir;
    inline static bool s_projectWrite{false};   // docs/17 P1-4: 项目目录写入放行
    inline static QStringList s_userDenyList;
    // docs/24 H8: 上述四个策略静态量由 GUI 线程写、池线程（工具执行/
    // RAG 导入）读——递归读写锁同步（validatePath 持读锁后内部辅助
    // 再入读锁）。
    inline static QReadWriteLock s_policyLock{QReadWriteLock::RecursionMode::Recursive};
    // P1-5: 每工具执行线程的快照槽（ExecutionScope 写入/清除）。
    inline static QThreadStorage<QString> s_execProjectDir;
};

} // namespace NexusAI
