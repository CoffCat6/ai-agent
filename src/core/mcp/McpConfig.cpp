#include "core/mcp/McpConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace NexusAI {
namespace McpConfig {

QString configFilePath() {
    const QString dataDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dataDir + QStringLiteral("/mcp_servers.json");
}

QVector<QVariantMap> load() {
    QFile file(configFilePath());
    QVector<QVariantMap> out;
    if (!file.open(QIODevice::ReadOnly)) return out;

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonArray servers = doc.object().value("servers").toArray();
    for (const auto& entry : servers) {
        const QJsonObject obj = entry.toObject();
        QVariantMap server;
        server["name"] = obj.value("name").toString();
        server["transport"] = obj.value("transport").toString();
        server["command"] = obj.value("command").toString();
        server["args"] = obj.value("args").toArray().toVariantList();
        server["env"] = obj.value("env").toObject().toVariantMap();
        server["headers"] = obj.value("headers").toObject().toVariantMap();
        server["url"] = obj.value("url").toString();
        server["enabled"] = obj.value("enabled").toBool(true);
        if (!server["name"].toString().isEmpty())
            out.append(server);
    }
    return out;
}

bool save(const QVariantList& servers) {
    const QString path = configFilePath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        qWarning() << "Failed to create MCP config directory for" << path;
        return false;
    }

    QJsonArray array;
    for (const QVariant& entry : servers) {
        const QVariantMap server = entry.toMap();
        QJsonObject obj;
        obj["name"] = server.value("name").toString();
        obj["transport"] = server.value("transport").toString();
        obj["command"] = server.value("command").toString();
        obj["args"] = QJsonArray::fromVariantList(
            server.value("args").toList());
        obj["env"] = QJsonObject::fromVariantMap(server.value("env").toMap());
        obj["headers"] = QJsonObject::fromVariantMap(server.value("headers").toMap());
        obj["url"] = server.value("url").toString();
        obj["enabled"] = server.value("enabled").toBool();
        array.append(obj);
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open MCP config for writing:" << path << file.errorString();
        return false;
    }
    const QByteArray data = QJsonDocument(QJsonObject{{"servers", array}})
                                .toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size()) {
        qWarning() << "Failed to write MCP config:" << path << file.errorString();
        return false;
    }
    if (!file.commit()) {
        qWarning() << "Failed to commit MCP config:" << path << file.errorString();
        return false;
    }
    return true;
}

} // namespace McpConfig

} // namespace NexusAI
