#include "ToolRegistry.h"
#include "Sandbox.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSettings>

#include <atomic>

namespace NexusAI {

namespace {

// Helper to build a JSON-Schema parameter object
QJsonObject buildParams(const QJsonObject& properties, const QJsonArray& required) {
    QJsonObject params;
    params["type"] = "object";
    params["properties"] = properties;
    params["required"] = required;
    return params;
}

QJsonObject stringParam(const QString& description) {
    QJsonObject p;
    p["type"] = "string";
    p["description"] = description;
    return p;
}

constexpr int kMaxFileChars = 20000;
constexpr int kMaxOutputChars = 8000;

QString truncateOutput(const QString& text, int maxChars) {
    if (text.size() <= maxChars) return text;
    return text.left(maxChars)
        + QStringLiteral("\n...[truncated %1 chars]").arg(text.size() - maxChars);
}

} // namespace

// ── Built-in File Read Tool (Safe) ────────────────────────
class FileReadTool : public ITool {
public:
    ToolDefinition definition() const override {
        ToolDefinition def;
        def.name = "read_file";
        def.description = "Read the contents of a file from the local filesystem.";
        def.level = ToolLevel::Safe;
        QJsonObject props;
        props["path"] = stringParam("Absolute path to the file to read.");
        def.parameters = buildParams(props, QJsonArray{"path"});
        return def;
    }

    ToolResult execute(const QJsonObject& args) override {
        const QString path = args.value("path").toString();
        if (path.isEmpty())
            return ToolResult::fail("Path is required");

        if (!Sandbox::validatePath(path, false))
            return ToolResult::fail("Path blocked by sandbox policy");

        QFile file(path);
        if (!file.exists())
            return ToolResult::fail("File does not exist: " + path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return ToolResult::fail("Cannot open file: " + file.errorString());

        QTextStream stream(&file);
        const QString content = stream.readAll();
        file.close();

        if (content.size() > kMaxFileChars) {
            return ToolResult::ok(truncateOutput(content, kMaxFileChars)
                + "\n(Use other tools if you need more of this file.)");
        }
        return ToolResult::ok(content);
    }
};

// ── Built-in File Write Tool (RequiresConfirm) ────────────
class FileWriteTool : public ITool {
public:
    ToolDefinition definition() const override {
        ToolDefinition def;
        def.name = "write_file";
        def.description = "Write content to a file on the local filesystem. "
                          "Creates the file if it does not exist.";
        def.level = ToolLevel::RequiresConfirm;
        QJsonObject props;
        props["path"] = stringParam("Absolute path to the file to write.");
        props["content"] = stringParam("Content to write to the file.");
        def.parameters = buildParams(props, QJsonArray{"path", "content"});
        return def;
    }

    ToolResult execute(const QJsonObject& args) override {
        const QString path = args.value("path").toString();
        const QString content = args.value("content").toString();

        if (path.isEmpty())
            return ToolResult::fail("Path is required");

        if (!Sandbox::validatePath(path, true))
            return ToolResult::fail("Path blocked by sandbox policy: writes are only "
                                    "allowed in user directories");

        // Ensure parent directory exists
        QDir().mkpath(QFileInfo(path).absolutePath());

        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return ToolResult::fail("Cannot open file for writing: " + file.errorString());

        QTextStream stream(&file);
        stream << content;
        stream.flush();
        file.close();

        return ToolResult::ok(QStringLiteral("File written successfully: %1 (%2 bytes)")
                                  .arg(path).arg(content.toUtf8().size()));
    }
};

// ── Built-in File Edit Tool (RequiresConfirm) ─────────────
// Batch 19 (FEAT-1901): search/replace edit for existing files - safer and
// more precise than a full write_file rewrite. Exact-match semantics: line
// endings are NOT normalized, so an old_string that differs from the file's
// CRLF bytes is reported as not-found (pinned by editFileCrlfBoundary).
class EditFileTool : public ITool {
public:
    ToolDefinition definition() const override {
        ToolDefinition def;
        def.name = "edit_file";
        def.description = "Replace an exact, unique occurrence of old_string in "
                          "an existing file with new_string. Prefer this over "
                          "write_file when only part of an existing file changes.";
        def.level = ToolLevel::RequiresConfirm;
        QJsonObject props;
        props["path"] = stringParam("Absolute path to the file to edit.");
        props["old_string"] = stringParam("Exact text to replace (must occur exactly once).");
        props["new_string"] = stringParam("Replacement text (may be empty to delete text).");
        def.parameters = buildParams(props, QJsonArray{"path", "old_string", "new_string"});
        return def;
    }

