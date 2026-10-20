#include "ToolRegistry.h"
#include "Sandbox.h"
#include "core/git/GitService.h"
#include "core/utils/SettingsRepository.h"
#include "core/utils/SettingsSchema.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>
#include <QDir>
#include <QDirIterator>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>

// ── P3: unified diff generator for file-mutating tool previews ──
// Common-prefix/suffix line diff — O(n) and sufficient for the confirmation
// dialog (files are already size-capped by read/edit bounds). Returns empty
// when there are no changes.
static QString unifiedDiff(const QString& oldText, const QString& newText,
                           const QString& oldLabel, const QString& newLabel,
                           int context = 3) {
    // Empty content is zero lines, not one empty line: otherwise a
    // new-file diff invents a spurious "-" line.
    const QStringList oldLines = oldText.isEmpty()
        ? QStringList() : oldText.split('\n', Qt::KeepEmptyParts);
    const QStringList newLines = newText.isEmpty()
        ? QStringList() : newText.split('\n', Qt::KeepEmptyParts);

    int p = 0;
    const int minLen = qMin(oldLines.size(), newLines.size());
    while (p < minLen && oldLines[p] == newLines[p]) ++p;

    int s = 0;
    while (s < minLen - p &&
           oldLines[oldLines.size() - 1 - s] == newLines[newLines.size() - 1 - s]) ++s;

    const int oldStart = p;
    const int oldEnd = oldLines.size() - s;
    const int newStart = p;
    const int newEnd = newLines.size() - s;

    if (oldStart == oldEnd && newStart == newEnd) return {};

    const int ctxBefore = qMin(context, oldStart);
    const int ctxAfter = qMin(context, static_cast<int>(oldLines.size()) - oldEnd);
    const int hunkOldStart = oldStart - ctxBefore;
    const int hunkNewStart = newStart - ctxBefore;
    const int hunkOldLen = ctxBefore + (oldEnd - oldStart) + ctxAfter;
    const int hunkNewLen = ctxBefore + (newEnd - newStart) + ctxAfter;

    QString diff;
    diff += QStringLiteral("--- %1\n").arg(oldLabel);
    diff += QStringLiteral("+++ %1\n").arg(newLabel);
    diff += QStringLiteral("@@ -%1,%2 +%3,%4 @@\n")
        .arg(hunkOldStart + 1).arg(hunkOldLen)
        .arg(hunkNewStart + 1).arg(hunkNewLen);

    for (int i = hunkOldStart; i < oldStart; ++i)
        diff += QStringLiteral(" %1\n").arg(oldLines[i]);
    for (int i = oldStart; i < oldEnd; ++i)
        diff += QStringLiteral("-%1\n").arg(oldLines[i]);
    for (int i = newStart; i < newEnd; ++i)
        diff += QStringLiteral("+%1\n").arg(newLines[i]);
    for (int i = oldEnd; i < oldEnd + ctxAfter; ++i)
        diff += QStringLiteral(" %1\n").arg(oldLines[i]);

    return diff;
}

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
// docs/24 H3: pre-read refusal bound for read_file (bytes, checked before
// readAll — the character cap only applies to the model-facing output).
constexpr qint64 kMaxReadFileBytes = 8 * 1024 * 1024;
// G4: per-file capture cap for undo. Larger files are recorded as path-only
// (truncated) instead of bloating the session checkpoint budget.
constexpr qint64 kMaxCheckpointCaptureBytes = 1024 * 1024;

// Phase 1: per-checkpoint diff bounds. Inputs beyond this are marked
// unavailable rather than producing a partial, misleading patch.
constexpr qint64 kMaxCheckpointDiffInputBytes = 512 * 1024;
constexpr int kMaxCheckpointDiffChars = 64 * 1024;

// Must run before the mutating write. Unreadable/oversized originals degrade
// to a path-only checkpoint rather than blocking the tool.
static FileCheckpoint captureFileCheckpoint(const QString& path) {
    FileCheckpoint checkpoint;
    checkpoint.path = path;
    const QFileInfo info(path);
    checkpoint.existed = info.exists();
    if (!checkpoint.existed) return checkpoint;

    if (!info.isFile() || info.size() > kMaxCheckpointCaptureBytes) {
        checkpoint.truncated = true;
        return checkpoint;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        checkpoint.truncated = true;
        return checkpoint;
    }
    checkpoint.beforeContent = file.readAll();
    if (checkpoint.beforeContent.size() > kMaxCheckpointCaptureBytes) {
        checkpoint.beforeContent.clear();
        checkpoint.truncated = true;
    }
    return checkpoint;
}

