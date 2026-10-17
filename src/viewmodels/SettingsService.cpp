#include "SettingsService.h"
#include "core/utils/SettingsSchema.h"
#include "core/utils/Activity.h"
#include "core/utils/SettingsRepository.h"

#include "core/agent/AIAgent.h"
#include "core/utils/Logger.h"
#include "infrastructure/llm/LlmProtocolRegistry.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QCoreApplication>
#include <QDir>
#include <QUrl>
#include <QUuid>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dpapi.h>
#endif

Q_LOGGING_CATEGORY(lcSettings, "nexus.settings")

namespace NexusAI {

namespace {

constexpr auto kProfilesKey = "modelProfiles";
constexpr auto kActiveKey = "activeProfileId";

// docs/25 E9：contextTokens 的两版历史默认值。8192 是 v2 时代的默认上下文
// 窗口；300000 是 v3 期短暂启用、后因过于激进被收敛掉的值。存量配置若
// 正好等于其中之一即视为「旧默认」而非用户主动选择（自定义值是任何其它
// 数字），升级时归一到当前默认 AIAgent::kDefaultContextTokens。
constexpr int kLegacyContextTokensV2 = 8192;
constexpr int kLegacyContextTokensV3 = 300000;

// 厂商默认端点/模型表来自协议注册表：一个厂商目前就一条协议，所以取该厂商
// 默认协议的描述符。新增协议（例如 OpenAI Responses）只需要改那张表。
const LlmProtocolDescriptor* defaultsForProvider(const QString& provider) {
    return LlmProtocolRegistry::resolve(QString(), provider);
}

// kind 是设置页的"这条配置是从哪个预设来的"轴："openai" | "anthropic" |
// "ollama" | "custom"。它只决定要不要给用户预填端点与模型列表，不参与线格式、
// local-only 门控与能力表——那三件事仍然只看 provider / apiProtocol。
// 因此"自定义网关"是 kind=custom 的 openai 配置，而不是第五种协议。
bool isKnownKind(const QString& kind) {
    return kind == QLatin1String("custom") || LlmProtocolRegistry::isKnownProvider(kind);
}

// kind 缺失（0.3 前的旧 profile 根本没这个键）时按厂商推断：自定义不可能来自
// 旧数据，所以旧数据一律归到它的厂商。
QString kindOf(const QVariantMap& profile) {
    const QString kind = profile.value("kind").toString();
    if (!kind.isEmpty()) return kind;
    const QString provider = profile.value("provider").toString();
    return LlmProtocolRegistry::isKnownProvider(provider)
        ? provider : QStringLiteral("openai");
}

bool isCustomProfile(const QVariantMap& profile) {
    return kindOf(profile) == QLatin1String("custom");
}

// provider 与 apiProtocol 必须成对自洽：以协议为准回填厂商（协议决定线格式，
// 也决定要不要 key、读哪个环境变量）；协议缺失或陌生（旧 profile 根本没这个
// 字段）则回落该厂商的默认协议。
const LlmProtocolDescriptor* normalizeEndpoint(QVariantMap& profile) {
    const auto* d = LlmProtocolRegistry::resolve(
        profile.value("apiProtocol").toString(),
        profile.value("provider").toString());
    profile["provider"] = d->provider;
    profile["apiProtocol"] = d->key;
    return d;
}

QVariantMap makeProfile(const QString& provider, const QString& displayName,
                        const QString& baseUrl, const QString& model,
                        const QString& apiKey, const QStringList& models = {}) {
    const LlmProtocolDescriptor* defaults = defaultsForProvider(provider);
    QVariantMap p;
    p["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    p["displayName"] = displayName;
    p["kind"] = defaults->provider;
    p["provider"] = defaults->provider;
    p["apiProtocol"] = defaults->key;
    p["baseUrl"] = baseUrl;
    p["model"] = model;
    p["apiKey"] = apiKey;

    // 模型列表不再按厂商预设回填：只保留调用方显式给出的模型。用户手填的模型
    // 是唯一来源，预填官方列表只会让人先删后填。
    QStringList mList = models;
    if (!model.isEmpty() && !mList.contains(model)) {
        mList.prepend(model);
    }
    p["models"] = mList;
    return p;
}

// 空白配置：一条 OpenAI 兼容（openai-chat）但完全不预填端点与模型的 profile。
// 与 makeProfile 分开的理由就是"不预设"——任何一处复用 makeProfile 都会把
// api.openai.com 和四个官方模型带回来。
QVariantMap makeCustomProfile(const QString& displayName) {
    const LlmProtocolDescriptor* defaults = defaultsForProvider(QStringLiteral("openai"));
    QVariantMap p;
    p["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    p["displayName"] = displayName;
    p["kind"] = QStringLiteral("custom");
    p["provider"] = defaults->provider;
    p["apiProtocol"] = defaults->key;
    p["baseUrl"] = QString();
    p["model"] = QString();
    p["apiKey"] = QString();
    p["models"] = QStringList();
    return p;
}

// v1 → v2：模型列表改由用户手动维护，升级时清空存量 profile 的模型列表与当前
// 模型，让每条配置都回到「需手填模型」的空态。只改 blob 里的这两个键，apiKey
// 仍是加密态、不经过本进程解密；由 load() 依版本号调用，只跑一次。
void clearProfileModelsOnUpgrade() {
    const QByteArray raw = SettingsRepository::value(kProfilesKey).toString().toUtf8();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) return;

    QJsonArray arr = doc.array();
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject profile = arr.at(i).toObject();
        profile["models"] = QJsonArray();
        profile["model"] = QString();
        arr[i] = profile;
    }
    QString error;
    if (!SettingsRepository::writeValue(
            kProfilesKey,
            QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)),
            &error))
        qCWarning(lcSettings) << "Failed to migrate model profile list:" << error;
}

// v2 → v3/v4：contextTokens 默认值先由 8192 上调、再收敛为保守的 128000。存量
// 配置里若正好是**某一版旧默认值**（8192 或短暂存在的 300000）就归一到当前默认
// ——这类值不是用户的主动选择，而且通用页的输入框已移除、用户再也改不到它；
// 真正的自定义值（其它任何数字）原样保留，不覆盖用户意图。
void upgradeContextTokenDefault() {
    const QVariant stored = SettingsRepository::value("contextTokens");
    if (!stored.isValid()) return;                    // 未写过：新默认自然生效
    const int value = stored.toInt();
    const bool wasSomeDefault = (value == kLegacyContextTokensV2
                                 || value == kLegacyContextTokensV3);
    if (!wasSomeDefault) return;                      // 用户自定义值：不动
    QString error;
    if (!SettingsRepository::writeValue("contextTokens", AIAgent::kDefaultContextTokens,
                                        &error))
        qCWarning(lcSettings) << "Failed to migrate context token default:" << error;
}

#ifdef Q_OS_WIN

// API keys are encrypted at rest with the Windows DPAPI (per-user) instead
// of being written as plain text. Stored values carry a "dpapi:v1:" prefix;
// anything else is legacy plain text and is migrated on load.
const QLatin1String kEncPrefix("dpapi:v1:");

QString protectApiKey(const QString& plain) {
    if (plain.isEmpty()) return plain;

    const QByteArray utf8 = plain.toUtf8();
    DATA_BLOB in{};
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(utf8.constData()));
    in.cbData = static_cast<DWORD>(utf8.size());

    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"NexusAI.apiKey", nullptr, nullptr, nullptr,
                          0, &out)) {
        // Never fall back to storing the plain text: return an empty string
        // so the key is dropped from persistence instead. The in-memory
        // profile keeps working for this session; after a restart the user
        // re-enters the key or relies on the env-var fallback.
        qWarning() << "DPAPI encryption failed; API key will not be persisted";
        return QString();
    }
    const QByteArray blob(reinterpret_cast<const char*>(out.pbData),
                          static_cast<int>(out.cbData));
    LocalFree(out.pbData);
    return QString(kEncPrefix) + QString::fromLatin1(blob.toBase64());
}

