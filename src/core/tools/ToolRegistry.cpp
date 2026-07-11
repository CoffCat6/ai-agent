#include "ToolRegistry.h"
#include "Sandbox.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>

namespace NexusAI {

// ── Built-in File Read Tool ───────────────────────────────
class FileReadTool : public ITool {
public:
    ToolDefinition definition() const override {
        ToolDefinition def;
        def.name = "read_file";
        def.description = "Read the contents of a file from the local filesystem.";
        def.level = ToolLevel::Safe;
        QJsonObject params;
        params["type"] = "object";
        QJsonObject pathProp;
        pathProp["type"] = "string";
        pathProp["description"] = "Absolute path to the file to read.";
        QJsonObject props;
        props["path"] = pathProp;
        params["properties"] = props;
        QJsonArray required;
        required.append("path");
        params["required"] = required;
        def.parameters = params;
        return def;
    }

    ToolResult execute(const QJsonObject& args) override {
        QString path = args.value("path").toString();
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
        QString content = stream.readAll();
        file.close();

        return ToolResult::ok(content);
    }
};

// ── Built-in File Write Tool ──────────────────────────────
class FileWriteTool : public ITool {
public:
    ToolDefinition definition() const override {
        ToolDefinition def;
        def.name = "write_file";
        def.description = "Write content to a file on the local filesystem.";
        def.level = ToolLevel::RequiresConfirm;
        QJsonObject params;
        params["type"] = "object";
        QJsonObject pathProp, contentProp;
        pathProp["type"] = "string";
        pathProp["description"] = "Absolute path to the file to write.";
        contentProp["type"] = "string";
        contentProp["description"] = "Content to write to the file.";
        QJsonObject props;
        props["path"] = pathProp;
        props["content"] = contentProp;
        params["properties"] = props;
        QJsonArray required;
        required.append("path");
        required.append("content");
        params["required"] = required;
        def.parameters = params;
        return def;
    }

    ToolResult execute(const QJsonObject& args) override {
        QString path = args.value("path").toString();
        QString content = args.value("content").toString();

        if (path.isEmpty())
            return ToolResult::fail("Path is required");

        if (!Sandbox::validatePath(path, true))
            return ToolResult::fail("Path blocked by sandbox policy");

        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return ToolResult::fail("Cannot open file for writing: " + file.errorString());

        QTextStream stream(&file);
        stream << content;
        stream.flush();
        file.close();

        return ToolResult::ok("File written successfully: " + path);
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
    registerTool(std::make_unique<FileWriteTool>());
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

} // namespace NexusAI