// Called only after the write committed. The hash is always recorded so the
// UI can tell whether the file was modified again later; the diff is bounded.
static FileCheckpoint finalizeCheckpoint(FileCheckpoint checkpoint,
                                         const QByteArray& afterBytes) {
    checkpoint.afterHash = QString::fromLatin1(
        QCryptographicHash::hash(afterBytes, QCryptographicHash::Sha256).toHex());

    if (checkpoint.truncated) {
        checkpoint.diffTruncated = true;
        return checkpoint;
    }
    const QByteArray beforeBytes =
        checkpoint.existed ? checkpoint.beforeContent : QByteArray();
    if (beforeBytes.size() > kMaxCheckpointDiffInputBytes
            || afterBytes.size() > kMaxCheckpointDiffInputBytes) {
        checkpoint.diffTruncated = true;
        return checkpoint;
    }

    const QString beforeText = checkpoint.existed
        ? QString::fromUtf8(beforeBytes) : QString();
    const QString afterText = QString::fromUtf8(afterBytes);
    if (beforeText.contains(QChar::ReplacementCharacter)
            || afterText.contains(QChar::ReplacementCharacter)) {
        checkpoint.diffTruncated = true;
        return checkpoint;
    }

    QString diff = unifiedDiff(
        beforeText, afterText,
        checkpoint.existed ? checkpoint.path : QStringLiteral("/dev/null"),
        checkpoint.path);
    for (const QString& line : diff.split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String("+++"))
                || line.startsWith(QLatin1String("---")))
            continue;
        if (line.startsWith(QLatin1Char('+'))) ++checkpoint.addedLines;
        else if (line.startsWith(QLatin1Char('-'))) ++checkpoint.removedLines;
    }
    if (diff.size() > kMaxCheckpointDiffChars) {
        int cut = diff.lastIndexOf(QLatin1Char('\n'), kMaxCheckpointDiffChars);
        if (cut <= 0) cut = kMaxCheckpointDiffChars;
        diff = diff.left(cut) + QStringLiteral("\n…[diff truncated]\n");
        checkpoint.diffTruncated = true;
    }
    checkpoint.diff = diff;
    return checkpoint;
}

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
        props["path"] = stringParam("Path to the file. Relative paths are "
            "resolved against the active project root when a project is "
            "active, otherwise against the current working directory.");
        def.parameters = buildParams(props, QJsonArray{"path"});
        return def;
    }

    ToolResult execute(const QJsonObject& args) override {
        // P0-2: 相对路径锚定到项目根（未激活项目时保持 CWD 语义）。
        const QString path = Sandbox::resolvePath(args.value("path").toString());
        if (path.isEmpty())
            return ToolResult::fail("Path is required");

        Sandbox::DenyReason reason;
        if (!Sandbox::validatePath(path, false, &reason))
            return ToolResult::fail(Sandbox::denialHint(reason));

        QFile file(path);
        if (!file.exists())
            return ToolResult::fail("File does not exist: " + path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return ToolResult::fail("Cannot open file: " + file.errorString());

        // docs/24 H3: refuse before reading — read_file is a Safe-level
        // (no-confirmation) tool, and the character cap below only kicks in
        // AFTER the whole file has been read into memory. A prompt-injected
        // model pointing at a multi-gigabyte file must not spike memory.
        if (file.size() > kMaxReadFileBytes) {
            return ToolResult::fail(
                QStringLiteral("File too large to read (%1 bytes; limit %2). "
                               "Read it in parts with other tools if really needed.")
                    .arg(file.size()).arg(kMaxReadFileBytes));
        }

        // Try UTF-8 first (Qt 6 default); if the byte sequence is not valid
        // UTF-8, fall back to the system locale encoding so non-UTF-8 text
        // files (e.g. GBK on zh-CN Windows) are readable instead of showing
        // replacement characters (mojibake).
        const QByteArray raw = file.readAll();
        file.close();
        QString content = QString::fromUtf8(raw);
        if (content.contains(QChar::ReplacementCharacter))
            content = QString::fromLocal8Bit(raw);

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
        def.autoEditSafe = true;   // 会话"自动编辑"模式放行；沙箱写策略仍生效
        QJsonObject props;
        props["path"] = stringParam("Path to the file to write. Relative "
            "paths are resolved against the active project root when a "
            "project is active, otherwise against the current working "
            "directory.");
        props["content"] = stringParam("Content to write to the file.");
        def.parameters = buildParams(props, QJsonArray{"path", "content"});
        return def;
    }

    QString generatePreview(const QJsonObject& args) const override {
        const QString path = Sandbox::resolvePath(args.value("path").toString());
        const QString content = args.value("content").toString();
        if (path.isEmpty()) return {};

        QFile file(path);
        if (!file.exists()) {
            const QStringList lines = content.split('\n', Qt::KeepEmptyParts);
            QString diff = QStringLiteral("--- /dev/null\n+++ %1\n@@ -0,0 +1,%2 @@\n")
                               .arg(path).arg(lines.size());
            for (const auto& line : lines)
                diff += QStringLiteral("+%1\n").arg(line);
            return diff;
        }
        if (!file.open(QIODevice::ReadOnly)) return {};
        const QString oldContent = QString::fromUtf8(file.readAll());
        file.close();
        return unifiedDiff(oldContent, content, path, path);
    }

    ToolResult execute(const QJsonObject& args) override {
        const QString path = Sandbox::resolvePath(args.value("path").toString());
        const QString content = args.value("content").toString();

        if (path.isEmpty())
            return ToolResult::fail("Path is required");

        Sandbox::DenyReason reason;
        if (!Sandbox::validatePath(path, true, &reason))
            return ToolResult::fail(Sandbox::denialHint(reason));

        // Ensure parent directory exists
        QDir().mkpath(QFileInfo(path).absolutePath());

        // docs/23 G4: atomic write with explicit error reporting, mirroring
        // edit_file (Batch 21 A2) — a plain stream write used to swallow
        // short-write/close errors and report success on a full disk.
        // No QIODevice::Text: bytes in, bytes out, same line-ending
        // semantics as edit_file (which matches old_string byte-exactly),
        // so the two tools cannot produce mixed-line-ending files.
        // A3: size cap mirrors read_file's kMaxReadFileBytes — a model
        // must not be able to dump unbounded content onto disk.
        constexpr qint64 kMaxWriteFileBytes = 8 * 1024 * 1024;
        if (content.size() > kMaxWriteFileBytes)
            return ToolResult::fail(QStringLiteral(
                "Content too large to write (%1 chars; limit %2).")
                .arg(content.size()).arg(kMaxWriteFileBytes));

        const QByteArray bytes = content.toUtf8();
        // G4: snapshot before QSaveFile replaces the target. On a fresh file
        // this records existed=false, so undo removes the file again.
        FileCheckpoint checkpoint = captureFileCheckpoint(path);
        QSaveFile out(path);
        if (!out.open(QIODevice::WriteOnly))
            return ToolResult::fail("Cannot open file for writing: " + out.errorString());
        if (out.write(bytes) != bytes.size()) {
            out.cancelWriting();
            return ToolResult::fail("Short write (disk full/quota?): " + out.errorString());
        }
        if (!out.commit())
            return ToolResult::fail("Failed to commit atomic write: " + out.errorString());

        ToolResult result = ToolResult::ok(
            QStringLiteral("File written successfully: %1 (%2 bytes)")
                .arg(path).arg(bytes.size()));
        result.checkpoint = finalizeCheckpoint(checkpoint, bytes);
        return result;
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
        def.description = "Replace an exact occurrence of old_string in "
                          "an existing file with new_string. By default the "
                          "match must be unique; set replace_all=true to "
                          "replace every occurrence. Prefer this over "
                          "write_file when only part of an existing file changes.";
        def.level = ToolLevel::RequiresConfirm;
        def.autoEditSafe = true;   // 会话"自动编辑"模式放行；沙箱写策略仍生效
        QJsonObject props;
        props["path"] = stringParam("Path to the file to edit. Relative "
            "paths are resolved against the active project root when a "
            "project is active, otherwise against the current working "
            "directory.");
        props["old_string"] = stringParam("Exact text to replace.");
        props["new_string"] = stringParam("Replacement text (may be empty to delete text).");
        QJsonObject replaceAllParam;
        replaceAllParam["type"] = "boolean";
        replaceAllParam["description"] = "If true, replace all occurrences of old_string. "
                                         "Default is false (requires a unique match).";
        props["replace_all"] = replaceAllParam;
        def.parameters = buildParams(props, QJsonArray{"path", "old_string", "new_string"});
        return def;
    }

    QString generatePreview(const QJsonObject& args) const override {
        const QString path = Sandbox::resolvePath(args.value("path").toString());
        const QString oldString = args.value("old_string").toString();
        const QString newString = args.value("new_string").toString();
        const bool replaceAll = args.value("replace_all").toBool();
        if (path.isEmpty() || oldString.isEmpty()) return {};

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        const QString content = QString::fromUtf8(file.readAll());
        file.close();

        if (replaceAll) {
            if (!content.contains(oldString)) return {};
            QString updated = content;
            updated.replace(oldString, newString);
            return unifiedDiff(content, updated, path, path);
        }

        const int pos = content.indexOf(oldString);
        if (pos < 0) return {};

        QString updated = content;
        updated.replace(pos, oldString.length(), newString);
        return unifiedDiff(content, updated, path, path);
    }

    ToolResult execute(const QJsonObject& args) override {
        const QString path = Sandbox::resolvePath(args.value("path").toString());
        const QString oldString = args.value("old_string").toString();
        const QString newString = args.value("new_string").toString();
        const bool replaceAll = args.value("replace_all").toBool();

        if (path.isEmpty())
            return ToolResult::fail("Path is required");
        if (oldString.isEmpty())
            return ToolResult::fail("old_string is required (use write_file to create new content)");

        Sandbox::DenyReason reason;
        if (!Sandbox::validatePath(path, true, &reason))
            return ToolResult::fail(Sandbox::denialHint(reason));

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return ToolResult::fail("Cannot open file for reading: " + file.errorString());
        // B1: same pre-read refusal bound as read_file — editing a
        // multi-gigabyte file loads it entirely into memory for the
        // search/replace pass.
        if (file.size() > kMaxReadFileBytes)
            return ToolResult::fail(QStringLiteral(
                "File too large to edit (%1 bytes; limit %2).")
                .arg(file.size()).arg(kMaxReadFileBytes));
        const QString content = QString::fromUtf8(file.readAll());
        file.close();
        if (content.contains(QChar::ReplacementCharacter))
            return ToolResult::fail("File is not valid UTF-8 text");

        int count = 0;
        int firstPos = -1;
        for (int at = content.indexOf(oldString); at >= 0;
             at = content.indexOf(oldString, at + oldString.length())) {
            if (count == 0) firstPos = at;
            ++count;
        }
        if (count == 0)
            return ToolResult::fail("old_string not found");
        if (!replaceAll && count > 1)
            return ToolResult::fail(QStringLiteral("old_string matches %1 locations, need unique "
                                                     "(set replace_all=true to replace all)")
                                        .arg(count));

        QString updated = content;
        if (replaceAll)
            updated.replace(oldString, newString);
        else
            updated.replace(firstPos, oldString.length(), newString);

        // Raw (non-Text) mode: bytes in, bytes out - no newline translation,
        // so the untouched parts of the file survive byte-for-byte.
        // Batch 21 (A2): atomic write. A plain open(WriteOnly|Truncate) had
        // already truncated the original by the time a short write or close
        // error surfaced; edit_file targets an EXISTING file, so corruption is
        // costlier than write_file. QSaveFile stages in a temp file and only
        // renames over the target on commit() (same pattern as
        // AgentViewModel::exportSession).
        const QByteArray bytes = updated.toUtf8();
        // G4: `content` is the exact pre-edit text already read above.
        FileCheckpoint checkpoint;
        checkpoint.path = path;
        checkpoint.existed = true;
        const QByteArray beforeBytes = content.toUtf8();
        if (beforeBytes.size() > kMaxCheckpointCaptureBytes) {
            checkpoint.truncated = true;
        } else {
            checkpoint.beforeContent = beforeBytes;
        }
        QSaveFile out(path);
        if (!out.open(QIODevice::WriteOnly))
            return ToolResult::fail("Cannot open file for writing: " + out.errorString());
        if (out.write(bytes) != bytes.size()) {
            out.cancelWriting();
            return ToolResult::fail("Short write (disk full/quota?): " + out.errorString());
        }
        if (!out.commit())
            return ToolResult::fail("Failed to commit atomic write: " + out.errorString());

        ToolResult result = ToolResult::ok(
            QStringLiteral("Replaced %1 occurrence%2; file now %3 bytes")
                .arg(count).arg(count == 1 ? QString() : QStringLiteral("s"))
                .arg(bytes.size()));
        result.checkpoint = finalizeCheckpoint(checkpoint, bytes);
        return result;
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
        props["path"] = stringParam("Path to the directory to list. Relative "
            "paths are resolved against the active project root when a "
            "project is active, otherwise against the current working "
            "directory.");
        def.parameters = buildParams(props, QJsonArray{"path"});
        return def;
    }

    ToolResult execute(const QJsonObject& args) override {
        const QString path = Sandbox::resolvePath(args.value("path").toString());
        if (path.isEmpty())
            return ToolResult::fail("Path is required");

        Sandbox::DenyReason reason;
        if (!Sandbox::validatePath(path, false, &reason))
            return ToolResult::fail(Sandbox::denialHint(reason));

        QDir dir(path);
        if (!dir.exists())
            return ToolResult::fail("Directory does not exist: " + path);

        // B2: QDirIterator reads incrementally — entryInfoList() would
        // load every QFileInfo into memory before the 200-entry display
        // cap, OOM on million-file directories.
        QStringList lines;
        const int maxEntries = 200;
        int count = 0;
        QDirIterator it(path, QDir::AllEntries | QDir::NoDotAndDotDot);
        while (it.hasNext()) {
            it.next();
            const QFileInfo info = it.fileInfo();
            if (count++ >= maxEntries) {
                lines << QStringLiteral("... (more entries, display limit %1)")
                             .arg(maxEntries);
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
                          "its combined stdout/stderr and exit code. Avoid nested "
                          "quotes (e.g. python -c \"...\") - they break on Windows; "
                          "prefer dedicated tools (http_fetch for web requests) "
                          "instead of reimplementing them via shell.";
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
        return execute(args, {});
    }

    ToolResult execute(const QJsonObject& args, const Cancellation& cancelled) override {
        const auto isCancelled = [&]() {
            return cancelled ? cancelled->load() : m_cancelRequested.load();
        };
        if (isCancelled()) return ToolResult::fail("Command cancelled");

        const QString command = args.value("command").toString();
        if (command.isEmpty())
            return ToolResult::fail("Command is required");

        int timeoutMs = args.value("timeout_ms").toInt(10000);
        timeoutMs = qBound(1000, timeoutMs, 60000);

        QProcess proc;
        proc.setProcessChannelMode(QProcess::MergedChannels);
        // docs/24 H7: the child inherits the parent environment by default,
        // and the model could read credential-shaped variables into the
        // tool output (`set` / `env`) — i.e. exfiltrate keys into the
        // context. Strip them before execution.
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        const QStringList envKeys = env.keys();
        for (const QString& key : envKeys) {
            const QString upper = key.toUpper();
            if (upper.contains(QLatin1String("API_KEY"))
                    || upper.contains(QLatin1String("APIKEY"))
                    || upper.contains(QLatin1String("TOKEN"))
                    || upper.contains(QLatin1String("SECRET"))
                    || upper.contains(QLatin1String("PASSWORD"))) {
                env.remove(key);
            }
        }
        proc.setProcessEnvironment(env);
        // P0-2: 项目激活时命令在项目根执行——"dir" "type" 等才落在用户
        // 语境中的目录上，而不是应用启动目录。
        const QString projectRoot = Sandbox::currentProjectDir();
        if (!projectRoot.isEmpty())
            proc.setWorkingDirectory(projectRoot);
#ifdef Q_OS_WIN
        // cmd.exe uses its own quoting rules, not the C-runtime argv rules
        // applied by QProcess to a QStringList. Preserve quoted paths.
        proc.setProgram(QStringLiteral("cmd.exe"));
        proc.setNativeArguments(QStringLiteral("/d /s /c \"") + command + QLatin1Char('"'));
        proc.start();
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
        // A2: drain output incrementally to bound memory — a chatty process
        // (e.g. `yes`) must not grow QProcess's internal buffer without limit.
        constexpr qint64 kMaxOutputBytes = 8 * 1024 * 1024;
        QByteArray accumulated;
        while (!proc.waitForFinished(kSliceMs)) {
            accumulated += proc.readAllStandardOutput();
            if (accumulated.size() > kMaxOutputBytes) {
#ifdef Q_OS_WIN
                if (proc.processId() > 0) {
                    qint64 killExit = QProcess::execute(
                        QStringLiteral("taskkill"),
                        {QStringLiteral("/PID"),
                         QString::number(proc.processId()),
                         QStringLiteral("/T"), QStringLiteral("/F")});
                    if (killExit != 0)
                        qWarning() << "shell_exec overflow: taskkill exit" << killExit;
                }
#endif
                proc.kill();
                proc.waitForFinished(2000);
                return ToolResult::fail(QStringLiteral(
                    "Command output exceeded %1 bytes limit").arg(kMaxOutputBytes));
            }
            if (isCancelled()) {
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
#ifdef Q_OS_WIN
                // A1: same tree-kill as cancel — proc.kill() alone orphans
                // the grandchild under cmd /c.
                if (proc.processId() > 0) {
                    qint64 killExit = QProcess::execute(
                        QStringLiteral("taskkill"),
                        {QStringLiteral("/PID"),
                         QString::number(proc.processId()),
                         QStringLiteral("/T"), QStringLiteral("/F")});
                    if (killExit != 0)
                        qWarning() << "shell_exec timeout: taskkill exit" << killExit;
                }
#endif
                proc.kill();
                proc.waitForFinished(2000);
                return ToolResult::fail(QStringLiteral("Command timed out after %1 ms").arg(timeoutMs));
            }
        }
        accumulated += proc.readAllStandardOutput();

        // Try UTF-8 first; if the byte sequence is not valid UTF-8, fall
        // back to the system locale encoding so non-UTF-8 output (e.g.
        // cmd.exe CP936/GBK on zh-CN Windows) is readable instead of
        // showing replacement characters (mojibake).
        const QByteArray rawOutput = accumulated;
        QString output = QString::fromUtf8(rawOutput);
        if (output.contains(QChar::ReplacementCharacter))
            output = QString::fromLocal8Bit(rawOutput);
        const int exitCode = proc.exitCode();
        return ToolResult::ok(QStringLiteral("exit code: %1\n%2")
            .arg(exitCode).arg(truncateOutput(output, kMaxOutputChars)));
    }

    // Batch 13 (SEC-1303): GUI thread only - flips the atomic flag the
    // slicing execute() polls between wait slices; never touches the
    // QProcess itself (it lives on the pool thread running execute).
    void cancel() override { m_cancelRequested.store(true); }
    void cancel(const Cancellation& cancelled) override {
        if (cancelled) cancelled->store(true);
        else cancel();
    }

private:
    std::atomic<bool> m_cancelRequested{false};
};

// ── Built-in Glob Tool (Safe) ───────────────────────────
// Fast file-path pattern search — lets the model discover files by name
// without constructing shell commands (cross-platform, no findstr/grep).
class GlobTool : public ITool {
public:
    ToolDefinition definition() const override {
        ToolDefinition def;
        def.name = "glob";
        def.description = "Find files matching a glob pattern. Returns matching "
                          "file paths relative to the search root. Use ** for "
                          "recursive search (e.g. **/*.cpp).";
        def.level = ToolLevel::Safe;
        QJsonObject props;
        props["pattern"] = stringParam("Glob pattern (e.g. **/*.cpp, src/**/*.h, "
            "*.txt). ** triggers recursive search.");
        props["path"] = stringParam("Root directory to search from. Relative "
            "paths resolve against the active project root when a project is "
            "active, otherwise against the current working directory. "
            "Defaults to the project root or CWD.");
        def.parameters = buildParams(props, QJsonArray{"pattern"});
        return def;
    }

    ToolResult execute(const QJsonObject& args) override {
        const QString pattern = args.value("pattern").toString();
        if (pattern.isEmpty())
            return ToolResult::fail("Pattern is required");

        QString searchPath = args.value("path").toString();
        if (searchPath.isEmpty())
            searchPath = Sandbox::currentProjectDir().isEmpty()
                ? QDir::currentPath()
                : Sandbox::currentProjectDir();
        else
            searchPath = Sandbox::resolvePath(searchPath);

        if (searchPath.isEmpty())
            return ToolResult::fail("Path is required");

        Sandbox::DenyReason reason;
        if (!Sandbox::validatePath(searchPath, false, &reason))
            return ToolResult::fail(Sandbox::denialHint(reason));

        // Parse pattern: extract subdirectory, recursive flag, and name filter.
        QString subPath;
        QString nameFilter = QStringLiteral("*");
        bool recursive = false;
        const QStringList segments = pattern.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        for (int i = 0; i < segments.size(); ++i) {
            if (segments[i] == QLatin1String("**")) {
                recursive = true;
                if (i + 1 < segments.size())
                    nameFilter = segments[i + 1];
                break;
            }
            if (i == segments.size() - 1) {
                nameFilter = segments[i];
            } else {
                if (!subPath.isEmpty()) subPath += QLatin1Char('/');
                subPath += segments[i];
            }
        }

        QString fullSearchPath = searchPath;
        if (!subPath.isEmpty())
            fullSearchPath = QDir(searchPath).absoluteFilePath(subPath);

        Sandbox::DenyReason subReason;
        if (!Sandbox::validatePath(fullSearchPath, false, &subReason))
            return ToolResult::fail(Sandbox::denialHint(subReason));

        QDir dir(fullSearchPath);
        if (!dir.exists())
            return ToolResult::fail("Directory does not exist: " + fullSearchPath);

        QStringList lines;
        constexpr int maxResults = 200;
        int count = 0;
        QDirIterator it(fullSearchPath, QStringList{nameFilter},
            QDir::Files | QDir::NoDotAndDotDot,
            recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
        while (it.hasNext()) {
            const QString filePath = it.next();
            if (count++ >= maxResults) {
                lines << QStringLiteral("... (more results, limit %1)").arg(maxResults);
                break;
            }
            lines << QDir(searchPath).relativeFilePath(filePath);
        }

        if (lines.isEmpty())
            return ToolResult::ok("No files matched.");
        return ToolResult::ok(lines.join('\n'));
    }
};

// ── Built-in Grep Tool (Safe) ───────────────────────────
// Content search across files — lets the model find code by regex without
// shell commands. Returns file:line:match triples, capped to protect context.
class GrepTool : public ITool {
public:
    ToolDefinition definition() const override {
        ToolDefinition def;
        def.name = "grep";
        def.description = "Search file contents using a regular expression. "
                          "Returns matching lines with file paths and line "
                          "numbers. Searches recursively from the given path.";
        def.level = ToolLevel::Safe;
        QJsonObject props;
        props["pattern"] = stringParam("Regular expression to search for.");
        props["path"] = stringParam("Root directory to search from. Relative "
            "paths resolve against the active project root when a project is "
            "active, otherwise against the current working directory. "
            "Defaults to the project root or CWD.");
        props["include"] = stringParam("File name filter (e.g. *.cpp, *.h). "
            "Defaults to all files.");
        def.parameters = buildParams(props, QJsonArray{"pattern"});
        return def;
    }

    ToolResult execute(const QJsonObject& args) override {
        const QString pattern = args.value("pattern").toString();
        if (pattern.isEmpty())
            return ToolResult::fail("Pattern is required");

        QString searchPath = args.value("path").toString();
        if (searchPath.isEmpty())
            searchPath = Sandbox::currentProjectDir().isEmpty()
                ? QDir::currentPath()
                : Sandbox::currentProjectDir();
        else
            searchPath = Sandbox::resolvePath(searchPath);

        if (searchPath.isEmpty())
            return ToolResult::fail("Path is required");

        Sandbox::DenyReason reason;
        if (!Sandbox::validatePath(searchPath, false, &reason))
            return ToolResult::fail(Sandbox::denialHint(reason));

        QDir dir(searchPath);
        if (!dir.exists())
            return ToolResult::fail("Directory does not exist: " + searchPath);

        const QString include = args.value("include").toString();
        const QStringList nameFilters = include.isEmpty()
            ? QStringList{QStringLiteral("*")}
            : QStringList{include};

        QRegularExpression regex(pattern);
        if (!regex.isValid())
            return ToolResult::fail(QStringLiteral("Invalid regex: %1").arg(regex.errorString()));

        QStringList lines;
        constexpr int maxMatches = 100;
        constexpr int maxFileBytes = 2 * 1024 * 1024;
        constexpr int maxFiles = 500;
        int matchCount = 0;
        int filesSearched = 0;

        QDirIterator it(searchPath, nameFilters,
            QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext() && matchCount < maxMatches && filesSearched < maxFiles) {
            const QString filePath = it.next();
            ++filesSearched;

            QFileInfo info(filePath);
            if (info.size() > maxFileBytes)
                continue;

            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;

            const QString relativePath = QDir(searchPath).relativeFilePath(filePath);
            int lineNum = 0;
            while (!file.atEnd() && matchCount < maxMatches) {
                const QString line = QString::fromUtf8(file.readLine());
                ++lineNum;
                if (regex.match(line).hasMatch()) {
                    lines << QStringLiteral("%1:%2: %3")
                        .arg(relativePath)
                        .arg(lineNum)
                        .arg(line.trimmed());
                    ++matchCount;
                }
            }
        }

        if (lines.isEmpty())
            return ToolResult::ok("No matches found.");
        if (matchCount >= maxMatches)
            lines << QStringLiteral("... (results truncated at %1 matches)").arg(maxMatches);
        return ToolResult::ok(lines.join('\n'));
    }
};

// ── Built-in Git Tools (P5c) ─────────────────────────────
// git_status and git_diff are Safe (read-only); git_commit is
// RequiresConfirm. All operate in the project root via Sandbox.

struct GitResult { bool ok; QString output; };

static GitResult runGit(const QStringList& args, int timeoutMs = 10000) {
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    const QString projectRoot = Sandbox::currentProjectDir();
    if (!projectRoot.isEmpty())
        proc.setWorkingDirectory(projectRoot);
    proc.start(QStringLiteral("git"), args);
    if (!proc.waitForStarted(3000))
        return {false, QStringLiteral("Failed to start git: %1").arg(proc.errorString())};
    if (!proc.waitForFinished(timeoutMs))
        return {false, QStringLiteral("git timed out after %1 ms").arg(timeoutMs)};
    const QString output = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    if (proc.exitCode() != 0)
        return {false, output.isEmpty() ? QStringLiteral("git exited with code %1").arg(proc.exitCode()) : output};
    return {true, output};
}

class GitStatusTool : public ITool {
public:
    ToolDefinition definition() const override {
        ToolDefinition def;
        def.name = "git_status";
        def.description = "Show the working tree status of the active project's "
                          "git repository. Returns porcelain format with branch info.";
        def.level = ToolLevel::Safe;
        QJsonObject props;
        props["path"] = stringParam("Optional path to limit status to a subdirectory or file.");
        def.parameters = buildParams(props, QJsonArray{});
        return def;
    }

    ToolResult execute(const QJsonObject& args) override {
        const QString path = args.value("path").toString();
        const QString resolved = path.isEmpty() ? QString() : Sandbox::resolvePath(path);
        GitService git(Sandbox::currentProjectDir());
        QString error;
        const QString output = git.statusText(resolved, &error);
        if (output.isNull())
            return ToolResult::fail(error.isEmpty()
                ? QStringLiteral("git status failed") : error);
        return ToolResult::ok(output.isEmpty()
            ? QStringLiteral("No changes (clean working tree).") : output);
    }
};

class GitDiffTool : public ITool {
public:
    ToolDefinition definition() const override {
        ToolDefinition def;
        def.name = "git_diff";
        def.description = "Show unstaged (or staged) changes in the active project's "
                          "git repository as a unified diff.";
        def.level = ToolLevel::Safe;
        QJsonObject props;
        QJsonObject stagedProp;
        stagedProp["type"] = "boolean";
        stagedProp["description"] = "If true, show staged (cached) changes. Default: false (unstaged).";
        props["staged"] = stagedProp;
        props["path"] = stringParam("Optional path to limit diff to a specific file.");
        def.parameters = buildParams(props, QJsonArray{});
        return def;
    }

    ToolResult execute(const QJsonObject& args) override {
        const QString path = args.value("path").toString();
        const QString resolved = path.isEmpty() ? QString() : Sandbox::resolvePath(path);
        GitService git(Sandbox::currentProjectDir());
        bool truncated = false;
        QString error;
        const QString output =
            git.workingDiff(resolved, args.value("staged").toBool(), &truncated, &error);
        if (output.isNull())
            return ToolResult::fail(error.isEmpty()
                ? QStringLiteral("git diff failed") : error);
        QString body = output.isEmpty() ? QStringLiteral("No changes.") : output;
        if (truncated)
            body += QStringLiteral("\n...[diff truncated]");
        return ToolResult::ok(body);
    }
};

class GitCommitTool : public ITool {
public:
    ToolDefinition definition() const override {
        ToolDefinition def;
        def.name = "git_commit";
        def.description = "Create a git commit with the given message in the active "
                          "project. Only staged changes are committed. Use git_diff "
                          "with staged=true to review what will be committed first.";
        def.level = ToolLevel::RequiresConfirm;
        QJsonObject props;
        props["message"] = stringParam("Commit message.");
        def.parameters = buildParams(props, QJsonArray{"message"});
        return def;
    }

    QString generatePreview(const QJsonObject& args) const override {
        const GitResult staged = runGit({QStringLiteral("diff"), QStringLiteral("--staged"), QStringLiteral("--stat")});
        const GitResult status = runGit({QStringLiteral("status"), QStringLiteral("--porcelain=v1")});
        QString preview;
        preview += QStringLiteral("Staged changes:\n%1\n\n").arg(staged.ok && !staged.output.isEmpty() ? staged.output : QStringLiteral("(none)"));
        preview += QStringLiteral("Working tree:\n%1").arg(status.ok && !status.output.isEmpty() ? status.output : QStringLiteral("(clean)"));
        return preview;
    }

    ToolResult execute(const QJsonObject& args) override {
        const QString message = args.value("message").toString().trimmed();
        if (message.isEmpty())
            return ToolResult::fail("Commit message is required");
        const GitResult r = runGit({QStringLiteral("commit"), QStringLiteral("-m"), message});
        return r.ok ? ToolResult::ok(r.output) : ToolResult::fail(r.output);
    }
};

// ── Built-in Todo Write Tool (Safe) ───────────────────────
// P5a: lets the LLM manage a structured task list for multi-step work.
// The result JSON is parsed by AIAgent::finishToolExecution to update
// the agent's m_todoList and notify the UI.
class TodoWriteTool : public ITool {
public:
    ToolDefinition definition() const override {
        ToolDefinition def;
        def.name = "todo_write";
        def.description = "Manage a task list for tracking multi-step work. "
                          "Pass the full list each time (replaces the previous one). "
                          "Each task has content, status (pending/in_progress/completed), "
                          "and priority (high/medium/low).";
        def.level = ToolLevel::Safe;
        QJsonObject todoItem;
        todoItem["type"] = "object";
        QJsonObject itemProps;
        itemProps["content"] = stringParam("Brief description of the task.");
        QJsonObject statusProp;
        statusProp["type"] = "string";
        statusProp["enum"] = QJsonArray{"pending", "in_progress", "completed"};
        statusProp["description"] = "Task status.";
        itemProps["status"] = statusProp;
        QJsonObject priorityProp;
        priorityProp["type"] = "string";
        priorityProp["enum"] = QJsonArray{"high", "medium", "low"};
        priorityProp["description"] = "Priority level (default: medium).";
        itemProps["priority"] = priorityProp;
        todoItem["properties"] = itemProps;
        QJsonArray itemRequired;
        itemRequired.append("content");
        itemRequired.append("status");
        todoItem["required"] = itemRequired;
        QJsonObject todosParam;
        todosParam["type"] = "array";
        todosParam["items"] = todoItem;
        todosParam["description"] = "Complete task list (replaces existing list).";
        QJsonObject props;
        props["todos"] = todosParam;
        def.parameters = buildParams(props, QJsonArray{"todos"});
        return def;
    }

    ToolResult execute(const QJsonObject& args) override {
        const QJsonArray todos = args.value("todos").toArray();
        if (todos.isEmpty())
            return ToolResult::ok(QStringLiteral("[]"));

        static const QSet<QString> validStatuses{
            QStringLiteral("pending"), QStringLiteral("in_progress"),
            QStringLiteral("completed")};
        static const QSet<QString> validPriorities{
            QStringLiteral("high"), QStringLiteral("medium"), QStringLiteral("low")};

        QJsonArray normalized;
        for (int i = 0; i < todos.size(); ++i) {
            const QJsonObject item = todos.at(i).toObject();
            const QString content = item.value("content").toString().trimmed();
            if (content.isEmpty())
                return ToolResult::fail(QStringLiteral(
                    "Task at index %1 has empty content").arg(i));
            const QString status = item.value("status").toString();
            if (!validStatuses.contains(status))
                return ToolResult::fail(QStringLiteral(
                    "Task at index %1 has invalid status '%2' "
                    "(must be pending/in_progress/completed)").arg(i).arg(status));
            QString priority = item.value("priority").toString(QStringLiteral("medium"));
            if (!validPriorities.contains(priority))
                priority = QStringLiteral("medium");
            QJsonObject norm;
            norm["content"] = content;
            norm["status"] = status;
            norm["priority"] = priority;
            normalized.append(norm);
        }

        return ToolResult::ok(QString::fromUtf8(
            QJsonDocument(normalized).toJson(QJsonDocument::Compact)));
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
    registerTool(std::make_unique<GlobTool>());
    registerTool(std::make_unique<GrepTool>());
    registerTool(std::make_unique<GitStatusTool>());
    registerTool(std::make_unique<GitDiffTool>());
    registerTool(std::make_unique<GitCommitTool>());
    registerTool(std::make_unique<TodoWriteTool>());
}

void ToolRegistry::registerTool(std::shared_ptr<ITool> tool) {
    if (!tool) return;
    QString name = tool->definition().name;
    m_tools.insert(name, tool);
    m_ownedTools.push_back(std::move(tool));
    emit toolsChanged();
}

std::shared_ptr<ITool> ToolRegistry::takeTool(const QString& name) {
    for (auto it = m_ownedTools.begin(); it != m_ownedTools.end(); ++it) {
        if ((*it)->definition().name == name) {
            std::shared_ptr<ITool> taken = *it;
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
        return it.value().get();
    return nullptr;
}

std::shared_ptr<ITool> ToolRegistry::findShared(const QString& name) const {
    return m_tools.value(name);
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
        if (tool->definition().level != ToolLevel::Dangerous
                && isToolUsable(tool->definition().name))
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

// docs/17 B1: 策略拦截层。只改内存集合，不落盘——与用户禁用彻底分层。
void ToolRegistry::setPolicyBlocked(const QStringList& names, bool blocked) {
    bool changed = false;
    for (const QString& name : names) {
        if (blocked) {
            if (!m_policyBlocked.contains(name)) {
                m_policyBlocked.insert(name);
                changed = true;
            }
        } else {
            if (m_policyBlocked.remove(name)) changed = true;
        }
    }
    if (changed) emit toolsChanged();
}

bool ToolRegistry::isPolicyBlocked(const QString& name) const {
    return m_policyBlocked.contains(name);
}

bool ToolRegistry::isToolUsable(const QString& name) const {
    return isToolEnabled(name) && !m_policyBlocked.contains(name);
}

void ToolRegistry::loadDisabledFromSettings() {
    if (!SettingsRepository::contains(QStringLiteral("tools/disabled"))) return;
    const QStringList disabled = SettingsRepository::value(
        QStringLiteral("tools/disabled"),
        SettingsSchema::defaultValue(QStringLiteral("tools/disabled"))).toStringList();
    for (const QString& name : disabled)
        if (m_tools.contains(name))
            m_disabledTools.insert(name);
}

void ToolRegistry::saveDisabledToSettings() {
    QString error;
    if (!SettingsRepository::writeValue(
            QStringLiteral("tools/disabled"),
            QStringList(m_disabledTools.begin(), m_disabledTools.end()), &error))
        qWarning() << "Failed to persist disabled tool list:" << error;
}

} // namespace NexusAI
