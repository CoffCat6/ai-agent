#pragma once

#include <QCoreApplication>
#include <QQmlEngine>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace NexusAI {

// Application settings persisted via QSettings.
//
// Model access is organized as named profiles: each profile binds a provider
// type ("openai" | "anthropic" | "ollama") to an endpoint (baseUrl / model /
// apiKey) plus a display name. Any number of profiles can coexist and the
// active one drives the live agent pipeline; switching is instant. The
// legacy single-provider keys are migrated into profiles on first load.
//
// API keys are encrypted at rest on Windows (DPAPI, per-user).
class SettingsService : public QObject {
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(Settings)

    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
    Q_PROPERTY(QString activeProfileId READ activeProfileId NOTIFY activeProfileChanged)
    Q_PROPERTY(QVariantMap activeProfile READ activeProfile NOTIFY activeProfileChanged)
    Q_PROPERTY(int contextTokens READ contextTokens WRITE setContextTokens NOTIFY contextTokensChanged)
    Q_PROPERTY(bool simulationMode READ simulationMode WRITE setSimulationMode NOTIFY simulationModeChanged)
    Q_PROPERTY(bool allowFullDiskRead READ allowFullDiskRead WRITE setAllowFullDiskRead NOTIFY allowFullDiskReadChanged)
    Q_PROPERTY(bool allowLocalNetworkFetch READ allowLocalNetworkFetch WRITE setAllowLocalNetworkFetch NOTIFY allowLocalNetworkFetchChanged)
    // Batch 14 (SEC-1401): when enabled, the knowledge-base tool is gated off
    // for cloud providers - document content must not leave this machine.
    Q_PROPERTY(bool localOnlyMode READ localOnlyMode WRITE setLocalOnlyMode NOTIFY localOnlyModeChanged)
    Q_PROPERTY(QString embeddingModel READ embeddingModel WRITE setEmbeddingModel NOTIFY embeddingModelChanged)
    Q_PROPERTY(QString logLevel READ logLevel WRITE setLogLevel NOTIFY logLevelChanged)
    // Batch 22 (F8): user-editable system prompt; empty string = built-in.
    Q_PROPERTY(QString systemPromptOverride READ systemPromptOverride WRITE setSystemPromptOverride NOTIFY systemPromptOverrideChanged)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)

public:
    // No default constructor on purpose (see AgentViewModel: the engine
    // would prefer Constructor over the create() factory).
    explicit SettingsService(QObject* parent);

    // QML singleton plumbing (same pattern as AgentViewModel)
    static SettingsService* instance();
    static SettingsService* create(QQmlEngine*, QJSEngine*);

    QVariantList profiles() const { return m_profiles; }
    QString activeProfileId() const { return m_activeId; }
    QVariantMap activeProfile() const;

    int contextTokens() const { return m_contextTokens; }
    bool simulationMode() const { return m_simulationMode; }
    void setContextTokens(int tokens);
    void setSimulationMode(bool enabled);

    // Security: allow the agent to read files outside the user-directory
    // allowlist (audit P1-8). Opt-in, default off.
    bool allowFullDiskRead() const { return m_allowFullDiskRead; }
    void setAllowFullDiskRead(bool enabled);

    // Security: keep knowledge-base content on this machine (FIX-505
    // follow-up). Opt-in, default off; the wiring that actually gates the
    // tool lives in main.cpp (applyLocalOnlyGate).
    bool localOnlyMode() const { return m_localOnlyMode; }
    void setLocalOnlyMode(bool enabled);

    // Security: SSRF escape hatch for http_fetch (Batch 20) - allows the
    // tool to reach loopback/private targets (e.g. the local 8317 gateway).
    // Opt-in, default off; consumed by NetworkFetchTool via main.cpp.
    bool allowLocalNetworkFetch() const { return m_allowLocalNetworkFetch; }
    void setAllowLocalNetworkFetch(bool enabled);

    // Knowledge-base embedding model (FEAT-801): identity for persisted
    // chunk vectors; a change triggers a full knowledge re-embed.
    QString embeddingModel() const { return m_embeddingModel; }
    void setEmbeddingModel(const QString& model);
    QString systemPromptOverride() const { return m_systemPromptOverride; }
    void setSystemPromptOverride(const QString& prompt);

    // Runtime-log minimum level (日志开发 doc 6 §3.5): reuses the
    // logging/level key that Logger::install() reads at startup, applies
    // immediately via Logger::setMinLevel. NEXUSAI_LOG_LEVEL still wins.
    QString logLevel() const { return m_logLevel; }
    void setLogLevel(const QString& level);

    static QString appVersion() { return QCoreApplication::applicationVersion(); }

    // ── Profile management (QML invokables) ───────────
    // Create a profile with provider defaults; returns its id.
    Q_INVOKABLE QString addProfile();
    // Merge the given fields (displayName/provider/baseUrl/apiKey/model/models)
    // into the profile; unknown fields are ignored.
    Q_INVOKABLE void updateProfile(const QString& id, const QVariantMap& fields);
    Q_INVOKABLE void removeProfile(const QString& id);
    Q_INVOKABLE void setActiveProfile(const QString& id);
    // Switch active model for a specific profile (or active profile if id is empty)
    Q_INVOKABLE void selectModelForProfile(const QString& id, const QString& model);
    // Add a model name to a profile's model list if not present
    Q_INVOKABLE void addModelToProfile(const QString& id, const QString& model);
    // Remove a model name from a profile's model list
    Q_INVOKABLE void removeModelFromProfile(const QString& id, const QString& model);
    // {baseUrl, model, models} defaults for a provider type (used when the editor
    // switches the provider of a profile).
    Q_INVOKABLE QVariantMap defaultEndpoint(const QString& provider) const;
    // Localized display name for a provider key (legacy profile migration).
    QString providerDisplayName(const QString& provider) const;

signals:
    void profilesChanged();
    void activeProfileChanged();
    void contextTokensChanged();
    void simulationModeChanged();
    void allowFullDiskReadChanged();
    void localOnlyModeChanged();
    void allowLocalNetworkFetchChanged();
    void embeddingModelChanged();
    void logLevelChanged();
    void systemPromptOverrideChanged();

private:
    void load();
    QVariantList migrateLegacy();
    void persist();

    QVariantList m_profiles;   // each entry: {id, displayName, provider, baseUrl, apiKey, model}
    QString m_activeId;
    int m_contextTokens{8192};
    bool m_simulationMode{false};
    bool m_allowFullDiskRead{false};
    bool m_localOnlyMode{false};
    bool m_allowLocalNetworkFetch{false};
    QString m_embeddingModel{QStringLiteral("nomic-embed-text")};
    QString m_logLevel{QStringLiteral("info")};
    QString m_systemPromptOverride;

    static SettingsService* s_instance;
};

} // namespace NexusAI