QString unprotectApiKey(const QString& stored) {
    if (!stored.startsWith(kEncPrefix)) return stored; // legacy plain text
    const QByteArray blob = QByteArray::fromBase64(
        stored.mid(kEncPrefix.size()).toLatin1());

    DATA_BLOB in{};
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(blob.constData()));
    in.cbData = static_cast<DWORD>(blob.size());

    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) {
        return QString(); // undecryptable (e.g. different user) -> treat as unset
    }
    const QByteArray plain(reinterpret_cast<const char*>(out.pbData),
                           static_cast<int>(out.cbData));
    LocalFree(out.pbData);
    return QString::fromUtf8(plain);
}

#else

// Non-Windows platforms: keys stay plain text in QSettings; prefer env vars.
QString protectApiKey(const QString& plain) { return plain; }
QString unprotectApiKey(const QString& stored) { return stored; }

#endif // Q_OS_WIN

} // namespace

SettingsService* SettingsService::s_instance = nullptr;

SettingsService* SettingsService::instance() {
    if (!s_instance) s_instance = new SettingsService(nullptr);
    return s_instance;
}

SettingsService* SettingsService::create(QQmlEngine*, QJSEngine*) {
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}

SettingsService::SettingsService(QObject* parent) : QObject(parent) {
    load();
}

QVariantMap SettingsService::activeProfile() const {
    for (const QVariant& entry : m_profiles) {
        const QVariantMap p = entry.toMap();
        if (p.value("id").toString() == m_activeId)
            return p;
    }
    return {};
}

bool SettingsService::persistValue(const QString& key, const QVariant& value) {
    QString error;
    if (SettingsRepository::writeValue(key, value, &error)) return true;

    qCWarning(lcSettings) << "Failed to persist setting" << key << error;
    emit persistenceError(tr("设置未能保存，请检查存储权限或磁盘空间后重试。"));
    return false;
}

