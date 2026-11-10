#include "McpConfigTransfer.h"

#include "core/utils/FilePermissions.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

namespace NexusAI {
namespace McpConfigTransfer {

// docs/36 B1-4：定义见文件末尾；前置声明让 validate() 能同步复用同一判定。
QString validateBatchArgs(const QString& batchPath, const QStringList& args);
QString validateStdioInvocation(const QString& command, const QStringList& args);

namespace {

QString tr(const char* source) {
    return QCoreApplication::translate("McpConfigTransfer", source);
}

constexpr int kFormatVersion = 1;
const QString kFormat = QStringLiteral("nexusai-mcp-servers");

// docs/36 B1-4：cmd.exe 在解析 .cmd/.bat 命令行时会把以下字符当作
// 命令分隔/重定向/变量展开语法。即便参数被双引号包裹，`%`（变量展开）
// 与 `!`（延迟展开）仍会生效，且 `cmd /s /c` 的引号剥离规则让“只给危险
// 字符加引号”无法可靠收敛，因此统一拒绝。
const QString kCmdMetacharacters = QStringLiteral("&|<>^()!%");

bool isBatchWrapperPath(const QString& path) {
    const QString trimmed = path.trimmed();
    return trimmed.endsWith(QLatin1String(".cmd"), Qt::CaseInsensitive)
        || trimmed.endsWith(QLatin1String(".bat"), Qt::CaseInsensitive);
}

// QJsonArray → QVariantList → QStringList；数字等非字符串项按 toString
// 读取（校验只看字符，不改变导入落盘的数据形态）。
QStringList argsFromVariant(const QVariant& value) {
    if (value.canConvert<QStringList>()) return value.toStringList();
    QStringList out;
    if (value.canConvert<QVariantList>()) {
        for (const QVariant& item : value.toList()) out << item.toString();
    }
    return out;
}

// 导入只接受这几类字段：未知字段（尤其是未来可能出现的凭据字段）不落盘。
QVariantMap sanitizeServer(const QVariantMap& raw) {
    QVariantMap clean;
    for (const QString& field : {QStringLiteral("name"), QStringLiteral("transport"),
                                 QStringLiteral("command"), QStringLiteral("url")}) {
        if (raw.contains(field)) clean.insert(field, raw.value(field));
    }
    if (raw.contains(QStringLiteral("args")))
        clean.insert(QStringLiteral("args"), raw.value(QStringLiteral("args")));
    for (const QString& field : {QStringLiteral("env"), QStringLiteral("headers")}) {
        if (!raw.contains(field)) continue;
        const QVariantMap map = raw.value(field).toMap();
        if (!map.isEmpty()) clean.insert(field, map);
    }
    clean.insert(QStringLiteral("enabled"), false);
    return clean;
}

QString validate(const QVariantMap& server) {
    const QString name = server.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty()) return tr("缺少服务器名称");
    const QString transport = server.value(QStringLiteral("transport")).toString();
    if (transport != QLatin1String("stdio") && transport != QLatin1String("http"))
        return tr("传输方式必须是 stdio 或 http");
    if (transport == QLatin1String("stdio")) {
        const QString command =
            server.value(QStringLiteral("command")).toString().trimmed();
        if (command.isEmpty())
            return tr("stdio 服务器缺少启动命令");
        // docs/36 B1-4：.cmd/.bat 包装器的参数会在 cmd.exe 中被二次解释，
        // 危险元字符必须在导入阶段就拦下（该条目不会进入 applyImport，
        // 因此不会落盘），错误口径与启动侧完全相同。
        return validateStdioInvocation(
            command,
            argsFromVariant(server.value(QStringLiteral("args"))));
    }
    if (server.value(QStringLiteral("url")).toString().trimmed().isEmpty())
        return tr("http 服务器缺少地址");
    return {};
}

bool hasSecrets(const QVariantMap& server) {
    return !server.value(QStringLiteral("env")).toMap().isEmpty()
        || !server.value(QStringLiteral("headers")).toMap().isEmpty();
}

QJsonObject exportEntry(const QVariantMap& server) {
    QJsonObject out;
    for (const QString& field : {QStringLiteral("name"), QStringLiteral("transport"),
                                 QStringLiteral("command"), QStringLiteral("url")}) {
        if (server.contains(field))
            out.insert(field, QJsonValue::fromVariant(server.value(field)));
    }
    const QVariant args = server.value(QStringLiteral("args"));
    if (args.isValid())
        out.insert(QStringLiteral("args"), QJsonValue::fromVariant(args));
    // 显式禁用：导入方必须逐台审阅后才能启用。
    out.insert(QStringLiteral("enabled"), false);
    return out;
}

QString uniqueName(const QString& base, const QSet<QString>& used) {
    if (!used.contains(base)) return base;
    for (int suffix = 2; suffix < 1000; ++suffix) {
        const QString candidate = QStringLiteral("%1 (%2)").arg(base).arg(suffix);
        if (!used.contains(candidate)) return candidate;
    }
    return QStringLiteral("%1 (%2)").arg(base).arg(QDateTime::currentMSecsSinceEpoch());
}

} // namespace

QJsonObject buildExport(const QVector<QVariantMap>& servers) {
    QJsonArray array;
    int secretsOmitted = 0;
    for (const QVariantMap& server : servers) {
        if (server.value(QStringLiteral("name")).toString().trimmed().isEmpty()) continue;
        if (hasSecrets(server)) ++secretsOmitted;
        array.append(exportEntry(server));
    }
    QJsonObject root;
    root.insert(QStringLiteral("format"), kFormat);
    root.insert(QStringLiteral("version"), kFormatVersion);
    root.insert(QStringLiteral("servers"), array);
    root.insert(QStringLiteral("secretsOmitted"), secretsOmitted);
    QJsonArray omitted;
    omitted.append(QStringLiteral("env"));
    omitted.append(QStringLiteral("headers"));
    root.insert(QStringLiteral("omitted"), omitted);
    return root;
}

bool writeExport(const QString& path, const QVector<QVariantMap>& servers,
                 QString* error) {
    if (path.trimmed().isEmpty()) {
        if (error) *error = tr("请选择保存位置");
        return false;
    }
    // docs/49 D4: rename 替换会丢原文件的执行位等权限，commit 后恢复。
    const QFile::Permissions savedPerms = FilePermissions::captureForReplace(path);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = tr("无法写入文件：%1").arg(file.errorString());
        return false;
    }
    file.write(QJsonDocument(buildExport(servers)).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error) *error = tr("保存失败：%1").arg(file.errorString());
        return false;
    }
    FilePermissions::restoreAfterReplace(path, savedPerms);
    return true;
}

