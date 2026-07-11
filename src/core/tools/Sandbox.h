#pragma once

#include "ITool.h"

#include <QString>
#include <QStringList>
#include <QDir>
#include <QStandardPaths>

namespace NexusAI {

// Sandbox security validator - enforces path and tool-level restrictions
class Sandbox {
public:
    enum class AccessMode {
        Read,
        Write
    };

    // Validate that a file path is within allowed directories
    static bool validatePath(const QString& path, bool isWrite) {
        if (path.isEmpty()) return false;

        // Block dangerous system paths
        static const QStringList blockedPaths = {
            "/etc", "/usr", "/bin", "/sbin", "/boot", "/sys", "/proc",
            "C:\\Windows\\System32", "C:\\Windows\\System",
            "C:\\Program Files", "C:\\Program Files (x86)"
        };

        QString normalizedPath = QDir::toNativeSeparators(path);
        for (const auto& blocked : blockedPaths) {
            if (normalizedPath.startsWith(blocked, Qt::CaseInsensitive)) {
                return false;
            }
        }

        // For writes, only allow user-accessible directories
        if (isWrite) {
            static const QStringList allowedWriteDirs = []() {
                QStringList dirs;
                dirs << QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                     << QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
                     << QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
                     << QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
                     << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
                return dirs;
            }();

            for (const auto& allowed : allowedWriteDirs) {
                if (!allowed.isEmpty() && normalizedPath.startsWith(
                        QDir::toNativeSeparators(allowed), Qt::CaseInsensitive)) {
                    return true;
                }
            }
            return false;
        }

        return true;
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
};

} // namespace NexusAI