    ToolResult execute(const QJsonObject& args) override {
        const QString path = args.value("path").toString();
        const QString oldString = args.value("old_string").toString();
        const QString newString = args.value("new_string").toString();

        if (path.isEmpty())
            return ToolResult::fail("Path is required");
        if (oldString.isEmpty())
            return ToolResult::fail("old_string is required (use write_file to create new content)");

        if (!Sandbox::validatePath(path, true))
            return ToolResult::fail("Path blocked by sandbox policy: writes are only "
                                    "allowed in user directories");

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return ToolResult::fail("Cannot open file for reading: " + file.errorString());
        const QString content = QString::fromUtf8(file.readAll());
        file.close();
        if (content.contains(QChar::ReplacementCharacter))
            return ToolResult::fail("File is not valid UTF-8 text");

        int count = 0;
        int pos = -1;
        for (int at = content.indexOf(oldString); at >= 0;
             at = content.indexOf(oldString, at + oldString.length())) {
            if (count == 0) pos = at;
            ++count;
        }
        if (count == 0)
            return ToolResult::fail("old_string not found");
        if (count > 1)
            return ToolResult::fail(QStringLiteral("old_string matches %1 locations, need unique")
                                        .arg(count));

        QString updated = content;
        updated.replace(pos, oldString.length(), newString);

        // Raw (non-Text) mode: bytes in, bytes out - no newline translation,
        // so the untouched parts of the file survive byte-for-byte.
        // Batch 21 (A2): atomic write. A plain open(WriteOnly|Truncate) had
        // already truncated the original by the time a short write or close
        // error surfaced; edit_file targets an EXISTING file, so corruption is
        // costlier than write_file. QSaveFile stages in a temp file and only
        // renames over the target on commit() (same pattern as
        // AgentViewModel::exportSession).
        const QByteArray bytes = updated.toUtf8();
        QSaveFile out(path);
        if (!out.open(QIODevice::WriteOnly))
            return ToolResult::fail("Cannot open file for writing: " + out.errorString());
        if (out.write(bytes) != bytes.size()) {
            out.cancelWriting();
            return ToolResult::fail("Short write (disk full/quota?): " + out.errorString());
        }
        if (!out.commit())
            return ToolResult::fail("Failed to commit atomic write: " + out.errorString());

        return ToolResult::ok(QStringLiteral("Replaced 1 occurrence at offset %1; file now %2 bytes")
                                  .arg(pos).arg(bytes.size()));
    }
};

// ── Built-in Directory Listing Tool (Safe) ────────────────
class ListDirTool : public ITool {
public:
    ToolDefinition definition() const override {
        ToolDefinition def;
        def.name = "list_dir";
        def.description = "List the entries of a directory on the local filesystem.";
        def.level = ToolLevel::Safe;
        QJsonObject props;
        props["path"] = stringParam("Absolute path to the directory to list.");
        def.parameters = buildParams(props, QJsonArray{"path"});
        return def;
    }

    ToolResult execute(const QJsonObject& args) override {
        const QString path = args.value("path").toString();
        if (path.isEmpty())
            return ToolResult::fail("Path is required");

        if (!Sandbox::validatePath(path, false))
            return ToolResult::fail("Path blocked by sandbox policy");

        QDir dir(path);
        if (!dir.exists())
            return ToolResult::fail("Directory does not exist: " + path);

        const QFileInfoList entries = dir.entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);