ImportPreview previewImportFile(const QString& path,
                                const QVector<QVariantMap>& current) {
    ImportPreview preview;
    if (path.trimmed().isEmpty()) {
        preview.error = tr("请选择配置文件");
        return preview;
    }
    QFile file(path);
    if (!file.exists()) {
        preview.error = tr("文件不存在：%1").arg(path);
        return preview;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        preview.error = tr("无法读取文件：%1").arg(file.errorString());
        return preview;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        preview.error = tr("不是有效的 JSON：%1").arg(parseError.errorString());
        return preview;
    }
    if (!document.isObject()) {
        preview.error = tr("不是有效的 MCP 服务器配置文件");
        return preview;
    }
    const QJsonObject root = document.object();
    const QJsonValue serversValue = root.value(QStringLiteral("servers"));
    if (!serversValue.isArray()) {
        preview.error = tr("不是有效的 MCP 服务器配置文件");
        return preview;
    }
    if (root.contains(QStringLiteral("format"))
        && root.value(QStringLiteral("format")).toString() != kFormat) {
        preview.error = tr("文件格式不受支持：%1")
                            .arg(root.value(QStringLiteral("format")).toString());
        return preview;
    }

    preview.ok = true;
    QSet<QString> existingNames;
    for (const QVariantMap& entry : current)
        existingNames.insert(entry.value(QStringLiteral("name")).toString());
    for (const QJsonValue& value : serversValue.toArray()) {
        ++preview.total;
        ImportEntry entry;
        entry.server = sanitizeServer(value.toObject().toVariantMap());
        entry.error = validate(entry.server);
        if (entry.error.isEmpty()) {
            ++preview.importable;
            if (hasSecrets(entry.server)) ++preview.withSecrets;
            entry.conflict = existingNames.contains(
                entry.server.value(QStringLiteral("name")).toString());
            if (entry.conflict) ++preview.conflictCount;
        }
        preview.entries.append(entry);
    }
    return preview;
}