void SettingsService::load() {
    // L2: schema 版本迁移。docs/25 E11：原 SettingsSchema::migrate 空钩子已
    // 删除——带策略的迁移由下方具名函数按版本区间直接调用；版本戳写回存储，
    // 后续迁移可据此判断起点。
    const int storedVersion = SettingsRepository::value(QStringLiteral("schemaVersion"),
        SettingsSchema::defaultValue(QStringLiteral("schemaVersion"))).toInt();
    if (storedVersion < SettingsSchema::kSchemaVersion) {
        // profile 存在 modelProfiles blob 里（不在顶层键注册表），清空存量
        // 模型的迁移因此落在这里。
        if (storedVersion < 2) clearProfileModelsOnUpgrade();
        // v3/v4：contextTokens 旧默认值（kLegacyContextTokensV2/V3）归一为
        // 当前默认。
        if (storedVersion < 4) upgradeContextTokenDefault();
        QString error;
        if (!SettingsRepository::writeValue("schemaVersion", SettingsSchema::kSchemaVersion,
                                            &error))
            qCWarning(lcSettings) << "Failed to persist settings schema version:" << error;
    }

    // ── Profiles (JSON list; apiKeys re-encrypted on persist) ──
    const QByteArray raw = SettingsRepository::value(kProfilesKey).toString().toUtf8();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error == QJsonParseError::NoError && doc.isArray()) {
        for (const auto& entry : doc.array()) {
            QVariantMap p = entry.toObject().toVariantMap();
            p["apiKey"] = unprotectApiKey(p.value("apiKey").toString());
            // 厂商与协议都不认识的 profile 是损坏数据，沿用旧行为直接丢弃。
            if (!LlmProtocolRegistry::isKnownProvider(p.value("provider").toString())
                    && !LlmProtocolRegistry::find(p.value("apiProtocol").toString()))
                continue;
            // 旧 profile 没有 apiProtocol：在这里按厂商补齐，之后运行侧只认这个键。
            normalizeEndpoint(p);
            // kind 同理补齐（放在归一之后：normalizeEndpoint 可能按协议改写厂商）。
            p["kind"] = kindOf(p);

            // 模型列表完全由用户维护：这里既不按厂商预设回填，也不把当前模型塞
            // 进列表——否则刚清空的列表会被立刻灌回一项。
            m_profiles.append(p);
        }
    }
    if (m_profiles.isEmpty())
        m_profiles = migrateLegacy();

    m_activeId = SettingsRepository::value(kActiveKey).toString();
    if (activeProfile().isEmpty() && !m_profiles.isEmpty())
        m_activeId = m_profiles.first().toMap().value("id").toString();

    // Legacy env fallback for the active profile's key
    QVariantMap active = activeProfile();
    if (!active.isEmpty() && active.value("apiKey").toString().isEmpty()) {
        // 要不要读环境变量、读哪一个，都由协议描述符给出（本地协议没有
        // key 概念，envApiKeyVar 为空即跳过）。
        const QString envVar = LlmProtocolRegistry::resolve(
            active.value("apiProtocol").toString(),
            active.value("provider").toString())->envApiKeyVar;
        const QString envKey = envVar.isEmpty()
            ? QString()
            : qEnvironmentVariable(envVar.toUtf8().constData());
        if (!envKey.isEmpty()) {
            active["apiKey"] = envKey;
            for (int i = 0; i < m_profiles.size(); ++i) {
                if (m_profiles.at(i).toMap().value("id").toString() == m_activeId) {
                    m_profiles[i] = active;
                    break;
                }
            }
        }
    }

    m_contextTokens = SettingsSchema::clampInt(
        QStringLiteral("contextTokens"),
        SettingsRepository::value(QStringLiteral("contextTokens"),
            SettingsSchema::defaultValue(QStringLiteral("contextTokens"))).toInt());
    m_maxSteps = SettingsSchema::clampInt(
        QStringLiteral("agent/maxSteps"),
        SettingsRepository::value(QStringLiteral("agent/maxSteps"),
            SettingsSchema::defaultValue(QStringLiteral("agent/maxSteps"))).toInt());
    m_taskTimeoutMinutes = SettingsSchema::clampInt(
        QStringLiteral("agent/taskTimeoutMinutes"),
        SettingsRepository::value(QStringLiteral("agent/taskTimeoutMinutes"),
            SettingsSchema::defaultValue(QStringLiteral("agent/taskTimeoutMinutes"))).toInt());

    m_allowFullDiskRead = SettingsRepository::value(QStringLiteral("security/allowFullDiskRead"),
        SettingsSchema::defaultValue(QStringLiteral("security/allowFullDiskRead"))).toBool();
    m_allowProjectWrite = SettingsRepository::value(QStringLiteral("security/allowProjectWrite"),
        SettingsSchema::defaultValue(QStringLiteral("security/allowProjectWrite"))).toBool();
    m_localOnlyMode = SettingsRepository::value(QStringLiteral("security/localOnlyMode"),
        SettingsSchema::defaultValue(QStringLiteral("security/localOnlyMode"))).toBool();
    m_allowLocalNetworkFetch = SettingsRepository::value(
        QStringLiteral("security/allowLocalNetworkFetch"),
        SettingsSchema::defaultValue(QStringLiteral("security/allowLocalNetworkFetch"))).toBool();
    m_embeddingModel = SettingsRepository::value(QStringLiteral("embedding/model"),
        SettingsSchema::defaultValue(QStringLiteral("embedding/model"))).toString();
    m_embeddingBaseUrl = SettingsRepository::value(QStringLiteral("embedding/baseUrl"),
        SettingsSchema::defaultValue(QStringLiteral("embedding/baseUrl"))).toString();
    // 外部向量模型（任务 #16）：provider 枚举夹紧（同 webSearch/provider
    // 的 load 侧防御，坏值回落 schema 默认）+ DPAPI 解密的 API key。
    m_embeddingProvider = SettingsRepository::value(QStringLiteral("embedding/provider"),
        SettingsSchema::defaultValue(QStringLiteral("embedding/provider"))).toString();
    if (!SettingsSchema::options(QStringLiteral("embedding/provider"))
             .contains(m_embeddingProvider))
        m_embeddingProvider = SettingsSchema::defaultValue(
            QStringLiteral("embedding/provider")).toString();
    m_embeddingApiKey = unprotectApiKey(SettingsRepository::value(
        QStringLiteral("embedding/apiKey"),
        SettingsSchema::defaultValue(QStringLiteral("embedding/apiKey"))).toString());
    m_systemPromptOverride = SettingsRepository::value(
        QStringLiteral("agent/systemPromptOverride"),
        SettingsSchema::defaultValue(QStringLiteral("agent/systemPromptOverride"))).toString();
    // logging/level is Logger::install()'s key; the env override still wins
    // there, so load() only mirrors the stored value (install() already
    // applied the effective level before any UI exists).
    m_logLevel = SettingsRepository::value(QStringLiteral("logging/level"),
        SettingsSchema::defaultValue(QStringLiteral("logging/level"))).toString().trimmed().toLower();
    if (!SettingsSchema::options(QStringLiteral("logging/level")).contains(m_logLevel))
        m_logLevel = SettingsSchema::defaultValue(QStringLiteral("logging/level")).toString();
    m_logDirectory = SettingsRepository::value(QStringLiteral("logging/directory"),
        SettingsSchema::defaultValue(QStringLiteral("logging/directory"))).toString().trimmed();
    m_logMaxFileSizeMb = SettingsSchema::clampInt(QStringLiteral("logging/maxFileSizeMb"),
        SettingsRepository::value(QStringLiteral("logging/maxFileSizeMb"),
            SettingsSchema::defaultValue(QStringLiteral("logging/maxFileSizeMb"))).toInt());
    m_logRetentionFiles = SettingsSchema::clampInt(QStringLiteral("logging/retentionFiles"),
        SettingsRepository::value(QStringLiteral("logging/retentionFiles"),
            SettingsSchema::defaultValue(QStringLiteral("logging/retentionFiles"))).toInt());
    m_webSearchProvider = SettingsRepository::value(QStringLiteral("webSearch/provider"),
        SettingsSchema::defaultValue(QStringLiteral("webSearch/provider"))).toString();
    if (!SettingsSchema::options(QStringLiteral("webSearch/provider"))
             .contains(m_webSearchProvider))
        m_webSearchProvider = SettingsSchema::defaultValue(
            QStringLiteral("webSearch/provider")).toString();
    // API key is stored encrypted (DPAPI) - empty if not set or decryption failed.
    m_webSearchApiKey = unprotectApiKey(SettingsRepository::value(
        QStringLiteral("webSearch/apiKey"),
        SettingsSchema::defaultValue(QStringLiteral("webSearch/apiKey"))).toString());
    // docs/20 §5.3 (C3): SearXNG instance URL; plain text (not a secret),
    // empty = not configured.
    m_webSearchSearxBaseUrl = SettingsRepository::value(
        QStringLiteral("webSearch/searxBaseUrl"),
        SettingsSchema::defaultValue(QStringLiteral("webSearch/searxBaseUrl"))).toString().trimmed();
    m_episodicMemoryEnabled = SettingsRepository::value(
        QStringLiteral("memory/episodicMemoryEnabled"),
        SettingsSchema::defaultValue(QStringLiteral("memory/episodicMemoryEnabled"))).toBool();
    // docs/23 G3: retention days (0 = keep forever), clamped by the schema.
    m_auditRetentionDays = SettingsSchema::clampInt(QStringLiteral("memory/auditRetentionDays"),
        SettingsRepository::value(QStringLiteral("memory/auditRetentionDays"),
            SettingsSchema::defaultValue(QStringLiteral("memory/auditRetentionDays"))).toInt());
    m_tokenRetentionDays = SettingsSchema::clampInt(QStringLiteral("memory/tokenRetentionDays"),
        SettingsRepository::value(QStringLiteral("memory/tokenRetentionDays"),
            SettingsSchema::defaultValue(QStringLiteral("memory/tokenRetentionDays"))).toInt());
    // docs/23 D1: startup auto-backup + rotation.
    m_backupOnStartup = SettingsRepository::value(
        QStringLiteral("memory/backupOnStartup"),
        SettingsSchema::defaultValue(QStringLiteral("memory/backupOnStartup"))).toBool();
    m_backupKeepCount = SettingsSchema::clampInt(QStringLiteral("memory/backupKeepCount"),
        SettingsRepository::value(QStringLiteral("memory/backupKeepCount"),
            SettingsSchema::defaultValue(QStringLiteral("memory/backupKeepCount"))).toInt());
    // docs/16 §7.3: quota settings (flat keys; 0 = unlimited for int quotas).
    // docs/25 E8：读取经 SettingsSchema::intSetting/boolSetting，与
    // AIAgent::checkQuota 共用同一实现（原先两处各写一份三段式样板）。
    m_dailyTokenQuota = SettingsSchema::intSetting(QStringLiteral("dailyTokenQuota"));
    m_dailyCostQuotaCents = SettingsSchema::intSetting(QStringLiteral("dailyCostQuotaCents"));
    m_monthlyTokenQuota = SettingsSchema::intSetting(QStringLiteral("monthlyTokenQuota"));
    m_monthlyCostQuotaCents = SettingsSchema::intSetting(QStringLiteral("monthlyCostQuotaCents"));
    m_strictQuotaMode = SettingsSchema::boolSetting(QStringLiteral("strictQuotaMode"));
    m_defaultPermissionMode = SettingsRepository::value(
        QStringLiteral("agent/defaultPermissionMode"),
        SettingsSchema::defaultValue(QStringLiteral("agent/defaultPermissionMode"))).toString();
    if (!SettingsSchema::options(QStringLiteral("agent/defaultPermissionMode"))
             .contains(m_defaultPermissionMode))
        m_defaultPermissionMode = SettingsSchema::defaultValue(
            QStringLiteral("agent/defaultPermissionMode")).toString();
    m_activeProjectId = SettingsRepository::value(QStringLiteral("projects/activeId"),
        SettingsSchema::defaultValue(QStringLiteral("projects/activeId"))).toString();
    m_readDenyList = SettingsRepository::value(QStringLiteral("security/readDenyList"),
        SettingsSchema::defaultValue(QStringLiteral("security/readDenyList"))).toList();
}

