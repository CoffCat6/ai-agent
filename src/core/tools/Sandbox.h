#pragma once

#include "ITool.h"

#include <QString>
#include <QStringList>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace NexusAI {

// Sandbox security validator - enforces path and tool-level restrictions
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

    // Validate that a file path is within allowed directories.
    // The path is canonicalized before matching, so ".." segments, relative
    // paths and (for existing files) symlinks cannot be used to escape the
    // policy. Prefix matches are boundary-aware: an allowed base directory
    // does not authorize a sibling such as "Documents2".
    static bool validatePath(const QString& path, bool isWrite) {
        if (path.isEmpty()) return false;

        const QString normalizedPath = cleanPath(path);

        // Block dangerous system paths (works for both Unix and Windows)
        static const QStringList blockedPaths = {
            "/etc", "/usr", "/bin", "/sbin", "/boot", "/sys", "/proc",
            "C:\\Windows", "C:\\Program Files", "C:\\Program Files (x86)"
        };
        for (const auto& blocked : blockedPaths) {
            if (isWithin(normalizedPath, QDir::toNativeSeparators(blocked))) {
                return false;
            }
        }

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

        if (isWrite) {
            // Writes additionally allow the whole home directory (users may
            // create files anywhere they own them).
            QStringList writeDirs = readDirs;
            writeDirs << QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
            return withinUserDir(normalizedPath, writeDirs);
        }

        // Reads: user allowlist by default; the FullDisk opt-in restores the
        // legacy behavior (system directories stay blocked either way).
        if (s_readPolicy == ReadPolicy::FullDisk) return true;
        return withinUserDir(normalizedPath, readDirs);
    }

    // Validate a tool call against sandbox policy
    static bool validateToolCall(const ToolDefinition& toolDef, const QJsonObject& args) {
        // Dangerous tools are always blocked
        if (toolDef.level == ToolLevel::Dangerous) {
            return false;
        }

        // For file tools, validate paths
        if (toolDef.name == "read_file" || toolDef.name == "write_file") {
            QString path = args.value("path").toString();
            bool isWrite = (toolDef.name == "write_file");
            return validatePath(path, isWrite);
        }

        return true;
    }

private:
    Sandbox() = delete; // Static utility class

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
};

} // namespace NexusAI
