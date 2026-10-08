#include "SettingsBackup.h"

#include "SettingsSchema.h"
#include "core/mcp/McpConfig.h"
#include "infrastructure/llm/LlmProtocolRegistry.h"
#include "McpManager.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QSaveFile>
#include <QSettings>
#include <QSet>
#include <QUrl>
#include <climits>
#include <cmath>

namespace NexusAI {
namespace {
constexpr auto kFormat = "nexusai-settings";
constexpr int kVersion = 1;
constexpr qint64 kMaxImportBytes = 2 * 1024 * 1024;

QString localPath(const QString& path) {
    const QUrl url(path);
    return url.isLocalFile() ? url.toLocalFile() : path;
}

// Only explicitly registered, portable keys are eligible. Never restore
// permissions, system integration, volatile layout, or credentials implicitly.
bool portableKey(const SettingsKeyDef& def) {
    const QString& key = def.key;
    if (def.encrypted || key == QLatin1String("modelProfiles")
        || key == QLatin1String("schemaVersion")
        || key == QLatin1String("tools/disabled")
        || key == QLatin1String("agent/defaultPermissionMode")
        || key == QLatin1String("projects/activeId")
        || key == QLatin1String("settings.lastCategory")
        || key == QLatin1String("prefs/v1/settings.lastCategory")
        || key == QLatin1String("prefs/v1/general.autoStart")
        || key.startsWith(QLatin1String("security/"))
        || key.startsWith(QLatin1String("ui/"))) return false;
    return true;
}

QMap<QString, QVariant> portableKeys() {
    QMap<QString, QVariant> keys;
    for (const auto& def : SettingsSchema::registeredKeys())
        if (portableKey(def)) keys.insert(def.key, def.defaultValue);
    return keys;
}

bool validValue(const QString& key, const QJsonValue& value, const QVariant& def) {
    if (key == QLatin1String("activeProfileId")
        || key.startsWith(QLatin1String("prefs/v1/ui.font")))
        return value.isString();
    if (def.metaType().id() == QMetaType::Bool) return value.isBool();
    if (def.metaType().id() == QMetaType::Int)
        return value.isDouble() && std::isfinite(value.toDouble())
            && value.toDouble() >= 0 && value.toDouble() <= INT_MAX
            && std::floor(value.toDouble()) == value.toDouble();
    if (def.metaType().id() == QMetaType::QString) return value.isString();
    if (key == QLatin1String("prefs/v1/ui.locale")) return value.isString();
    return false;
}

// An explicit field list also prevents unknown future profile fields from
// silently leaking credentials into a portable, unencrypted JSON file.
QJsonObject cleanProfile(const QJsonObject& input) {
    static const QStringList fields{
        QStringLiteral("id"), QStringLiteral("displayName"), QStringLiteral("kind"),
        QStringLiteral("provider"), QStringLiteral("apiProtocol"),
        QStringLiteral("baseUrl"), QStringLiteral("model"), QStringLiteral("models"),
        QStringLiteral("temperature"), QStringLiteral("topP"),
        QStringLiteral("maxOutputTokens"), QStringLiteral("numCtx"),
        QStringLiteral("contextLength"), QStringLiteral("reasoningEffort")};
    QJsonObject clean;
    for (const QString& field : fields)
        if (input.contains(field)) clean.insert(field, input.value(field));
    return clean;
}

QJsonObject cleanServer(const QJsonObject& input) {
    QJsonObject clean;
    for (const QString& field : {QStringLiteral("name"), QStringLiteral("transport"),
                                 QStringLiteral("command"), QStringLiteral("args"),
                                 QStringLiteral("url")})
        if (input.contains(field)) clean.insert(field, input.value(field));
    clean.insert(QStringLiteral("enabled"), false); // explicit re-enable after review
    return clean;
}

QStringList omittedSecrets() {
    return {QStringLiteral("模型 API Key"), QStringLiteral("Web Search API Key"),
            QStringLiteral("MCP 环境变量与请求头")};
}
} // namespace

SettingsBackup* SettingsBackup::s_instance = nullptr;
SettingsBackup::SettingsBackup(QObject* parent) : QObject(parent) {}
SettingsBackup* SettingsBackup::instance() {
    if (!s_instance) s_instance = new SettingsBackup;
    return s_instance;
}
SettingsBackup* SettingsBackup::create(QQmlEngine*, QJSEngine*) {
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}
void SettingsBackup::setError(const QString& error) {
    if (m_lastError == error) return;
    m_lastError = error;
    emit lastErrorChanged();
}

bool SettingsBackup::exportSettings(const QString& path) {
    setError({});
    QSettings s;
    s.sync();
    if (s.status() != QSettings::NoError) {
        setError(tr("无法读取当前设置"));
        return false;
    }
    QJsonObject settings;
    const auto keys = portableKeys();
    for (auto it = keys.cbegin(); it != keys.cend(); ++it) {
        if (!s.contains(it.key())) continue;
        QVariant value = s.value(it.key());
        if (it.value().metaType().id() == QMetaType::Bool) value = value.toBool();
        else if (it.value().metaType().id() == QMetaType::Int) value = value.toInt();
        else if (it.value().metaType().id() == QMetaType::QString) value = value.toString();
        else if (it.key().startsWith(QLatin1String("prefs/v1/ui.font"))
                 || it.key() == QLatin1String("prefs/v1/ui.locale")
                 || it.key() == QLatin1String("activeProfileId")) value = value.toString();
        if (!validValue(it.key(), QJsonValue::fromVariant(value), it.value())) continue;
        settings.insert(it.key(), QJsonValue::fromVariant(value));
    }

    QJsonArray profiles;
    if (s.contains(QStringLiteral("modelProfiles"))) {
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(
            s.value(QStringLiteral("modelProfiles")).toString().toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isArray()) {
            setError(tr("模型配置已损坏，无法安全导出"));
            return false;
        }
        for (const auto& entry : doc.array()) {
            if (!entry.isObject()) {
                setError(tr("模型配置包含无效条目"));
                return false;
            }
            profiles.append(cleanProfile(entry.toObject()));
        }
    }

    QJsonArray servers;
    // Missing MCP file is fine; unreadable/corrupt data must not silently
    // turn a supposedly complete export into an incomplete one.
    const QString mcpPath = McpConfig::configFilePath();
    if (QFile::exists(mcpPath)) {
        QFile file(mcpPath);
        if (!file.open(QIODevice::ReadOnly)) {
            setError(tr("无法读取 MCP 服务器配置"));
            return false;
        }
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()
            || !doc.object().value(QStringLiteral("servers")).isArray()) {
            setError(tr("MCP 服务器配置已损坏"));
            return false;
        }
        for (const auto& entry : doc.object().value(QStringLiteral("servers")).toArray()) {
            if (!entry.isObject()) {
                setError(tr("MCP 服务器配置包含无效条目"));
                return false;
            }
            servers.append(cleanServer(entry.toObject()));
        }
    }