// Single-provider layout (pre-0.3): provider, apiKey, <provider>/baseUrl,
// <provider>/model -> one profile per provider that carried configuration.
QVariantList SettingsService::migrateLegacy() {
    const QString legacyProvider = [&]() {
        const QString p = SettingsRepository::value("provider", "openai").toString();
        return LlmProtocolRegistry::isKnownProvider(p) ? p : QStringLiteral("openai");
    }();
    const QString legacyKey = unprotectApiKey(SettingsRepository::value("apiKey").toString());

    QVariantList out;
    for (const QString& provider : LlmProtocolRegistry::providers()) {
        const bool isActiveProvider = (provider == legacyProvider);
        const bool hasScopedValues = SettingsRepository::contains(provider + "/baseUrl");
        if (!isActiveProvider && !hasScopedValues) continue;

        const LlmProtocolDescriptor* defaults = defaultsForProvider(provider);
        out.append(makeProfile(
            provider,
            providerDisplayName(provider),
            SettingsRepository::value(provider + "/baseUrl", defaults->defaultBaseUrl).toString(),
            SettingsRepository::value(provider + "/model", QString()).toString(),
            defaults->local ? QString() : legacyKey));
    }
    if (out.isEmpty()) {
        const LlmProtocolDescriptor* defaults = defaultsForProvider(legacyProvider);
        out.append(makeProfile(legacyProvider, providerDisplayName(legacyProvider),
                               defaults->defaultBaseUrl, QString(), legacyKey));
    }

    // docs/24 H10: the legacy top-level apiKey was stored in PLAINTEXT —
    // once it has been folded into an encrypted profile, nothing may remain
    // in the registry. "provider" folded into the profile too.
    SettingsRepository::removeBatch({QStringLiteral("apiKey"),
                                     QStringLiteral("provider")});

    qInfo() << "Migrated legacy provider settings into" << out.size() << "model profile(s)";
    return out;
}

bool SettingsService::persist() {
    QJsonArray arr;
    for (const QVariant& entry : m_profiles) {
        QVariantMap p = entry.toMap();
        const QString apiKey = p.value("apiKey").toString();
        const QString protectedKey = protectApiKey(apiKey);
        if (!apiKey.isEmpty() && protectedKey.isEmpty()) {
            emit persistenceError(tr("API 密钥加密失败，模型配置未保存。"));
            return false;
        }
        p["apiKey"] = protectedKey;
        arr.append(QJsonObject::fromVariantMap(p));
    }

    const QVariantMap entries = {
        {QString::fromLatin1(kProfilesKey),
         QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact))},
        {QString::fromLatin1(kActiveKey), m_activeId}
    };
    QString error;
    if (SettingsRepository::writeBatch(entries, &error)) return true;

    qCWarning(lcSettings) << "Failed to persist model profiles:" << error;
    emit persistenceError(tr("模型配置未能保存。请检查设置存储权限或磁盘空间后重试。"));
    return false;
}

QString SettingsService::addProfile(const QString& kind) {
    QVariantMap profile;
    if (kind == QLatin1String("custom")) {
        profile = makeCustomProfile(tr("自定义网关"));
    } else {
        const QString vendor = LlmProtocolRegistry::isKnownProvider(kind)
            ? kind : QStringLiteral("openai");
        const LlmProtocolDescriptor* defaults = defaultsForProvider(vendor);
        profile = makeProfile(vendor, tr("新配置"),
            defaults->defaultBaseUrl, QString(), QString());
    }

    const QVariantList previousProfiles = m_profiles;
    const QString previousActiveId = m_activeId;
    const QString id = profile.value("id").toString();
    m_profiles.append(profile);
    if (m_profiles.size() == 1)
        m_activeId = id;

    if (!persist()) {
        m_profiles = previousProfiles;
        m_activeId = previousActiveId;
        return {};
    }
    emit profilesChanged();
    if (m_activeId == id)
        emit activeProfileChanged();
    return id;
}

void SettingsService::updateProfile(const QString& id, const QVariantMap& fields) {
    for (int i = 0; i < m_profiles.size(); ++i) {
        QVariantMap p = m_profiles.at(i).toMap();
        if (p.value("id").toString() != id) continue;

        const QString providerBefore = p.value("provider").toString();

        for (auto it = fields.constBegin(); it != fields.constEnd(); ++it) {
            if (it.key() == QLatin1String("id")) continue; // id is immutable
            if (it.key() == QLatin1String("provider")
                    && !LlmProtocolRegistry::isKnownProvider(it.value().toString()))
                continue;
            if (it.key() == QLatin1String("kind") && !isKnownKind(it.value().toString()))
                continue;   // 未登记的来源标记不接受，避免脏 kind 让预设回填失焦
            if (it.key() == QLatin1String("apiProtocol")
                    && !LlmProtocolRegistry::find(it.value().toString()))
                continue;   // 未注册的协议不接受
            if (it.key() == QLatin1String("model")) {
                // 模型名带空格会让"当前模型"和列表项对不上（列表项是 trim 过的），
                // chip 高亮与聊天侧选择器都会错位。
                p[it.key()] = it.value().toString().trimmed();
                continue;
            }
            p[it.key()] = it.value();
        }
        // 设置页的厂商芯片统一提交 kind（"openai" | "anthropic" | "ollama" |
        // "custom"）：非 custom 时 kind 就是厂商键，custom 归到 openai 厂商——
        // 自定义网关骑在 openai-chat 上，不是第五种线协议。UI 因此不必再各自
        // 记一份"点这个芯片意味着哪个厂商"。
        const QString submittedKind = fields.value(QStringLiteral("kind")).toString();
        if (submittedKind == QLatin1String("custom"))
            p["provider"] = QStringLiteral("openai");
        else if (isKnownKind(submittedKind))
            p["provider"] = submittedKind;

        // 两个轴的优先级要看这次提交了什么：UI 的来源芯片只发 kind（或旧调用
        // 发 provider），此时协议要跟着重置成该厂商的默认协议（否则协议字段会
        // 反过来把厂商改写回去，换厂商就成了空操作）；显式换协议时则以协议为准
        // 回填厂商。被上面丢弃的非法值不算一次提交，不能顺带重置协议。
        const bool kindSubmitted = isKnownKind(submittedKind);
        if (!fields.contains(QStringLiteral("apiProtocol"))
                && (fields.contains(QStringLiteral("provider")) || kindSubmitted)) {
            p["apiProtocol"] = defaultsForProvider(p.value("provider").toString())->key;
        }
        const LlmProtocolDescriptor* endpoint = normalizeEndpoint(p);

        // 端点拥有的两项：baseUrl 与厂商归属。当前模型与模型列表不再由端点回填
        // （那是用户在列表里手填和维护的），唯一例外是换厂商时清空二者——上一家的
        // 模型名在新端点上用不了，留着只会让每次请求都撞服务端报错。
        const bool vendorChanged = endpoint->provider != providerBefore;
        // 自定义配置是"端点三项归用户"的那一类：任何预设回填都会把用户填的网关
        // 地址换成 OpenAI 官方值，所以 baseUrl 重置整段跳过。
        const bool customProfile = isCustomProfile(p);

        // 换厂商 = 换端点家族：上一家的当前模型与模型列表在新端点上都用不了，
        // 又不再有预设可回填，于是清空让用户重填（页脚会提示还需填写模型名）。
        // 同一厂商换协议不动模型与列表：仍是同一个网关，手填的名字照旧有效。
        if (vendorChanged && !customProfile) {
            if (!fields.contains(QStringLiteral("baseUrl")))
                p["baseUrl"] = endpoint->defaultBaseUrl;
            // 本次显式提交的字段不覆盖，只清掉上一家的遗留（模型与列表一起）。
            if (!fields.contains(QStringLiteral("model"))) p["model"] = QString();
            if (!fields.contains(QStringLiteral("models"))) p["models"] = QStringList();
        }

        // 显式提交模型名时把它并入列表：聊天侧模型选择器读的是 models，缺了项就
        // 等于把刚选的模型藏起来。不做无条件补写——否则清空后的列表会被立刻灌回
        // 当前模型，等于把"不再预填"又绕过去了。
        if (fields.contains(QStringLiteral("model"))) {
            const QString current = p.value("model").toString();
            QStringList models = p.value("models").toStringList();
            if (!current.isEmpty() && !models.contains(current)) {
                models.prepend(current);
                p["models"] = models;
            }
        }
        const QVariantList previousProfiles = m_profiles;
        m_profiles[i] = p;
        if (!persist()) {
            m_profiles = previousProfiles;
            return;
        }
        emit profilesChanged();
        if (id == m_activeId)
            emit activeProfileChanged();
        return;
    }
}

