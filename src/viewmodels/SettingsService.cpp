#include "SettingsService.h"

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
    QString displayName;
    QString baseUrl;
    QString model;
    QStringList models;
};

const QHash<QString, ProviderDefaults>& providerDefaults() {
    static const QHash<QString, ProviderDefaults> defaults = {
        {"openai",    {QStringLiteral("OpenAI 兼容"), QStringLiteral("https://api.openai.com/v1"),  QStringLiteral("gpt-4o-mini"),
                       {QStringLiteral("gpt-4o-mini"), QStringLiteral("gpt-4o"), QStringLiteral("gpt-4-turbo"), QStringLiteral("o3-mini")}}},
        {"anthropic", {QStringLiteral("Claude"),      QStringLiteral("https://api.anthropic.com"), QStringLiteral("claude-sonnet-4-5"),
                       {QStringLiteral("claude-sonnet-4-5"), QStringLiteral("claude-3-5-haiku-20241022"), QStringLiteral("claude-3-5-sonnet-20241022"), QStringLiteral("claude-3-opus-20240229")}}},
        {"ollama",    {QStringLiteral("Ollama 本地"), QStringLiteral("http://localhost:11434"),     QStringLiteral("qwen2.5:7b"),
                       {QStringLiteral("qwen2.5:7b"), QStringLiteral("llama3.1:8b"), QStringLiteral("deepseek-r1:7b")}}},
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
        return plain; // fall back to plain text rather than losing the key
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

    m_contextTokens = qBound(2048, settings.value("contextTokens", 8192).toInt(), 1048576);
    m_simulationMode = settings.value("simulationMode", false).toBool();
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
            defaults.displayName,
            settings.value(provider + "/baseUrl", defaults.baseUrl).toString(),
            settings.value(provider + "/model", defaults.model).toString(),
            provider == QLatin1String("ollama") ? QString() : legacyKey));
    }
    if (out.isEmpty()) {
        const ProviderDefaults& defaults = providerDefaults().value(legacyProvider);
        out.append(makeProfile(legacyProvider, defaults.displayName,
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
        QStringLiteral("新配置"), defaults.baseUrl, defaults.model, QString());

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

QVariantMap SettingsService::defaultEndpoint(const QString& provider) const {
    const ProviderDefaults& defaults =
        providerDefaults().value(kKnownProviders.contains(provider) ? provider : QStringLiteral("openai"));
    return {{QStringLiteral("baseUrl"), defaults.baseUrl},
            {QStringLiteral("model"), defaults.model},
            {QStringLiteral("models"), defaults.models}};
}

void SettingsService::setContextTokens(int tokens) {
    const int clamped = qBound(2048, tokens, 1048576);
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

} // namespace NexusAI