MergePolicy policyFromName(const QString& name) {
    const QString normalized = name.trimmed().toLower();
    if (normalized == QLatin1String("overwrite")) return MergePolicy::Overwrite;
    if (normalized == QLatin1String("rename")) return MergePolicy::Rename;
    return MergePolicy::Skip;
}

QString policyName(MergePolicy policy) {
    switch (policy) {
    case MergePolicy::Overwrite: return QStringLiteral("overwrite");
    case MergePolicy::Rename:    return QStringLiteral("rename");
    case MergePolicy::Skip:      break;
    }
    return QStringLiteral("skip");
}

QString validateBatchArgs(const QString& batchPath, const QStringList& args) {
    // 非 batch（含解析失败的空路径、.exe、脚本语言解释器）不经过 shell，
    // 由 QProcess 以参数数组启动，元字符只是普通字符。
    if (!isBatchWrapperPath(batchPath)) return {};

    for (const QString& arg : args) {
        for (const QChar character : arg) {
            if (!kCmdMetacharacters.contains(character)) continue;
            return tr("参数「%1」含有 cmd 元字符「%2」：.cmd/.bat 启动器会经 "
                      "cmd.exe 解释参数，可能串联执行额外命令。"
                      "请改用 .exe 启动器，或移除参数中的 cmd 元字符。")
                .arg(arg, QString(character));
        }
    }
    return {};
}

QString validateStdioInvocation(const QString& command, const QStringList& args) {
    const QString trimmed = command.trimmed();
    if (trimmed.isEmpty()) return {};
    // 已带 .cmd/.bat 后缀（或绝对路径）：直接校验。
    if (isBatchWrapperPath(trimmed)) return validateBatchArgs(trimmed, args);

    // 裸名（npx / uvx）：与 StdioTransport 启动时使用同一个
    // QStandardPaths::findExecutable / PATHEXT 解析规则，确保导入与启动
    // 对“这是不是一个 batch 包装器”的判断一致。
    const QString resolved = QStandardPaths::findExecutable(trimmed);
    if (resolved.isEmpty()) {
        // 未安装：启动时同样判定为非 batch（随后 FailedToStart），放行以
        // 保持两侧口径一致；此时也无法串联执行任何命令。
        return {};
    }
    return validateBatchArgs(resolved, args);
}

QVector<QVariantMap> applyImport(const QVector<QVariantMap>& current,
                                 const ImportPreview& preview,
                                 MergePolicy policy, int* added, int* replaced,
                                 int* skipped) {
    QVector<QVariantMap> result = current;
    int addedCount = 0, replacedCount = 0, skippedCount = 0;

    QSet<QString> usedNames;
    for (const QVariantMap& entry : result)
        usedNames.insert(entry.value(QStringLiteral("name")).toString());

    for (const ImportEntry& entry : preview.entries) {
        if (!entry.error.isEmpty()) {
            ++skippedCount;
            continue;
        }
        QVariantMap server = entry.server;
        const QString name = server.value(QStringLiteral("name")).toString().trimmed();
        server.insert(QStringLiteral("name"), name);

        int existing = -1;
        for (int i = 0; i < result.size(); ++i) {
            if (result.at(i).value(QStringLiteral("name")).toString() == name) {
                existing = i;
                break;
            }
        }

        if (existing < 0) {
            result.append(server);
            usedNames.insert(name);
            ++addedCount;
            continue;
        }

        switch (policy) {
        case MergePolicy::Skip:
            ++skippedCount;
            break;
        case MergePolicy::Overwrite:
            result[existing] = server;   // sanitize 已把 enabled 归 false
            ++replacedCount;
            break;
        case MergePolicy::Rename: {
            const QString renamed = uniqueName(name, usedNames);
            server.insert(QStringLiteral("name"), renamed);
            result.append(server);
            usedNames.insert(renamed);
            ++addedCount;
            break;
        }
        }
    }

    if (added) *added = addedCount;
    if (replaced) *replaced = replacedCount;
    if (skipped) *skipped = skippedCount;
    return result;
}

} // namespace McpConfigTransfer
} // namespace NexusAI