void SettingsService::removeProfile(const QString& id) {
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles.at(i).toMap().value("id").toString() != id) continue;

        const QVariantList previousProfiles = m_profiles;
        const QString previousActiveId = m_activeId;
        m_profiles.removeAt(i);
        if (m_activeId == id) {
            m_activeId = m_profiles.isEmpty()
                ? QString() : m_profiles.first().toMap().value("id").toString();
        }
        if (!persist()) {
            m_profiles = previousProfiles;
            m_activeId = previousActiveId;
            return;
        }
        emit profilesChanged();
        emit activeProfileChanged();
        return;
    }
}

void SettingsService::setActiveProfile(const QString& id) {
    if (id == m_activeId) return;
    bool found = false;
    for (const QVariant& entry : m_profiles) {
        if (entry.toMap().value("id").toString() == id) { found = true; break; }
    }
    if (!found) return;

    const QString previousActiveId = m_activeId;
    m_activeId = id;
    if (!persist()) {
        m_activeId = previousActiveId;
        return;
    }
    emit activeProfileChanged();
}

void SettingsService::selectModelForProfile(const QString& id, const QString& model) {
    const QString targetId = id.isEmpty() ? m_activeId : id;
    if (targetId.isEmpty() || model.trimmed().isEmpty()) return;

    for (int i = 0; i < m_profiles.size(); ++i) {
        QVariantMap p = m_profiles.at(i).toMap();
        if (p.value("id").toString() != targetId) continue;

        if (p.value("model").toString() == model) return;

        p["model"] = model;
        QStringList models = p.value("models").toStringList();
        if (!models.contains(model)) {
            models.prepend(model);
            p["models"] = models;
        }
        const QVariantList previousProfiles = m_profiles;
        m_profiles[i] = p;
        if (!persist()) {
            m_profiles = previousProfiles;
            return;
        }
        emit profilesChanged();
        if (targetId == m_activeId) {
            emit activeProfileChanged();
        }
        return;
    }
}

void SettingsService::addModelToProfile(const QString& id, const QString& model) {
    const QString targetId = id.isEmpty() ? m_activeId : id;
    const QString trimmedModel = model.trimmed();
    if (targetId.isEmpty() || trimmedModel.isEmpty()) return;

    for (int i = 0; i < m_profiles.size(); ++i) {
        QVariantMap p = m_profiles.at(i).toMap();
        if (p.value("id").toString() != targetId) continue;

        QStringList models = p.value("models").toStringList();
        if (!models.contains(trimmedModel)) {
            models.append(trimmedModel);
            p["models"] = models;
            // 列表从此不再预填，新建配置的当前模型是空的：刚加进来的第一个模型
            // 就是唯一可用的那个，顺手设为当前，免得用户加完还卡在"还需填写：模型名"。
            // 这是用户自己敲进来的值，不是厂商预设。
            if (p.value("model").toString().isEmpty())
                p["model"] = trimmedModel;
            const QVariantList previousProfiles = m_profiles;
            m_profiles[i] = p;
            if (!persist()) {
                m_profiles = previousProfiles;
                return;
            }
            emit profilesChanged();
            if (targetId == m_activeId) {
                emit activeProfileChanged();
            }
        }
        return;
    }
}

void SettingsService::removeModelFromProfile(const QString& id, const QString& model) {
    const QString targetId = id.isEmpty() ? m_activeId : id;
    if (targetId.isEmpty() || model.isEmpty()) return;

    for (int i = 0; i < m_profiles.size(); ++i) {
        QVariantMap p = m_profiles.at(i).toMap();
        if (p.value("id").toString() != targetId) continue;

        QStringList models = p.value("models").toStringList();
        if (models.contains(model)) {
            models.removeAll(model);
            p["models"] = models;

            // If the deleted model was current, switch to first remaining
            if (p.value("model").toString() == model) {
                p["model"] = models.isEmpty() ? QString() : models.first();
            }

            const QVariantList previousProfiles = m_profiles;
            m_profiles[i] = p;
            if (!persist()) {
                m_profiles = previousProfiles;
                return;
            }
            emit profilesChanged();
            if (targetId == m_activeId) {
                emit activeProfileChanged();
            }
        }
        return;
    }
}

QString SettingsService::providerDisplayName(const QString& provider) const {
    if (provider == QLatin1String("openai")) return tr("OpenAI 兼容");
    if (provider == QLatin1String("ollama")) return tr("Ollama 本地");
    // 任务 #24：gemini 必须显式分支——兜底返回 "Claude"，不加的话
    // Gemini 配置的芯片会显示成 Claude。
    if (provider == QLatin1String("gemini")) return tr("Gemini");
    return tr("Claude");
}

void SettingsService::setContextTokens(int tokens) {
    const int clamped = SettingsSchema::clampInt(QStringLiteral("contextTokens"), tokens);
    if (m_contextTokens == clamped || !persistValue(QStringLiteral("contextTokens"), clamped))
        return;
    m_contextTokens = clamped;
    emit contextTokensChanged();
}

// 步数预算：即时保存；生效路径在 main.cpp（maxStepsChanged → applySettings
// 把值推给模板 agent 与各标签 agent）。
void SettingsService::setMaxSteps(int steps) {
    const int clamped = SettingsSchema::clampInt(QStringLiteral("agent/maxSteps"), steps);
    if (m_maxSteps == clamped
            || !persistValue(QStringLiteral("agent/maxSteps"), clamped)) return;
    m_maxSteps = clamped;
    emit maxStepsChanged();
}

void SettingsService::setTaskTimeoutMinutes(int minutes) {
    const int clamped = SettingsSchema::clampInt(
        QStringLiteral("agent/taskTimeoutMinutes"), minutes);
    if (m_taskTimeoutMinutes == clamped
            || !persistValue(QStringLiteral("agent/taskTimeoutMinutes"), clamped)) return;
    m_taskTimeoutMinutes = clamped;
    emit taskTimeoutMinutesChanged();
}


void SettingsService::setAllowFullDiskRead(bool enabled) {
    if (m_allowFullDiskRead == enabled
            || !persistValue(QStringLiteral("security/allowFullDiskRead"), enabled)) return;
    m_allowFullDiskRead = enabled;
    Activity::usage(Activity::Kind::User, QStringLiteral("settings.allowFullDiskRead"),
                    enabled ? QStringLiteral("on") : QStringLiteral("off"));
    emit allowFullDiskReadChanged();
}

// docs/17 P1-4: 项目目录写入放行。即时生效路径在 main.cpp（applySettings
// 订阅 allowProjectWriteChanged → Sandbox::setProjectWriteEnabled）。
void SettingsService::setAllowProjectWrite(bool enabled) {
    if (m_allowProjectWrite == enabled
            || !persistValue(QStringLiteral("security/allowProjectWrite"), enabled)) return;
    m_allowProjectWrite = enabled;
    Activity::usage(Activity::Kind::User, QStringLiteral("settings.allowProjectWrite"),
                    enabled ? QStringLiteral("on") : QStringLiteral("off"));
    emit allowProjectWriteChanged();
}