    QJsonObject root{{QStringLiteral("format"), QString::fromLatin1(kFormat)},
                     {QStringLiteral("version"), kVersion},
                     {QStringLiteral("schemaVersion"), SettingsSchema::kSchemaVersion},
                     {QStringLiteral("exportedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                     {QStringLiteral("settings"), settings},
                     {QStringLiteral("profiles"), profiles},
                     {QStringLiteral("mcpServers"), servers}};
    QSaveFile file(localPath(path));
    if (!file.open(QIODevice::WriteOnly)) {
        setError(tr("无法创建导出文件：%1").arg(file.errorString()));
        return false;
    }
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        setError(tr("导出文件写入失败：%1").arg(file.errorString()));
        return false;
    }
    return true;
}

QVariantMap SettingsBackup::parseImport(const QString& path, QVariantMap* settings,
                                        QVariantList* profiles, QVariantList* servers) {
    auto fail = [](const QString& error) -> QVariantMap {
        return {{QStringLiteral("valid"), false}, {QStringLiteral("error"), error}};
    };
    QFile file(localPath(path));
    if (!file.open(QIODevice::ReadOnly)) return fail(tr("无法读取备份文件：%1").arg(file.errorString()));
    if (file.size() > kMaxImportBytes) return fail(tr("备份文件超过 2 MB 限制"));
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.read(kMaxImportBytes + 1), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return fail(tr("备份文件不是有效的 JSON 对象"));
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String(kFormat)
        || root.value(QStringLiteral("version")).toInt(-1) != kVersion)
        return fail(tr("不支持的设置备份格式或版本"));
    if (root.value(QStringLiteral("schemaVersion")).toInt(-1) != SettingsSchema::kSchemaVersion)
        return fail(tr("设置版本不匹配，请使用兼容版本的应用恢复"));
    if (!root.value(QStringLiteral("settings")).isObject()
        || !root.value(QStringLiteral("profiles")).isArray()
        || !root.value(QStringLiteral("mcpServers")).isArray())
        return fail(tr("备份缺少设置、模型或 MCP 数据段"));
    const auto keys = portableKeys();
    const QJsonObject saved = root.value(QStringLiteral("settings")).toObject();
    settings->clear();
    for (auto it = saved.begin(); it != saved.end(); ++it) {
        if (!keys.contains(it.key()) || !validValue(it.key(), it.value(), keys.value(it.key())))
            return fail(tr("备份包含不允许恢复或类型错误的设置项：%1").arg(it.key()));
        settings->insert(it.key(), it.value().toVariant());
    }
    const QJsonArray modelArray = root.value(QStringLiteral("profiles")).toArray();
    profiles->clear();
    QSet<QString> profileIds;
    for (const QJsonValue& value : modelArray) {
        if (!value.isObject()) return fail(tr("模型配置格式错误"));
        const QJsonObject raw = value.toObject();
        if (raw.contains(QStringLiteral("apiKey"))) return fail(tr("备份包含密钥字段，拒绝恢复"));
        const QString id = raw.value(QStringLiteral("id")).toString();
        if (id.isEmpty() || profileIds.contains(id)
            || !raw.value(QStringLiteral("provider")).isString()
            || !raw.value(QStringLiteral("apiProtocol")).isString()
            || (!LlmProtocolRegistry::isKnownProvider(raw.value(QStringLiteral("provider")).toString())
                && !LlmProtocolRegistry::find(raw.value(QStringLiteral("apiProtocol")).toString())))
            return fail(tr("模型配置 ID 或协议无效"));
        profileIds.insert(id);
        profiles->append(cleanProfile(raw).toVariantMap());
    }
    if (settings->contains(QStringLiteral("activeProfileId"))
        && !settings->value(QStringLiteral("activeProfileId")).toString().isEmpty()
        && !profileIds.contains(settings->value(QStringLiteral("activeProfileId")).toString()))
        return fail(tr("活动模型 ID 未出现在模型列表中"));

