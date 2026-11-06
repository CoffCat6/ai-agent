#include "SettingsBackup.h"

#include "core/utils/SettingsSchema.h"
#include "core/mcp/McpConfig.h"
#include "infrastructure/llm/LlmProtocolRegistry.h"
#include "McpManager.h"
#include "PreferenceRegistry.h"
#include "SettingsCatalog.h"
#include "SettingsValueFormat.h"
#include "core/utils/Logger.h"
#include "core/utils/SettingsRepository.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSysInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QSaveFile>
#include <QSet>
#include <QUrl>
#include <QVector>
#include <algorithm>
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

// 诊断包里的文本文件：要么完整写下，要么什么都不留。
bool writeTextFile(const QString& path, const QString& text, QString* error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    const QByteArray bytes = text.toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

// Only explicitly registered, portable keys are eligible. Never restore
// permissions, system integration, volatile layout, or credentials implicitly.
bool portableKey(const SettingsKeyDef& def) {
    const QString& key = def.key;
    if (def.encrypted || def.legacy || key == QLatin1String("modelProfiles")
        || key == QLatin1String("schemaVersion")
        || key == QLatin1String("tools/disabled")
        || key == QLatin1String("agent/defaultPermissionMode")
        || key == QLatin1String("projects/activeId")
        || key == QLatin1String("settings.lastCategory")
        || key == QLatin1String("prefs/v1/settings.lastCategory")
        || key == QLatin1String("prefs/v1/settings.collapsedGroups")
        || key == QLatin1String("prefs/v1/general.onboarded")
        || key == QLatin1String("prefs/v1/general.autoStart")
        || key.startsWith(QLatin1String("security/"))
        || key.startsWith(QLatin1String("ui/"))) return false;
    return true;
}

QMap<QString, SettingsKeyDef> portableKeys() {
    QMap<QString, SettingsKeyDef> keys;
    for (const SettingsKeyDef& def : SettingsSchema::registeredKeys())
        if (portableKey(def)) keys.insert(def.key, def);
    return keys;
}

bool validValue(const SettingsKeyDef& def, const QJsonValue& value) {
    bool valid = false;
    switch (def.type) {
    case SettingValueType::Any:
        return false;
    case SettingValueType::Bool:
        valid = value.isBool();
        break;
    case SettingValueType::Int:
        valid = value.isDouble() && std::isfinite(value.toDouble())
            && std::floor(value.toDouble()) == value.toDouble()
            && (!def.constraint.hasRange
                || (value.toDouble() >= def.constraint.min
                    && value.toDouble() <= def.constraint.max));
        break;
    case SettingValueType::Double:
        valid = value.isDouble() && std::isfinite(value.toDouble());
        break;
    case SettingValueType::String:
        valid = value.isString();
        if (valid && def.constraint.required) valid = !value.toString().isEmpty();
        if (valid && def.constraint.maxLength > 0)
            valid = value.toString().size() <= def.constraint.maxLength;
        if (valid && !def.constraint.options.isEmpty())
            valid = def.constraint.options.contains(value.toString());
        break;
    case SettingValueType::StringList:
        valid = value.isArray();
        if (valid) {
            for (const QJsonValue& item : value.toArray())
                if (!item.isString()) return false;
        }
        break;
    }
    return valid;
}

// An explicit field list also prevents unknown future profile fields from
// silently leaking credentials into a portable, unencrypted JSON file.
// docs/43 §7 #6：modelParams 为按模型参数覆盖表（模型名 → {参数键: 值}），
// 整 map 透传，导出/导入两侧共用本白名单（一处改动双侧生效）；值的合法性
// 由 SettingsService::sanitizeModelParams 在写入侧单点把关，备份层只校验
// 身份字段，不做值的 sanitize。topP 为 v5 幽灵键（全仓无读写），v6 迁移已
// 负责删除存量数据，白名单同步摘除（AC-7）。
QJsonObject cleanProfile(const QJsonObject& input) {
    static const QStringList fields{
        QStringLiteral("id"), QStringLiteral("displayName"), QStringLiteral("kind"),
        QStringLiteral("provider"), QStringLiteral("apiProtocol"),
        QStringLiteral("baseUrl"), QStringLiteral("model"), QStringLiteral("models"),
        QStringLiteral("temperature"),
        QStringLiteral("maxOutputTokens"), QStringLiteral("numCtx"),
        QStringLiteral("contextLength"), QStringLiteral("reasoningEffort"),
        QStringLiteral("modelParams")};
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

// 导出/备份文件里的键是物理键：声明式偏好带 "prefs/v1/" 前缀，M1 键
// 原样。设置目录与 PreferenceStore 用的是逻辑键（偏好不带前缀），
// 分类查询与值快照都必须换回逻辑键。
QString logicalKeyOf(const QString& exportKey) {
    const QString prefix = QStringLiteral("prefs/v1/");
    return exportKey.startsWith(prefix) ? exportKey.mid(prefix.size()) : exportKey;
}

QString categoryOfExportKey(const QString& exportKey) {
    return SettingsCatalog::instance()->entry(logicalKeyOf(exportKey))
        .value(QStringLiteral("category")).toString();
}

bool inCategoryScope(const QStringList& categories, const QString& categoryId) {
    return categories.isEmpty() || categories.contains(categoryId);
}

constexpr int kMaxImportChanges = 200;
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

QJsonObject SettingsBackup::buildExportPayload(const QStringList& categories) {
    QJsonObject settings;
    const auto keys = portableKeys();
    const bool includeModel = inCategoryScope(categories, QStringLiteral("model"));
    const bool includeMcp = inCategoryScope(categories, QStringLiteral("mcp"));
    QStringList keysToRead = keys.keys();
    if (includeModel) keysToRead.append(QStringLiteral("modelProfiles"));
    QVariantMap storedValues;
    QStringList presentKeys;
    QString readError;
    if (!SettingsRepository::readBatch(keysToRead, &storedValues, &presentKeys,
                                       &readError)) {
        setError(tr("无法读取当前设置：%1").arg(readError));
        return {};
    }
    for (auto it = keys.cbegin(); it != keys.cend(); ++it) {
        if (!presentKeys.contains(it.key())) continue;
        // 分类过滤：未知分类的键在选中任意分类时一律排除，避免"只导出
        // 模型配置"却悄悄带上一条目录里没登记的键。
        if (!categories.isEmpty()
            && !categories.contains(categoryOfExportKey(it.key()))) continue;
        QVariant value = storedValues.value(it.key());
        switch (it.value().type) {
        case SettingValueType::Bool: value = value.toBool(); break;
        case SettingValueType::Int: value = value.toInt(); break;
        case SettingValueType::Double: value = value.toDouble(); break;
        case SettingValueType::String: value = value.toString(); break;
        case SettingValueType::StringList: value = value.toStringList(); break;
        case SettingValueType::Any: break;
        }
        if (!validValue(it.value(), QJsonValue::fromVariant(value))) continue;
        settings.insert(it.key(), QJsonValue::fromVariant(value));
    }

    QJsonArray profiles;
    if (includeModel && presentKeys.contains(QStringLiteral("modelProfiles"))) {
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(
            storedValues.value(QStringLiteral("modelProfiles")).toString().toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isArray()) {
            setError(tr("模型配置已损坏，无法安全导出"));
            return {};
        }
        for (const auto& entry : doc.array()) {
            if (!entry.isObject()) {
                setError(tr("模型配置包含无效条目"));
                return {};
            }
            profiles.append(cleanProfile(entry.toObject()));
        }
    }

    QJsonArray servers;
    // Missing MCP file is fine; unreadable/corrupt data must not silently
    // turn a supposedly complete export into an incomplete one. 按分类导出时
    // 不读 MCP：一份损坏的 MCP 配置不该挡住"只导出模型配置"。
    const QString mcpPath = McpConfig::configFilePath();
    if (includeMcp && QFile::exists(mcpPath)) {
        QFile file(mcpPath);
        if (!file.open(QIODevice::ReadOnly)) {
            setError(tr("无法读取 MCP 服务器配置"));
            return {};
        }
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()
            || !doc.object().value(QStringLiteral("servers")).isArray()) {
            setError(tr("MCP 服务器配置已损坏"));
            return {};
        }
        for (const auto& entry : doc.object().value(QStringLiteral("servers")).toArray()) {
            if (!entry.isObject()) {
                setError(tr("MCP 服务器配置包含无效条目"));
                return {};
            }
            servers.append(cleanServer(entry.toObject()));
        }
    }

    return QJsonObject{{QStringLiteral("format"), QString::fromLatin1(kFormat)},
                       {QStringLiteral("version"), kVersion},
                       {QStringLiteral("schemaVersion"), SettingsSchema::kSchemaVersion},
                       {QStringLiteral("exportedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                       {QStringLiteral("settings"), settings},
                       {QStringLiteral("profiles"), profiles},
                       {QStringLiteral("mcpServers"), servers}};
}

QStringList SettingsBackup::exportableCategories() const {
    QSet<QString> present;
    for (const SettingsKeyDef& def : SettingsSchema::registeredKeys()) {
        if (!portableKey(def)) continue;
        const QString categoryId = categoryOfExportKey(def.key);
        if (!categoryId.isEmpty()) present.insert(categoryId);
    }
    // 模型配置与 MCP 服务器不在 SettingsSchema 里，但都属于可导出的分类。
    present.insert(QStringLiteral("model"));
    present.insert(QStringLiteral("mcp"));
    QStringList ordered;
    for (const QString& categoryId : PreferenceRegistry::instance()->categories())
        if (present.contains(categoryId)) ordered.append(categoryId);
    return ordered;
}

bool SettingsBackup::exportSettings(const QString& path,
                                    const QStringList& categories) {
    setError({});
    const QJsonObject root = buildExportPayload(categories);
    if (root.isEmpty()) return false; // buildExportPayload 已设置 lastError
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

// 诊断包 = 脱敏设置快照 + 日志副本 + 环境信息。Qt 没有公开的 zip 写入
// API，与其背一个第三方压缩依赖，不如导出一个目录，用户自己打包即可。
QString SettingsBackup::exportDiagnostics(const QString& directory) {
    setError({});
    const QString basePath = localPath(directory).trimmed();
    if (basePath.isEmpty()) {
        setError(tr("未指定诊断包保存位置"));
        return {};
    }
    QDir baseDir(basePath);
    if (!baseDir.exists()) {
        setError(tr("保存位置不存在：%1").arg(basePath));
        return {};
    }

    // 同一秒内重复导出不覆盖：追加 -2、-3 …（诊断包之间常拿来对比）。
    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString folderName = QStringLiteral("nexus-diagnostics-%1").arg(stamp);
    for (int suffix = 2; baseDir.exists(folderName); ++suffix)
        folderName = QStringLiteral("nexus-diagnostics-%1-%2").arg(stamp).arg(suffix);
    if (!baseDir.mkpath(folderName)) {
        setError(tr("无法创建诊断包目录：%1").arg(folderName));
        return {};
    }
    const QDir bundle(baseDir.filePath(folderName));

    // 1) 设置快照：与「导出设置」共用过滤规则，密钥字段不会写入。
    const QJsonObject payload = buildExportPayload();
    if (payload.isEmpty()) return {}; // lastError 已由 buildExportPayload 设置
    QString writeError;
    if (!writeTextFile(bundle.filePath(QStringLiteral("settings.json")),
                       QString::fromUtf8(QJsonDocument(payload).toJson(
                           QJsonDocument::Indented)),
                       &writeError)) {
        setError(tr("写入 settings.json 失败：%1").arg(writeError));
        return {};
    }

    // 2) 日志副本（当前文件 + 轮转历史）。单个文件超过 32 MB 就跳过：
    // 诊断包是给人看的，不该因为一个超大日志把导出卡住。
    int copiedLogs = 0;
    int skippedLogs = 0;
    constexpr qint64 kMaxLogBytes = 32LL * 1024 * 1024;
    if (bundle.mkpath(QStringLiteral("logs"))) {
        const QFileInfoList logFiles = QDir(Logger::logDir())
            .entryInfoList({QStringLiteral("nexus-*.log")}, QDir::Files, QDir::Time);
        for (const QFileInfo& info : logFiles) {
            if (info.size() > kMaxLogBytes) {
                ++skippedLogs;
                continue;
            }
            const QString target =
                bundle.filePath(QStringLiteral("logs/") + info.fileName());
            if (QFile::copy(info.absoluteFilePath(), target)) ++copiedLogs;
            else ++skippedLogs;
        }
    }

    // 3) 环境信息：只记录「环境是什么」，不记录用户内容。
    const QStringList env{
        QStringLiteral("Nexus AI 诊断包"),
        QStringLiteral("生成时间: %1")
            .arg(QDateTime::currentDateTime().toString(Qt::ISODate)),
        QStringLiteral("应用版本: %1").arg(QCoreApplication::applicationVersion()),
        QStringLiteral("Qt 运行时: %1").arg(QString::fromLatin1(qVersion())),
        QStringLiteral("操作系统: %1").arg(QSysInfo::prettyProductName()),
        QStringLiteral("内核: %1 %2")
            .arg(QSysInfo::kernelType(), QSysInfo::kernelVersion()),
        QStringLiteral("CPU 架构: %1（构建：%2）")
            .arg(QSysInfo::currentCpuArchitecture(),
                 QSysInfo::buildCpuArchitecture()),
        QStringLiteral("应用数据目录: %1")
            .arg(QStandardPaths::writableLocation(
                QStandardPaths::AppLocalDataLocation)),
        QStringLiteral("日志目录: %1").arg(Logger::logDir()),
        QStringLiteral("最低日志级别: %1")
            .arg(QString::fromLatin1(Logger::levelName(Logger::minLevel()))),
        QStringLiteral("日志文件: 已复制 %1 个，跳过 %2 个（超过 32 MB 或无法读取）")
            .arg(copiedLogs).arg(skippedLogs),
        QString(),
        QStringLiteral("说明：settings.json 与「导出设置」使用同一份过滤规则——"),
        QStringLiteral("模型 API Key、Web Search API Key、MCP 环境变量与请求头不会写入。"),
        QStringLiteral("自由文本字段与 MCP 命令参数仍可能包含路径等敏感信息，分享前请检查。")};
    if (!writeTextFile(bundle.filePath(QStringLiteral("environment.txt")),
                       env.join(QLatin1Char('\n')), &writeError)) {
        setError(tr("写入 environment.txt 失败：%1").arg(writeError));
        return {};
    }

    return bundle.absolutePath();
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
        if (!keys.contains(it.key()) || !validValue(keys.value(it.key()), it.value()))
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
    // docs/31 O4：逐键差异。与当前值按设置中心同一口径比较，只列会变化
    // 或新增的键。portable 白名单已排除加密键；万一将来加入密钥类键，
    // 这里也只报"已配置 / 未包含"，绝不渲染备份里的值。
    struct ChangeRow {
        int order;
        QString title;
        QVariantMap map;
    };
    QVector<ChangeRow> rows;
    PreferenceRegistry* registry = PreferenceRegistry::instance();
    for (auto it = settings->cbegin(); it != settings->cend(); ++it) {
        const QString logicalKey = logicalKeyOf(it.key());
        const SettingValueSnapshot current = SettingsValue::inspect(logicalKey);
        const QVariant importedValue = it.value();
        if (current.known && !current.secret
            && SettingsValue::sameValueForKey(logicalKey, current.current,
                                              importedValue))
            continue;
        const QVariantMap meta = SettingsCatalog::instance()->entry(logicalKey);
        const QString categoryId = meta.value(QStringLiteral("category")).toString();
        QString title = meta.value(QStringLiteral("title")).toString();
        if (title.isEmpty()) title = it.key();
        QVariantMap change;
        change.insert(QStringLiteral("key"), it.key());
        change.insert(QStringLiteral("title"), title);
        change.insert(QStringLiteral("category"), categoryId);
        change.insert(QStringLiteral("categoryName"),
                      meta.value(QStringLiteral("categoryName")));
        change.insert(QStringLiteral("currentDisplay"),
                      current.known ? SettingsValue::displayOf(current, logicalKey)
                                    : QString());
        change.insert(QStringLiteral("importedDisplay"),
                      current.secret ? tr("未包含")
                                     : SettingsValue::displayValue(logicalKey,
                                                                   importedValue));
        change.insert(QStringLiteral("secret"), current.secret);
        rows.append({registry->categoryOrder(categoryId), title, change});
    }
    std::sort(rows.begin(), rows.end(), [](const ChangeRow& a, const ChangeRow& b) {
        if (a.order != b.order) return a.order < b.order;
        const int compared = a.title.localeAwareCompare(b.title);
        if (compared != 0) return compared < 0;
        return a.map.value(QStringLiteral("key")).toString()
            < b.map.value(QStringLiteral("key")).toString();
    });
    QVariantList changes;
    const int changesTotal = rows.size();
    for (int i = 0; i < changesTotal && i < kMaxImportChanges; ++i)
        changes.append(rows.at(i).map);

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
            {QStringLiteral("warnings"), warnings},
            {QStringLiteral("changes"), changes},
            {QStringLiteral("changesTotal"), changesTotal},
            {QStringLiteral("changesTruncated"),
             changesTotal - changes.size()}};
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
    QVariantMap currentSettings;
    QStringList currentPresentKeys;
    QString readError;
    if (!SettingsRepository::readBatch({QStringLiteral("modelProfiles")},
                                       &currentSettings, &currentPresentKeys,
                                       &readError)) {
        setError(tr("无法读取当前设置，恢复已取消：%1").arg(readError));
        return false;
    }
    // Carry credentials forward only for an identical local endpoint. A backup
    // from another machine must never attach local credentials to a new target.
    QJsonParseError profileError;
    const QJsonDocument currentProfiles = QJsonDocument::fromJson(
        currentSettings.value(QStringLiteral("modelProfiles")).toString().toUtf8(), &profileError);
    if (currentPresentKeys.contains(QStringLiteral("modelProfiles"))
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
    const QStringList importedKeys = imported.keys();
    QVariantMap previous;
    QStringList presentKeys;
    if (!SettingsRepository::readBatch(importedKeys, &previous, &presentKeys,
                                       &readError)) {
        setError(tr("无法读取当前设置，恢复已取消：%1").arg(readError));
        return false;
    }

    QString writeError;
    if (!SettingsRepository::writeBatch(imported, &writeError)) {
        setError(tr("设置写入失败，恢复已取消：%1").arg(writeError));
        return false;
    }
    if (!McpConfig::save(servers)) {
        QString rollbackError;
        bool settingsRolledBack = SettingsRepository::writeBatch(previous, &rollbackError);
        QStringList absentKeys;
        for (const QString& key : importedKeys)
            if (!presentKeys.contains(key)) absentKeys.append(key);
        if (!SettingsRepository::removeBatch(absentKeys, &rollbackError))
            settingsRolledBack = false;
        setError(settingsRolledBack
                     ? tr("恢复失败，原有设置已保留")
                     : tr("恢复失败且设置回滚未完成，请检查当前设置：%1").arg(rollbackError));
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