        QStringList lines;
        const int maxEntries = 200;
        int count = 0;
        for (const QFileInfo& info : entries) {
            if (count++ >= maxEntries) {
                lines << QStringLiteral("... (%1 more entries)")
                             .arg(entries.size() - maxEntries);
                break;
            }
            lines << (info.isDir()
                ? QStringLiteral("[dir]  %1").arg(info.fileName())
                : QStringLiteral("[file] %1 (%2 bytes)").arg(info.fileName()).arg(info.size()));
        }
        return ToolResult::ok(QStringLiteral("%1:\n%2").arg(path, lines.join('\n')));
    }
};

// ── Built-in Shell Execution Tool (RequiresConfirm) ───────
class ShellExecTool : public ITool {
public:
    ToolDefinition definition() const override {
        ToolDefinition def;
        def.name = "shell_exec";
        def.description = "Execute a shell command on the user's machine and return "
                          "its combined stdout/stderr and exit code.";
        def.level = ToolLevel::RequiresConfirm;
        QJsonObject cmdProp = stringParam("The shell command to execute.");
        QJsonObject timeoutProp;
        timeoutProp["type"] = "integer";
        timeoutProp["description"] = "Timeout in milliseconds (1000-60000, default 10000).";
        QJsonObject props;
        props["command"] = cmdProp;
        props["timeout_ms"] = timeoutProp;
        def.parameters = buildParams(props, QJsonArray{"command"});
        return def;
    }

    ToolResult execute(const QJsonObject& args) override {
        // Batch 21 (B3): drop a cancel left over from a previous call, so a
        // cancel() landing between two executions can't kill the next task.
        m_cancelRequested.store(false);

        const QString command = args.value("command").toString();
        if (command.isEmpty())
            return ToolResult::fail("Command is required");

        int timeoutMs = args.value("timeout_ms").toInt(10000);
        timeoutMs = qBound(1000, timeoutMs, 60000);

        QProcess proc;
        proc.setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_WIN
        proc.start("cmd.exe", {"/c", command});
#else
        proc.start("/bin/sh", {"-c", command});
#endif
        if (!proc.waitForStarted(3000))
            return ToolResult::fail("Failed to start process: " + proc.errorString());

        // Batch 13 (SEC-1303): sliced wait so a GUI-thread cancel() (atomic
        // flag only - this QProcess must never be touched cross-thread) is
        // observed within one slice and torn down HERE, on the pool thread
        // that owns the QProcess.
        constexpr int kSliceMs = 200;
        int waitedMs = 0;
        while (!proc.waitForFinished(kSliceMs)) {
            if (m_cancelRequested.exchange(false)) {
#ifdef Q_OS_WIN
                // cmd.exe /c makes the long runner a grandchild; killing
                // cmd alone orphans it - terminate the tree, then reap.
                // (Two-arg execute: Qt 6 does NOT split a single-string
                // command, and a silent failure here would orphan ping.)
                if (proc.processId() > 0) {
                    qint64 killExit = QProcess::execute(
                        QStringLiteral("taskkill"),
                        {QStringLiteral("/PID"),
                         QString::number(proc.processId()),
                         QStringLiteral("/T"), QStringLiteral("/F")});
                    if (killExit != 0)
                        qWarning() << "shell_exec cancel: taskkill exit" << killExit;
                }
#endif
                proc.kill();
                proc.waitForFinished(2000);
                return ToolResult::fail(QStringLiteral("Command cancelled"));
            }
            waitedMs += kSliceMs;
            if (waitedMs >= timeoutMs) {
                proc.kill();
                proc.waitForFinished(2000);
                return ToolResult::fail(QStringLiteral("Command timed out after %1 ms").arg(timeoutMs));
            }
        }

        const QString output = QString::fromUtf8(proc.readAllStandardOutput());
        const int exitCode = proc.exitCode();
        return ToolResult::ok(QStringLiteral("exit code: %1\n%2")
            .arg(exitCode).arg(truncateOutput(output, kMaxOutputChars)));
    }