    servers->clear();
    QSet<QString> serverNames;
    for (const QJsonValue& value : root.value(QStringLiteral("mcpServers")).toArray()) {
        if (!value.isObject()) return fail(tr("MCP 配置格式错误"));
        const QJsonObject raw = value.toObject();
        if (raw.contains(QStringLiteral("env")) || raw.contains(QStringLiteral("headers")))
            return fail(tr("备份包含 MCP 凭据字段，拒绝恢复"));
        const QString name = raw.value(QStringLiteral("name")).toString().trimmed();
        const QString transport = raw.value(QStringLiteral("transport")).toString();
        if (name.isEmpty() || serverNames.contains(name)
            || (transport != QLatin1String("stdio") && transport != QLatin1String("http"))
            || (raw.contains(QStringLiteral("args")) && !raw.value(QStringLiteral("args")).isArray()))
            return fail(tr("MCP 服务器名称或传输方式无效"));
        serverNames.insert(name);
        servers->append(cleanServer(raw).toVariantMap());
    }
    QStringList sections{tr("应用设置（%1 项）").arg(saved.size()),
                         tr("模型配置（%1 条）").arg(profiles->size()),
                         tr("MCP 服务器（%1 条，恢复后保持禁用）").arg(servers->size())};
    QStringList warnings{tr("相同模型端点和 MCP 服务的本机凭据将保留；其他配置不会从备份恢复凭据。"),
                         tr("安全权限、工具启用状态、开机自启及运行时布局不会恢复。"),
                         tr("MCP 命令参数、服务地址或自定义文本可能包含自行填写的敏感信息，请检查备份文件。"),
                         tr("恢复后需要重启应用；会话和知识库数据库不在此备份内。")};
    return {{QStringLiteral("valid"), true}, {QStringLiteral("error"), QString()},
            {QStringLiteral("sections"), sections},
            {QStringLiteral("skippedSecrets"), omittedSecrets()},
            {QStringLiteral("warnings"), warnings}};
}

QVariantMap SettingsBackup::previewImport(const QString& path) {
    QVariantMap settings;
    QVariantList profiles, servers;
    return parseImport(path, &settings, &profiles, &servers);
}

