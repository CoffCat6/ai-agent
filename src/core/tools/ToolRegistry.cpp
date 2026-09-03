#include "ToolRegistry.h"
#include "Sandbox.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>

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

        if (!proc.waitForFinished(timeoutMs)) {
            proc.kill();
            proc.waitForFinished(2000);
            return ToolResult::fail(QStringLiteral("Command timed out after %1 ms").arg(timeoutMs));
        }

        const QString output = QString::fromUtf8(proc.readAllStandardOutput());
        const int exitCode = proc.exitCode();
        return ToolResult::ok(QStringLiteral("exit code: %1\n%2")
            .arg(exitCode).arg(truncateOutput(output, kMaxOutputChars)));
    }
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
}

void ToolRegistry::registerBuiltins() {
    registerTool(std::make_unique<FileReadTool>());
    registerTool(std::make_unique<ListDirTool>());
    registerTool(std::make_unique<FileWriteTool>());
    registerTool(std::make_unique<ShellExecTool>());
    registerTool(std::make_unique<RmRfTool>());
}

void ToolRegistry::registerTool(std::unique_ptr<ITool> tool) {
    if (!tool) return;
    QString name = tool->definition().name;
    m_tools.insert(name, tool.get());
    m_ownedTools.push_back(std::move(tool));
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
        if (tool->definition().level != ToolLevel::Dangerous)
            names.append(tool->definition().name);
    }
    return names;
}

} // namespace NexusAI