    // Batch 13 (SEC-1303): GUI thread only - flips the atomic flag the
    // slicing execute() polls between wait slices; never touches the
    // QProcess itself (it lives on the pool thread running execute).
    void cancel() override { m_cancelRequested.store(true); }

private:
    std::atomic<bool> m_cancelRequested{false};
};

// ── Blocked Dangerous Tool (never exposed to the LLM) ─────
class RmRfTool : public ITool {
public:
    ToolDefinition definition() const override {
        ToolDefinition def;
        def.name = "rm_rf";
        def.description = "Recursively delete files and directories. "
                          "Blocked by the sandbox and never offered to the model.";
        def.level = ToolLevel::Dangerous;
        def.parameters = buildParams(
            QJsonObject{{"path", stringParam("Target path.")}}, QJsonArray{"path"});
        return def;
    }

    ToolResult execute(const QJsonObject&) override {
        return ToolResult::fail("This tool is blocked by the sandbox and can never be executed.");
    }
};

// ── ToolRegistry Implementation ──────────────────────────
ToolRegistry& ToolRegistry::instance() {
    static ToolRegistry registry;
    return registry;
}

ToolRegistry::ToolRegistry() {
    registerBuiltins();
    loadDisabledFromSettings();
}

void ToolRegistry::registerBuiltins() {
    registerTool(std::make_unique<FileReadTool>());
    registerTool(std::make_unique<ListDirTool>());
    registerTool(std::make_unique<FileWriteTool>());
    registerTool(std::make_unique<EditFileTool>());   // Batch 19 (FEAT-1901)
    registerTool(std::make_unique<ShellExecTool>());
    registerTool(std::make_unique<RmRfTool>());
}

void ToolRegistry::registerTool(std::unique_ptr<ITool> tool) {
    if (!tool) return;
    QString name = tool->definition().name;
    m_tools.insert(name, tool.get());
    m_ownedTools.push_back(std::move(tool));
    emit toolsChanged();
}

std::unique_ptr<ITool> ToolRegistry::takeTool(const QString& name) {
    for (auto it = m_ownedTools.begin(); it != m_ownedTools.end(); ++it) {
        if ((*it)->definition().name == name) {
            std::unique_ptr<ITool> taken = std::move(*it);
            m_ownedTools.erase(it);
            m_tools.remove(name);      // keep the name index in sync
            m_disabledTools.remove(name);
            emit toolsChanged();
            return taken;
        }
    }
    return nullptr;
}

ITool* ToolRegistry::find(const QString& name) const {
    auto it = m_tools.find(name);
    if (it != m_tools.end())
        return it.value();
    return nullptr;
}

QVector<ToolDefinition> ToolRegistry::listAll() const {
    QVector<ToolDefinition> defs;
    for (const auto& tool : m_ownedTools) {
        defs.append(tool->definition());
    }
    return defs;
}

bool ToolRegistry::isRegistered(const QString& name) const {
    return m_tools.contains(name);
}

QStringList ToolRegistry::toolNamesForModel() const {
    QStringList names;
    for (const auto& tool : m_ownedTools) {
        if (!m_disabledTools.contains(tool->definition().name))
            names.append(tool->definition().name);
    }
    return names;
}

void ToolRegistry::setToolEnabled(const QString& name, bool enabled) {
    if (enabled) m_disabledTools.remove(name);
    else m_disabledTools.insert(name);
    saveDisabledToSettings();
    emit toolsChanged();
}

bool ToolRegistry::isToolEnabled(const QString& name) const {
    // Batch 14 review fix: "enabled" only means something for a registered
    // tool - an unregistered name must not report enabled. Current call
    // sites all find() first; this keeps the predicate honest if a future
    // caller skips that step.
    return m_tools.contains(name) && !m_disabledTools.contains(name);
}

void ToolRegistry::loadDisabledFromSettings() {
    QSettings s("NexusAI", "NexusAI");
    if (s.contains("tools/disabled")) {
        const QStringList disabled = s.value("tools/disabled").toStringList();
        for (const QString& name : disabled)
            if (m_tools.contains(name))
                m_disabledTools.insert(name);
    } else {
        // First run: dangerous tools default to disabled for safety.
        m_disabledTools.insert(QStringLiteral("rm_rf"));
    }
}

void ToolRegistry::saveDisabledToSettings() {
    QSettings s("NexusAI", "NexusAI");
    s.setValue("tools/disabled", QStringList(m_disabledTools.begin(),
                                             m_disabledTools.end()));
}

} // namespace NexusAI