bool SettingsBackup::importSettings(const QString& path) {
    setError({});
    QVariantMap imported;
    QVariantList profiles, servers;
    const QVariantMap preview = parseImport(path, &imported, &profiles, &servers);
    if (!preview.value(QStringLiteral("valid")).toBool()) {
        setError(preview.value(QStringLiteral("error")).toString());
        return false;
    }
    QSettings s;
    s.sync();
    if (s.status() != QSettings::NoError) {
        setError(tr("无法读取当前设置，恢复已取消"));
        return false;
    }
    // Carry credentials forward only for an identical local endpoint. A backup
    // from another machine must never attach local credentials to a new target.
    QJsonParseError profileError;
    const QJsonDocument currentProfiles = QJsonDocument::fromJson(
        s.value(QStringLiteral("modelProfiles")).toString().toUtf8(), &profileError);
    if (s.contains(QStringLiteral("modelProfiles"))
        && (profileError.error != QJsonParseError::NoError || !currentProfiles.isArray())) {
        setError(tr("当前模型配置已损坏，恢复已取消"));
        return false;
    }
    for (QVariant& entry : profiles) {
        QVariantMap profile = entry.toMap();
        for (const QJsonValue& local : currentProfiles.array()) {
            const QJsonObject old = local.toObject();
            if (old.value(QStringLiteral("id")).toString() != profile.value(QStringLiteral("id")).toString()
                || old.value(QStringLiteral("provider")).toString() != profile.value(QStringLiteral("provider")).toString()
                || old.value(QStringLiteral("apiProtocol")).toString() != profile.value(QStringLiteral("apiProtocol")).toString()
                || old.value(QStringLiteral("baseUrl")).toString() != profile.value(QStringLiteral("baseUrl")).toString()) continue;
            if (old.contains(QStringLiteral("apiKey")))
                profile.insert(QStringLiteral("apiKey"), old.value(QStringLiteral("apiKey")).toString());
            break;
        }
        entry = profile;
    }
    imported.insert(QStringLiteral("modelProfiles"), QString::fromUtf8(
        QJsonDocument(QJsonArray::fromVariantList(profiles)).toJson(QJsonDocument::Compact)));

    const auto localServers = McpConfig::load();
    for (QVariant& entry : servers) {
        QVariantMap server = entry.toMap();
        for (const QVariantMap& local : localServers) {
            if (local.value(QStringLiteral("name")) != server.value(QStringLiteral("name"))
                || local.value(QStringLiteral("transport")) != server.value(QStringLiteral("transport"))
                || local.value(QStringLiteral("command")) != server.value(QStringLiteral("command"))
                || local.value(QStringLiteral("args")).toList() != server.value(QStringLiteral("args")).toList()
                || local.value(QStringLiteral("url")) != server.value(QStringLiteral("url"))) continue;
            server.insert(QStringLiteral("env"), local.value(QStringLiteral("env")));
            server.insert(QStringLiteral("headers"), local.value(QStringLiteral("headers")));
            break;
        }
        entry = server;
    }
    // Snapshot exactly the keys to be changed; an absent key must remain
    // absent if a later step fails.
    QMap<QString, QVariant> previous;
    QSet<QString> present;
    for (auto it = imported.cbegin(); it != imported.cend(); ++it) {
        if (s.contains(it.key())) {
            previous.insert(it.key(), s.value(it.key()));
            present.insert(it.key());
        }
        s.setValue(it.key(), it.value());
    }
    s.sync();
    const bool settingsOk = s.status() == QSettings::NoError;
    const bool mcpOk = settingsOk && McpConfig::save(servers);
    if (!mcpOk) {
        for (auto it = imported.cbegin(); it != imported.cend(); ++it) {
            if (present.contains(it.key())) s.setValue(it.key(), previous.value(it.key()));
            else s.remove(it.key());
        }
        s.sync();
        setError(s.status() == QSettings::NoError
                     ? tr("恢复失败，原有设置已保留")
                     : tr("恢复失败且回滚未完成，请检查当前设置"));
        return false;
    }
    McpManager::instance()->applyRestoredConfigs(servers);
    if (!m_restartRequired) {
        m_restartRequired = true;
        emit restartRequiredChanged();
    }
    return true;
}

} // namespace NexusAI