// Batch 14 (SEC-1401): same silent-repeat pattern as allowFullDiskRead.
// The actual tool gating lives in main.cpp (applyLocalOnlyGate), wired to
// localOnlyModeChanged for immediate effect.
void SettingsService::setLocalOnlyMode(bool enabled) {
    if (m_localOnlyMode == enabled
            || !persistValue(QStringLiteral("security/localOnlyMode"), enabled)) return;
    m_localOnlyMode = enabled;
    Activity::usage(Activity::Kind::User, QStringLiteral("settings.localOnlyMode"),
                    enabled ? QStringLiteral("on") : QStringLiteral("off"));
    emit localOnlyModeChanged();
}

// Batch 20 (FEAT-2002): SSRF escape hatch for http_fetch - same silent-repeat
// pattern as allowFullDiskRead. Consumed via main.cpp applySettings →
// NetworkFetchTool::setAllowPrivateTargets.
void SettingsService::setAllowLocalNetworkFetch(bool enabled) {
    if (m_allowLocalNetworkFetch == enabled
            || !persistValue(QStringLiteral("security/allowLocalNetworkFetch"), enabled)) return;
    m_allowLocalNetworkFetch = enabled;
    Activity::usage(Activity::Kind::User, QStringLiteral("settings.allowLocalNetworkFetch"),
                    enabled ? QStringLiteral("on") : QStringLiteral("off"));
    emit allowLocalNetworkFetchChanged();
}

void SettingsService::setEmbeddingModel(const QString& model) {
    const QString trimmed = model.trimmed();
    if (trimmed.isEmpty() || m_embeddingModel == trimmed
            || !persistValue(QStringLiteral("embedding/model"), trimmed)) return;
    m_embeddingModel = trimmed;
    Activity::usage(Activity::Kind::User, QStringLiteral("settings.embeddingModel"),
                    trimmed);
    emit embeddingModelChanged();
}

// Batch 25 (G11): endpoint for the embedding requests; vectors depend on
// the model only, so a change here never triggers a re-embed.
void SettingsService::setEmbeddingBaseUrl(const QString& url) {
    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty() || m_embeddingBaseUrl == trimmed
            || !persistValue(QStringLiteral("embedding/baseUrl"), trimmed)) return;
    m_embeddingBaseUrl = trimmed;
    Activity::usage(Activity::Kind::User, QStringLiteral("settings.embeddingBaseUrl"),
                    trimmed);
    emit embeddingBaseUrlChanged();
}

// 外部向量模型（任务 #16）：合法 provider 键来自 SettingsSchema，供
// 设置 UI 与 main.cpp 的后端工厂共用（对齐 webSearchProviders）。
QStringList SettingsService::embeddingProviders() const {
    return SettingsSchema::options(QStringLiteral("embedding/provider"));
}

// 外部向量模型（任务 #16）：provider 切换。未知键拒绝持久化（对齐
// setWebSearchProvider 的 L4 守卫——持久化一个没有后端匹配的键会让
// 工厂静默回退，UI 上显示一个幻影 provider）。
void SettingsService::setEmbeddingProvider(const QString& provider) {
    const QString trimmed = provider.trimmed();
    if (trimmed.isEmpty() || m_embeddingProvider == trimmed) return;
    if (!embeddingProviders().contains(trimmed)) {
        qWarning() << "Unknown embedding provider:" << trimmed;
        return;
    }
    if (!persistValue(QStringLiteral("embedding/provider"), trimmed)) return;
    m_embeddingProvider = trimmed;
    Activity::usage(Activity::Kind::User, QStringLiteral("settings.embeddingProvider"),
                    trimmed);
    emit embeddingProviderChanged();
}

// 拦截决策（批次三设计决策 2）：纯函数实现，语义见头文件注释。
// main.cpp applyEmbeddingConfig 的后端切换与此同源（任务 #20 提取）。
bool SettingsService::embeddingUsesExternalProvider(const QString& provider,
                                                    bool localOnlyMode) {
    return provider == QLatin1String("openai") && !localOnlyMode;
}

// 外部向量模型的 API key：DPAPI 加密落盘（完全复刻 setWebSearchApiKey，
// 绝不明文持久化），空串清除已存 key；密钥本身绝不写进日志。
void SettingsService::setEmbeddingApiKey(const QString& key) {
    const QString trimmed = key.trimmed();
    if (m_embeddingApiKey == trimmed) return;
    const QString stored = protectApiKey(trimmed);
    if (!trimmed.isEmpty() && stored.isEmpty()) {
        emit persistenceError(tr("向量模型 API 密钥加密失败，未保存更改。"));
        return;
    }
    if (!persistValue(QStringLiteral("embedding/apiKey"), stored)) return;
    m_embeddingApiKey = trimmed;
    Activity::usage(Activity::Kind::User, QStringLiteral("settings.embeddingApiKey"),
                    trimmed.isEmpty() ? QStringLiteral("(cleared)") : QStringLiteral("(set)"));
    emit embeddingApiKeyChanged();
}

// Empty (after trim) clears the override and falls back to the built-in
// prompt - unlike embeddingModel, empty is a meaningful value here.
void SettingsService::setSystemPromptOverride(const QString& prompt) {
    const QString trimmed = prompt.trimmed();
    if (m_systemPromptOverride == trimmed
            || !persistValue(QStringLiteral("agent/systemPromptOverride"), trimmed)) return;
    m_systemPromptOverride = trimmed;
    Activity::usage(Activity::Kind::User, QStringLiteral("settings.systemPromptOverride"),
                    trimmed.isEmpty() ? QStringLiteral("(cleared)")
                                      : QStringLiteral("%1 chars").arg(trimmed.size()));
    emit systemPromptOverrideChanged();
}

void SettingsService::setLogLevel(const QString& level) {
    const QString normalized = level.trimmed().toLower();
    if (!SettingsSchema::options(QStringLiteral("logging/level")).contains(normalized))
        return;
    if (m_logLevel == normalized
            || !persistValue(QStringLiteral("logging/level"), normalized)) return;
    m_logLevel = normalized;
    Logger::setMinLevel(Logger::levelFromName(normalized, Logger::minLevel()));
    emit logLevelChanged();
}

QString SettingsService::logDirectory() const {
    return Logger::logDir();
}

void SettingsService::setLogDirectory(const QString& directory) {
    QString normalized = directory.trimmed();
    if (normalized.startsWith(QLatin1String("file://")))
        normalized = QUrl(normalized).toLocalFile();
    if (!normalized.isEmpty()) normalized = QDir::cleanPath(normalized);
    if (m_logDirectory == normalized
            || !persistValue(QStringLiteral("logging/directory"), normalized)) return;
    m_logDirectory = normalized;
    Logger::setLogDirectory(normalized);
    emit logDirectoryChanged();
}

void SettingsService::setLogMaxFileSizeMb(int sizeMb) {
    const int clamped = SettingsSchema::clampInt(QStringLiteral("logging/maxFileSizeMb"), sizeMb);
    if (m_logMaxFileSizeMb == clamped
            || !persistValue(QStringLiteral("logging/maxFileSizeMb"), clamped)) return;
    m_logMaxFileSizeMb = clamped;
    Logger::setMaxFileSizeMb(clamped);
    emit logMaxFileSizeMbChanged();
}

void SettingsService::setLogRetentionFiles(int count) {
    const int clamped = SettingsSchema::clampInt(QStringLiteral("logging/retentionFiles"), count);
    if (m_logRetentionFiles == clamped
            || !persistValue(QStringLiteral("logging/retentionFiles"), clamped)) return;
    m_logRetentionFiles = clamped;
    Logger::setRetentionFiles(clamped);
    emit logRetentionFilesChanged();
}

