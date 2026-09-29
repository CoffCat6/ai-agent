#include "SettingsService.h"
#include "SettingsSchema.h"
#include "core/utils/Activity.h"

#include "core/agent/AIAgent.h"
#include "core/utils/Logger.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QUuid>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dpapi.h>
#endif

namespace NexusAI {

namespace {

struct ProviderDefaults {
    QString baseUrl;
    QString model;
    QStringList models;
    QHash<QString, int> contextWindows;  // model name → context window (tokens)
};

// No displayName in the static table: a translated name must be resolved
// through tr() at query time (providerDisplayName) so a runtime language
// switch is reflected; a static-initialized string would be frozen.
const QHash<QString, ProviderDefaults>& providerDefaults() {
    static const QHash<QString, ProviderDefaults> defaults = {
        {"openai",    {QStringLiteral("https://api.openai.com/v1"),  QStringLiteral("gpt-4o-mini"),
                       {QStringLiteral("gpt-4o-mini"), QStringLiteral("gpt-4o"), QStringLiteral("gpt-4-turbo"), QStringLiteral("o3-mini")},
                       {{QStringLiteral("gpt-4o-mini"), 128000},
                        {QStringLiteral("gpt-4o"), 128000},
                        {QStringLiteral("gpt-4-turbo"), 128000},
                        {QStringLiteral("o3-mini"), 200000}}}},
        {"anthropic", {QStringLiteral("https://api.anthropic.com"), QStringLiteral("claude-sonnet-4-5"),
                       {QStringLiteral("claude-sonnet-4-5"), QStringLiteral("claude-3-5-haiku-20241022"), QStringLiteral("claude-3-5-sonnet-20241022"), QStringLiteral("claude-3-opus-20240229")},
                       {{QStringLiteral("claude-sonnet-4-5"), 200000},
                        {QStringLiteral("claude-3-5-haiku-20241022"), 200000},
                        {QStringLiteral("claude-3-5-sonnet-20241022"), 200000},
                        {QStringLiteral("claude-3-opus-20240229"), 200000}}}},
        {"ollama",    {QStringLiteral("http://localhost:11434"),     QStringLiteral("qwen2.5:7b"),
                       {QStringLiteral("qwen2.5:7b"), QStringLiteral("llama3.1:8b"), QStringLiteral("deepseek-r1:7b")},
                       {{QStringLiteral("qwen2.5:7b"), 32768},
                        {QStringLiteral("llama3.1:8b"), 128000},
                        {QStringLiteral("deepseek-r1:7b"), 65536}}}},
    };
    return defaults;
}

const QStringList kKnownProviders = {QStringLiteral("openai"),
                                     QStringLiteral("anthropic"),
                                     QStringLiteral("ollama")};

constexpr auto kProfilesKey = "modelProfiles";
constexpr auto kActiveKey = "activeProfileId";

QVariantMap makeProfile(const QString& provider, const QString& displayName,
                        const QString& baseUrl, const QString& model,
                        const QString& apiKey, const QStringList& models = {}) {
    QVariantMap p;
    p["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    p["displayName"] = displayName;
    const QString validProvider = kKnownProviders.contains(provider) ? provider : QStringLiteral("openai");
    p["provider"] = validProvider;
    p["baseUrl"] = baseUrl;
    p["model"] = model;
    p["apiKey"] = apiKey;
    
    QStringList mList = models;
    if (mList.isEmpty()) {
        mList = providerDefaults().value(validProvider).models;
    }
    if (!model.isEmpty() && !mList.contains(model)) {
        mList.prepend(model);
    }
    p["models"] = mList;
    return p;
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

void SettingsService::load() {
    QSettings settings;

    // L2: schema migration hook. Runs once per schema version bump; v0→v1
    // is a no-op, but the version stamp is written so future migrations
    // can detect the starting point.
    const int storedVersion = settings.value("schemaVersion", 0).toInt();
    if (storedVersion < SettingsSchema::kSchemaVersion) {
        SettingsSchema::migrate(storedVersion);
        settings.setValue("schemaVersion", SettingsSchema::kSchemaVersion);
    }

    // ── Profiles (JSON list; apiKeys re-encrypted on persist) ──
    const QByteArray raw = settings.value(kProfilesKey).toString().toUtf8();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error == QJsonParseError::NoError && doc.isArray()) {
        for (const auto& entry : doc.array()) {
            QVariantMap p = entry.toObject().toVariantMap();
            p["apiKey"] = unprotectApiKey(p.value("apiKey").toString());
            const QString provider = p.value("provider").toString();
            // Drop profiles with an unknown provider type
            if (!kKnownProviders.contains(provider)) continue;

            // Ensure "models" field is populated
            QStringList models = p.value("models").toStringList();
            if (models.isEmpty()) {
                models = providerDefaults().value(provider).models;
            }
            const QString model = p.value("model").toString();
            if (!model.isEmpty() && !models.contains(model)) {
                models.prepend(model);
            }
            p["models"] = models;

            m_profiles.append(p);
        }
    }
    if (m_profiles.isEmpty())
        m_profiles = migrateLegacy();

    m_activeId = settings.value(kActiveKey).toString();
    if (activeProfile().isEmpty() && !m_profiles.isEmpty())
        m_activeId = m_profiles.first().toMap().value("id").toString();

    // Legacy env fallback for the active profile's key
    QVariantMap active = activeProfile();
    if (!active.isEmpty() && active.value("apiKey").toString().isEmpty()
            && active.value("provider").toString() != QLatin1String("ollama")) {
        const QString envKey = qEnvironmentVariable(
            active.value("provider").toString() == QLatin1String("anthropic")
                ? "ANTHROPIC_API_KEY" : "OPENAI_API_KEY");
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

    m_contextTokens = qBound(AIAgent::MinContextTokens,
                             settings.value("contextTokens", 8192).toInt(),
                             AIAgent::MaxContextTokens);
    m_simulationMode = settings.value("simulationMode", false).toBool();
    m_allowFullDiskRead = settings.value("security/allowFullDiskRead", false).toBool();
    m_localOnlyMode = settings.value("security/localOnlyMode", false).toBool();
    m_allowLocalNetworkFetch = settings.value("security/allowLocalNetworkFetch", false).toBool();
    m_embeddingModel = settings.value("embedding/model", QStringLiteral("nomic-embed-text")).toString();
    m_embeddingBaseUrl = settings.value("embedding/baseUrl", QStringLiteral("http://localhost:11434")).toString();
    m_systemPromptOverride = settings.value("agent/systemPromptOverride", QString()).toString();
    // logging/level is Logger::install()'s key; the env override still wins
    // there, so load() only mirrors the stored value (install() already
    // applied the effective level before any UI exists).
    m_logLevel = settings.value("logging/level", QStringLiteral("info")).toString();
    m_webSearchProvider = settings.value("webSearch/provider", QStringLiteral("tavily")).toString();
    // API key is stored encrypted (DPAPI) - empty if not set or decryption failed.
    m_webSearchApiKey = unprotectApiKey(settings.value("webSearch/apiKey", QString()).toString());
    m_episodicMemoryEnabled = settings.value("memory/episodicMemoryEnabled", false).toBool();
    // docs/16 §7.3: quota settings (flat keys; 0 = unlimited for int quotas).
    m_dailyTokenQuota = qMax(0, settings.value("dailyTokenQuota", 0).toInt());
    m_dailyCostQuotaCents = qMax(0, settings.value("dailyCostQuotaCents", 0).toInt());
    m_monthlyTokenQuota = qMax(0, settings.value("monthlyTokenQuota", 0).toInt());
    m_monthlyCostQuotaCents = qMax(0, settings.value("monthlyCostQuotaCents", 0).toInt());
    m_strictQuotaMode = settings.value("strictQuotaMode", false).toBool();
    m_defaultPermissionMode = settings.value("agent/defaultPermissionMode",
                                             QStringLiteral("confirm")).toString();
    m_activeProjectId = settings.value("projects/activeId").toString();
}

// Single-provider layout (pre-0.3): provider, apiKey, <provider>/baseUrl,
// <provider>/model -> one profile per provider that carried configuration.
QVariantList SettingsService::migrateLegacy() {
    QSettings settings;

    const QString legacyProvider = [&]() {
        const QString p = settings.value("provider", "openai").toString();
        return kKnownProviders.contains(p) ? p : QStringLiteral("openai");
    }();
    const QString legacyKey = unprotectApiKey(settings.value("apiKey").toString());

    QVariantList out;
    for (const QString& provider : kKnownProviders) {
        const bool isActiveProvider = (provider == legacyProvider);
        const bool hasScopedValues = settings.contains(provider + "/baseUrl");
        if (!isActiveProvider && !hasScopedValues) continue;

        const ProviderDefaults& defaults = providerDefaults().value(provider);
        out.append(makeProfile(
            provider,
            providerDisplayName(provider),
            settings.value(provider + "/baseUrl", defaults.baseUrl).toString(),
            settings.value(provider + "/model", defaults.model).toString(),
            provider == QLatin1String("ollama") ? QString() : legacyKey));
    }
    if (out.isEmpty()) {
        const ProviderDefaults& defaults = providerDefaults().value(legacyProvider);
        out.append(makeProfile(legacyProvider, providerDisplayName(legacyProvider),
                               defaults.baseUrl, defaults.model, legacyKey));
    }

    qInfo() << "Migrated legacy provider settings into" << out.size() << "model profile(s)";
    return out;
}

void SettingsService::persist() {
    QJsonArray arr;
    for (const QVariant& entry : m_profiles) {
        QVariantMap p = entry.toMap();
        p["apiKey"] = protectApiKey(p.value("apiKey").toString());
        arr.append(QJsonObject::fromVariantMap(p));
    }
    QSettings settings;
    settings.setValue(kProfilesKey,
                      QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
    settings.setValue(kActiveKey, m_activeId);
    settings.sync();
}

QString SettingsService::addProfile() {
    const ProviderDefaults& defaults = providerDefaults().value(QStringLiteral("openai"));
    const QVariantMap profile = makeProfile(QStringLiteral("openai"),
        tr("新配置"), defaults.baseUrl, defaults.model, QString());

    const QString id = profile.value("id").toString();
    m_profiles.append(profile);
    if (m_profiles.size() == 1)
        m_activeId = id;

    persist();
    emit profilesChanged();
    if (m_activeId == id)
        emit activeProfileChanged();
    return id;
}

void SettingsService::updateProfile(const QString& id, const QVariantMap& fields) {
    for (int i = 0; i < m_profiles.size(); ++i) {
        QVariantMap p = m_profiles.at(i).toMap();
        if (p.value("id").toString() != id) continue;

        for (auto it = fields.constBegin(); it != fields.constEnd(); ++it) {
            if (it.key() == QLatin1String("id")) continue; // id is immutable
            if (it.key() == QLatin1String("provider")
                    && !kKnownProviders.contains(it.value().toString()))
                continue;
            p[it.key()] = it.value();
        }
        m_profiles[i] = p;

        persist();
        emit profilesChanged();
        if (id == m_activeId)
            emit activeProfileChanged();
        return;
    }
}

void SettingsService::removeProfile(const QString& id) {
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles.at(i).toMap().value("id").toString() != id) continue;

        m_profiles.removeAt(i);
        if (m_activeId == id) {
            m_activeId = m_profiles.isEmpty()
                ? QString() : m_profiles.first().toMap().value("id").toString();
        }
        persist();
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

    m_activeId = id;
    persist();
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
        m_profiles[i] = p;

        persist();
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
            m_profiles[i] = p;

            persist();
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

            m_profiles[i] = p;

            persist();
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
    return tr("Claude");
}

QVariantMap SettingsService::defaultEndpoint(const QString& provider) const {
    const ProviderDefaults& defaults =
        providerDefaults().value(kKnownProviders.contains(provider) ? provider : QStringLiteral("openai"));
    return {{QStringLiteral("baseUrl"), defaults.baseUrl},
            {QStringLiteral("model"), defaults.model},
            {QStringLiteral("models"), defaults.models}};
}

void SettingsService::setContextTokens(int tokens) {
    const int clamped = qBound(AIAgent::MinContextTokens, tokens, AIAgent::MaxContextTokens);
    if (m_contextTokens == clamped) return;
    m_contextTokens = clamped;
    QSettings().setValue("contextTokens", m_contextTokens);
    emit contextTokensChanged();
}

void SettingsService::setSimulationMode(bool enabled) {
    if (m_simulationMode == enabled) return;
    m_simulationMode = enabled;
    QSettings().setValue("simulationMode", enabled);
    emit simulationModeChanged();
}

void SettingsService::setAllowFullDiskRead(bool enabled) {
    if (m_allowFullDiskRead == enabled) return;
    m_allowFullDiskRead = enabled;
    Activity::usage(Activity::Kind::User, QStringLiteral("settings.allowFullDiskRead"),
                    enabled ? QStringLiteral("on") : QStringLiteral("off"));
    QSettings().setValue("security/allowFullDiskRead", enabled);
    emit allowFullDiskReadChanged();
}

// Batch 14 (SEC-1401): same silent-repeat pattern as allowFullDiskRead.
// The actual tool gating lives in main.cpp (applyLocalOnlyGate), wired to
// localOnlyModeChanged for immediate effect.
void SettingsService::setLocalOnlyMode(bool enabled) {
    if (m_localOnlyMode == enabled) return;
    m_localOnlyMode = enabled;
    Activity::usage(Activity::Kind::User, QStringLiteral("settings.localOnlyMode"),
                    enabled ? QStringLiteral("on") : QStringLiteral("off"));
    QSettings().setValue("security/localOnlyMode", enabled);
    emit localOnlyModeChanged();
}

// Batch 20 (FEAT-2002): SSRF escape hatch for http_fetch - same silent-repeat
// pattern as allowFullDiskRead. Consumed via main.cpp applySettings →
// NetworkFetchTool::setAllowPrivateTargets.
void SettingsService::setAllowLocalNetworkFetch(bool enabled) {
    if (m_allowLocalNetworkFetch == enabled) return;
    m_allowLocalNetworkFetch = enabled;
    Activity::usage(Activity::Kind::User, QStringLiteral("settings.allowLocalNetworkFetch"),
                    enabled ? QStringLiteral("on") : QStringLiteral("off"));
    QSettings().setValue("security/allowLocalNetworkFetch", enabled);
    emit allowLocalNetworkFetchChanged();
}

void SettingsService::setEmbeddingModel(const QString& model) {
    const QString trimmed = model.trimmed();
    if (trimmed.isEmpty() || m_embeddingModel == trimmed) return;
    m_embeddingModel = trimmed;
    Activity::usage(Activity::Kind::User, QStringLiteral("settings.embeddingModel"),
                    trimmed);
    QSettings().setValue("embedding/model", trimmed);
    emit embeddingModelChanged();
}

// Batch 25 (G11): endpoint for the embedding requests; vectors depend on
// the model only, so a change here never triggers a re-embed.
void SettingsService::setEmbeddingBaseUrl(const QString& url) {
    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty() || m_embeddingBaseUrl == trimmed) return;
    m_embeddingBaseUrl = trimmed;
    Activity::usage(Activity::Kind::User, QStringLiteral("settings.embeddingBaseUrl"),
                    trimmed);
    QSettings().setValue("embedding/baseUrl", trimmed);
    emit embeddingBaseUrlChanged();
}

// Empty (after trim) clears the override and falls back to the built-in
// prompt - unlike embeddingModel, empty is a meaningful value here.
void SettingsService::setSystemPromptOverride(const QString& prompt) {
    const QString trimmed = prompt.trimmed();
    if (m_systemPromptOverride == trimmed) return;
    m_systemPromptOverride = trimmed;
    Activity::usage(Activity::Kind::User, QStringLiteral("settings.systemPromptOverride"),
                    trimmed.isEmpty() ? QStringLiteral("(cleared)")
                                      : QStringLiteral("%1 chars").arg(trimmed.size()));
    QSettings().setValue("agent/systemPromptOverride", trimmed);
    emit systemPromptOverrideChanged();
}

void SettingsService::setLogLevel(const QString& level) {
    const QString normalized = level.trimmed().toLower();
    if (normalized.isEmpty() || m_logLevel == normalized) return;
    m_logLevel = normalized;
    QSettings().setValue("logging/level", normalized);
    Logger::setMinLevel(Logger::levelFromName(normalized, Logger::minLevel()));
    emit logLevelChanged();
}

// L1: legal search provider keys for the settings UI. Kept in sync with the
// backend factory (L4); currently only Tavily is implemented.
QStringList SettingsService::webSearchProviders() const {
    return {QStringLiteral("tavily"), QStringLiteral("duckduckgo")};
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
    m_webSearchProvider = trimmed;
    QSettings().setValue("webSearch/provider", trimmed);
    Activity::usage(Activity::Kind::User, QStringLiteral("settings.webSearchProvider"),
                    trimmed);
    emit webSearchProviderChanged();
}

// API key is encrypted at rest via DPAPI. Empty string drops the stored key.
void SettingsService::setWebSearchApiKey(const QString& key) {
    const QString trimmed = key.trimmed();
    if (m_webSearchApiKey == trimmed) return;
    m_webSearchApiKey = trimmed;
    // REQ-T6-05: encrypt on persistence; never log the key.
    const QString stored = protectApiKey(trimmed);
    QSettings().setValue("webSearch/apiKey", stored.isEmpty() ? QString() : stored);
    Activity::usage(Activity::Kind::User, QStringLiteral("settings.webSearchApiKey"),
                    trimmed.isEmpty() ? QStringLiteral("(cleared)") : QStringLiteral("(set)"));
    emit webSearchApiKeyChanged();
}

// T8 (F4): episodic memory toggle - same 5-step pattern as localOnlyMode.
void SettingsService::setEpisodicMemoryEnabled(bool enabled) {
    if (m_episodicMemoryEnabled == enabled) return;
    m_episodicMemoryEnabled = enabled;
    Activity::usage(Activity::Kind::User, QStringLiteral("settings.episodicMemoryEnabled"),
                    enabled ? QStringLiteral("on") : QStringLiteral("off"));
    QSettings().setValue("memory/episodicMemoryEnabled", enabled);
    emit episodicMemoryEnabledChanged();
}

// docs/16 §7.3: quota setters. Int quotas are clamped to >= 0 (0 = unlimited);
// same 4-step pattern as setContextTokens/setSimulationMode. Flat QSettings keys
// match the SettingsSchema registration (no prefix).
void SettingsService::setDailyTokenQuota(int v) {
    const int clamped = qMax(0, v);
    if (m_dailyTokenQuota == clamped) return;
    m_dailyTokenQuota = clamped;
    QSettings().setValue("dailyTokenQuota", m_dailyTokenQuota);
    emit dailyTokenQuotaChanged();
}

void SettingsService::setDailyCostQuotaCents(int v) {
    const int clamped = qMax(0, v);
    if (m_dailyCostQuotaCents == clamped) return;
    m_dailyCostQuotaCents = clamped;
    QSettings().setValue("dailyCostQuotaCents", m_dailyCostQuotaCents);
    emit dailyCostQuotaCentsChanged();
}

void SettingsService::setMonthlyTokenQuota(int v) {
    const int clamped = qMax(0, v);
    if (m_monthlyTokenQuota == clamped) return;
    m_monthlyTokenQuota = clamped;
    QSettings().setValue("monthlyTokenQuota", m_monthlyTokenQuota);
    emit monthlyTokenQuotaChanged();
}

void SettingsService::setMonthlyCostQuotaCents(int v) {
    const int clamped = qMax(0, v);
    if (m_monthlyCostQuotaCents == clamped) return;
    m_monthlyCostQuotaCents = clamped;
    QSettings().setValue("monthlyCostQuotaCents", m_monthlyCostQuotaCents);
    emit monthlyCostQuotaCentsChanged();
}

void SettingsService::setStrictQuotaMode(bool v) {
    if (m_strictQuotaMode == v) return;
    m_strictQuotaMode = v;
    QSettings().setValue("strictQuotaMode", v);
    emit strictQuotaModeChanged();
}

void SettingsService::setActiveProjectId(const QString& id) {
    if (m_activeProjectId == id) return;
    m_activeProjectId = id;
    QSettings().setValue("projects/activeId", id);
    emit activeProjectIdChanged();
}

void SettingsService::resetGeneralToDefaults() {
    // 键集合与 GeneralPage 呈现项一一对应；默认值与 SettingsSchema 保持
    // 一致（test_settings::resetGeneralDefaultsMatchesSchema 锁定）。
    setContextTokens(8192);
    setSimulationMode(false);
    setAllowFullDiskRead(false);
    setLocalOnlyMode(false);
    setAllowLocalNetworkFetch(false);
    setLogLevel(QStringLiteral("info"));
    setEmbeddingModel(QStringLiteral("nomic-embed-text"));
    setEmbeddingBaseUrl(QStringLiteral("http://localhost:11434"));
    setSystemPromptOverride(QString());
}

void SettingsService::setDefaultPermissionMode(const QString& v) {
    // 只接受 PermissionMode 的三种字符串形式；未知值回落 confirm。
    if (v != QLatin1String("confirm") && v != QLatin1String("auto_edit")
            && v != QLatin1String("full_access"))
        return;
    if (m_defaultPermissionMode == v) return;
    m_defaultPermissionMode = v;
    QSettings().setValue("agent/defaultPermissionMode", v);
    emit defaultPermissionModeChanged();
}

// Capability-table lookup: returns the context window for a provider+model,
// or 0 when either is unknown so the caller can fall back to the global
// contextTokens. providerDefaults() lives in the anonymous namespace above
// but is visible to these member definitions within the same TU.
int SettingsService::modelContextWindow(const QString& provider,
                                        const QString& model) const {
    const auto it = providerDefaults().constFind(provider);
    if (it == providerDefaults().constEnd()) return 0;
    const auto wit = it.value().contextWindows.constFind(model);
    if (wit == it.value().contextWindows.constEnd()) return 0;
    return wit.value();
}

// Three-level priority resolution for the active profile's context budget:
//   1. Profile-level "contextLength" override (highest)
//   2. Capability-table contextWindow (default per model)
//   3. Global contextTokens (fallback for unknown models)
int SettingsService::resolveContextLimit() const {
    const QVariantMap profile = activeProfile();
    const QString provider = profile.value("provider").toString();
    const QString model = profile.value("model").toString();

    // Priority 1: Profile-level contextLength override.
    const QVariant profileOverride = profile.value("contextLength");
    if (profileOverride.isValid() && !profileOverride.toString().isEmpty()) {
        bool ok = false;
        const int v = profileOverride.toInt(&ok);
        if (ok && v > 0) return v;
    }

    // Priority 2: capability-table contextWindow.
    const int tableValue = modelContextWindow(provider, model);
    if (tableValue > 0) return tableValue;

    // Priority 3: global contextTokens (fallback).
    return m_contextTokens;
}

} // namespace NexusAI