// L1: legal search provider keys for the settings UI. Kept in sync with the
// backend factory (L4); searxng was enabled by docs/20 §5.3 (C3).
QStringList SettingsService::webSearchProviders() const {
    return SettingsSchema::options(QStringLiteral("webSearch/provider"));
}

// T6 (F3): search provider ("tavily" | …). Clamped to known providers.
void SettingsService::setWebSearchProvider(const QString& provider) {
    const QString trimmed = provider.trimmed();
    if (trimmed.isEmpty() || m_webSearchProvider == trimmed) return;
    // L4: reject unknown provider keys — keeps the setting in sync with the
    // backend factory (webSearchProviders()). Without this guard the setting
    // would persist a value no backend matches, and the factory would silently
    // fall back to Tavily, making the UI show a phantom provider.
    if (!webSearchProviders().contains(trimmed)) {
        qWarning() << "Unknown web search provider:" << trimmed;
        return;
    }
    if (!persistValue(QStringLiteral("webSearch/provider"), trimmed)) return;
    m_webSearchProvider = trimmed;
    Activity::usage(Activity::Kind::User, QStringLiteral("settings.webSearchProvider"),
                    trimmed);
    emit webSearchProviderChanged();
}

// API key is encrypted at rest via DPAPI. Empty string drops the stored key.
void SettingsService::setWebSearchApiKey(const QString& key) {
    const QString trimmed = key.trimmed();
    if (m_webSearchApiKey == trimmed) return;
    // REQ-T6-05: encrypt on persistence; never log the key.
    const QString stored = protectApiKey(trimmed);
    if (!trimmed.isEmpty() && stored.isEmpty()) {
        emit persistenceError(tr("搜索 API 密钥加密失败，未保存更改。"));
        return;
    }
    if (!persistValue(QStringLiteral("webSearch/apiKey"), stored)) return;
    m_webSearchApiKey = trimmed;
    Activity::usage(Activity::Kind::User, QStringLiteral("settings.webSearchApiKey"),
                    trimmed.isEmpty() ? QStringLiteral("(cleared)") : QStringLiteral("(set)"));
    emit webSearchApiKeyChanged();
}

// docs/20 §5.3 (C3): SearXNG instance base URL. Unlike the API key, empty
// is meaningful (clears the setting → "not configured") and the value is
// not secret, so the usage event records it as-is.
void SettingsService::setWebSearchSearxBaseUrl(const QString& url) {
    const QString trimmed = url.trimmed();
    if (m_webSearchSearxBaseUrl == trimmed
            || !persistValue(QStringLiteral("webSearch/searxBaseUrl"), trimmed)) return;
    m_webSearchSearxBaseUrl = trimmed;
    Activity::usage(Activity::Kind::User, QStringLiteral("settings.webSearchSearxBaseUrl"),
                    trimmed.isEmpty() ? QStringLiteral("(cleared)") : trimmed);
    emit webSearchSearxBaseUrlChanged();
}

// T8 (F4): episodic memory toggle - same 5-step pattern as localOnlyMode.
void SettingsService::setEpisodicMemoryEnabled(bool enabled) {
    if (m_episodicMemoryEnabled == enabled
            || !persistValue(QStringLiteral("memory/episodicMemoryEnabled"), enabled)) return;
    m_episodicMemoryEnabled = enabled;
    Activity::usage(Activity::Kind::User, QStringLiteral("settings.episodicMemoryEnabled"),
                    enabled ? QStringLiteral("on") : QStringLiteral("off"));
    emit episodicMemoryEnabledChanged();
}

// docs/23 G3: retention setters — clamp via the schema range, persist,
// emit. 0 = keep forever; the prune timer in main reads these live.
void SettingsService::setAuditRetentionDays(int days) {
    const int clamped = SettingsSchema::clampInt(QStringLiteral("memory/auditRetentionDays"), days);
    if (m_auditRetentionDays == clamped
            || !persistValue(QStringLiteral("memory/auditRetentionDays"), clamped)) return;
    m_auditRetentionDays = clamped;
    emit auditRetentionDaysChanged();
}

void SettingsService::setTokenRetentionDays(int days) {
    const int clamped = SettingsSchema::clampInt(QStringLiteral("memory/tokenRetentionDays"), days);
    if (m_tokenRetentionDays == clamped
            || !persistValue(QStringLiteral("memory/tokenRetentionDays"), clamped)) return;
    m_tokenRetentionDays = clamped;
    emit tokenRetentionDaysChanged();
}

// docs/23 D1: backup settings — same 4-step pattern as the retention keys.
void SettingsService::setBackupOnStartup(bool enabled) {
    if (m_backupOnStartup == enabled
            || !persistValue(QStringLiteral("memory/backupOnStartup"), enabled)) return;
    m_backupOnStartup = enabled;
    emit backupOnStartupChanged();
}

void SettingsService::setBackupKeepCount(int count) {
    const int clamped = SettingsSchema::clampInt(QStringLiteral("memory/backupKeepCount"), count);
    if (m_backupKeepCount == clamped
            || !persistValue(QStringLiteral("memory/backupKeepCount"), clamped)) return;
    m_backupKeepCount = clamped;
    emit backupKeepCountChanged();
}

// docs/16 §7.3: quota setters. Int quotas are clamped to >= 0 (0 = unlimited);
// same 4-step pattern as setContextTokens. Flat QSettings keys
// match the SettingsSchema registration (no prefix).
void SettingsService::setDailyTokenQuota(int v) {
    const int clamped = SettingsSchema::clampInt(QStringLiteral("dailyTokenQuota"), v);
    if (m_dailyTokenQuota == clamped
            || !persistValue(QStringLiteral("dailyTokenQuota"), clamped)) return;
    m_dailyTokenQuota = clamped;
    emit dailyTokenQuotaChanged();
}

void SettingsService::setDailyCostQuotaCents(int v) {
    const int clamped = SettingsSchema::clampInt(QStringLiteral("dailyCostQuotaCents"), v);
    if (m_dailyCostQuotaCents == clamped
            || !persistValue(QStringLiteral("dailyCostQuotaCents"), clamped)) return;
    m_dailyCostQuotaCents = clamped;
    emit dailyCostQuotaCentsChanged();
}

void SettingsService::setMonthlyTokenQuota(int v) {
    const int clamped = SettingsSchema::clampInt(QStringLiteral("monthlyTokenQuota"), v);
    if (m_monthlyTokenQuota == clamped
            || !persistValue(QStringLiteral("monthlyTokenQuota"), clamped)) return;
    m_monthlyTokenQuota = clamped;
    emit monthlyTokenQuotaChanged();
}

void SettingsService::setMonthlyCostQuotaCents(int v) {
    const int clamped = SettingsSchema::clampInt(QStringLiteral("monthlyCostQuotaCents"), v);
    if (m_monthlyCostQuotaCents == clamped
            || !persistValue(QStringLiteral("monthlyCostQuotaCents"), clamped)) return;
    m_monthlyCostQuotaCents = clamped;
    emit monthlyCostQuotaCentsChanged();
}

void SettingsService::setStrictQuotaMode(bool v) {
    if (m_strictQuotaMode == v
            || !persistValue(QStringLiteral("strictQuotaMode"), v)) return;
    m_strictQuotaMode = v;
    emit strictQuotaModeChanged();
}

void SettingsService::setReadDenyList(const QVariantList& v) {
    if (m_readDenyList == v
            || !persistValue(QStringLiteral("security/readDenyList"), v)) return;
    m_readDenyList = v;
    emit readDenyListChanged();
}

void SettingsService::setActiveProjectId(const QString& id) {
    if (m_activeProjectId == id
            || !persistValue(QStringLiteral("projects/activeId"), id)) return;
    m_activeProjectId = id;
    emit activeProjectIdChanged();
}

void SettingsService::resetGeneralToDefaults() {
    // Preference items are reset by their page through PreferenceRegistry.
    setMaxSteps(SettingsSchema::defaultValue(QStringLiteral("agent/maxSteps")).toInt());
    setTaskTimeoutMinutes(SettingsSchema::defaultValue(
        QStringLiteral("agent/taskTimeoutMinutes")).toInt());
    setLogLevel(SettingsSchema::defaultValue(QStringLiteral("logging/level")).toString());
    setLogDirectory(SettingsSchema::defaultValue(QStringLiteral("logging/directory")).toString());
    setLogMaxFileSizeMb(SettingsSchema::defaultValue(QStringLiteral("logging/maxFileSizeMb")).toInt());
    setLogRetentionFiles(SettingsSchema::defaultValue(QStringLiteral("logging/retentionFiles")).toInt());
    setEmbeddingModel(SettingsSchema::defaultValue(QStringLiteral("embedding/model")).toString());
    setEmbeddingBaseUrl(SettingsSchema::defaultValue(QStringLiteral("embedding/baseUrl")).toString());
    setSystemPromptOverride(SettingsSchema::defaultValue(
        QStringLiteral("agent/systemPromptOverride")).toString());
}

void SettingsService::setDefaultPermissionMode(const QString& v) {
    if (!SettingsSchema::options(QStringLiteral("agent/defaultPermissionMode")).contains(v))
        return;
    if (m_defaultPermissionMode == v
            || !persistValue(QStringLiteral("agent/defaultPermissionMode"), v)) return;
    m_defaultPermissionMode = v;
    emit defaultPermissionModeChanged();
}

namespace {

// docs/20 §4.3（D8）：HKCU\...\Run 键的默认实现（Windows 专属）。
// 只写 HKCU（用户级，无需管理员）；值 = exe 全路径，卸载/换机时清键即可。
// 非 Windows 平台 no-op（docs/20 的 Windows 优先策略，预留扩展点）。
class RegistryAutoStart : public SettingsService::AutoStartBackend {
public:
    static constexpr const wchar_t* kRunKey =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    static constexpr const wchar_t* kValueName = L"NexusAI";

    bool isEnabled() const override {
#ifdef Q_OS_WIN
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key)
                != ERROR_SUCCESS)
            return false;
        DWORD type = 0, size = 0;
        const LSTATUS rc = RegQueryValueExW(key, kValueName, nullptr, &type,
                                            nullptr, &size);
        RegCloseKey(key);
        return rc == ERROR_SUCCESS;
#else
        return false;
#endif
    }

    bool setEnabled(bool enabled, const QString& exePath) override {
#ifdef Q_OS_WIN
        HKEY key = nullptr;
        const REGSAM access = enabled ? KEY_SET_VALUE : KEY_SET_VALUE;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, access, &key)
                != ERROR_SUCCESS)
            return false;
        LSTATUS rc;
        if (enabled) {
            const QString native = QDir::toNativeSeparators(exePath);
            // 引号包裹：Program Files 这类带空格的路径不加引号会被 Run 键
            // 解析成错误的可执行名 + 参数。
            const QString quoted = QStringLiteral("\"%1\"").arg(native);
            rc = RegSetValueExW(key, kValueName, 0, REG_SZ,
                reinterpret_cast<const BYTE*>(quoted.utf16()),
                static_cast<DWORD>((quoted.size() + 1) * sizeof(wchar_t)));
        } else {
            rc = RegDeleteValueW(key, kValueName);
            // 键本就不存在 = 已是目标状态，视为成功。
            if (rc == ERROR_FILE_NOT_FOUND) rc = ERROR_SUCCESS;
        }
        RegCloseKey(key);
        return rc == ERROR_SUCCESS;
#else
        Q_UNUSED(enabled); Q_UNUSED(exePath);
        return false;
#endif
    }
};

} // namespace

bool SettingsService::autoStart() const {
    if (m_autoStart) return m_autoStart->isEnabled();
    static RegistryAutoStart defaultBackend;
    return defaultBackend.isEnabled();
}

void SettingsService::setAutoStart(bool enabled) {
    if (autoStart() == enabled) return;
    const bool ok = m_autoStart
        ? m_autoStart->setEnabled(enabled, QCoreApplication::applicationFilePath())
        : RegistryAutoStart().setEnabled(enabled, QCoreApplication::applicationFilePath());
    if (!ok) {
        qCWarning(lcSettings) << "autoStart: registry write failed for" << enabled;
        emit persistenceError(tr("无法更新 Windows 开机自启动设置。"));
        return;
    }
    emit autoStartChanged();
}

void SettingsService::setAutoStartBackend(AutoStartBackend* backend) {
    m_autoStart = backend;
}


// Capability-table lookup: returns the context window for a protocol+model,
// or 0 when either is unknown so the caller can fall back to the global
// contextTokens. 能力表住在协议注册表的描述符里，按协议取——同一个厂商的两条
// 协议可用的模型集合不一样。
int SettingsService::modelContextWindow(const QString& apiProtocol,
                                        const QString& model) const {
    const auto* d = LlmProtocolRegistry::find(apiProtocol);
    if (!d) return 0;
    return d->contextWindows.value(model, 0);
}

// 某厂商可选的协议（UI 的协议选择行与列表标签）：{apiProtocol, label}。
// 只有一个协议时 UI 不显示这一行。默认端点与模型不从这里外发——切换端点后的
// 回填由 updateProfile 单点负责，两处各给一份默认值迟早会漂移。
QVariantList SettingsService::endpointOptions(const QString& provider) const {
    QVariantList out;
    const QString vendor = LlmProtocolRegistry::isKnownProvider(provider)
        ? provider : QStringLiteral("openai");
    for (const auto& d : LlmProtocolRegistry::protocolsOfProvider(vendor)) {
        out.append(QVariantMap{{QStringLiteral("apiProtocol"), d.key},
                               {QStringLiteral("label"), d.label}});
    }
    return out;
}

// Three-level priority resolution for the active profile's context budget:
//   1. Profile-level "contextLength" override (highest)
//   2. Capability-table contextWindow (default per model)
//   3. Global contextTokens (fallback for unknown models)
int SettingsService::resolveContextLimit() const {
    return resolveContextLimitForProfile(activeProfile());
}

int SettingsService::resolveContextLimitForProfile(const QVariantMap& profile) const {
    const QString provider = profile.value("provider").toString();
    const QString model = profile.value("model").toString();

    // Priority 1: Profile-level contextLength override.
    const QVariant profileOverride = profile.value("contextLength");
    if (profileOverride.isValid() && !profileOverride.toString().isEmpty()) {
        bool ok = false;
        const int v = profileOverride.toInt(&ok);
        if (ok && v > 0) return v;
    }

    // Priority 2: capability-table contextWindow（按协议查，同厂商两条协议的
    // 可用模型集合不同）。apiProtocol 未经 load 归一的调用方按厂商兜底。
    const QString apiProtocol = LlmProtocolRegistry::resolve(
        profile.value("apiProtocol").toString(), provider)->key;
    const int tableValue = modelContextWindow(apiProtocol, model);
    if (tableValue > 0) return tableValue;

    // Priority 3: global contextTokens (fallback).
    return m_contextTokens;
}

} // namespace NexusAI
