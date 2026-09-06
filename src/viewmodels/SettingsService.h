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
    Q_PROPERTY(QString embeddingModel READ embeddingModel WRITE setEmbeddingModel NOTIFY embeddingModelChanged)
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

    // Knowledge-base embedding model (FEAT-801): identity for persisted
    // chunk vectors; a change triggers a full knowledge re-embed.
    QString embeddingModel() const { return m_embeddingModel; }
    void setEmbeddingModel(const QString& model);

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

signals:
    void profilesChanged();
    void activeProfileChanged();
    void contextTokensChanged();
    void simulationModeChanged();
    void allowFullDiskReadChanged();
    void embeddingModelChanged();

private:
    void load();
    QVariantList migrateLegacy();
    void persist();

    QVariantList m_profiles;   // each entry: {id, displayName, provider, baseUrl, apiKey, model}
    QString m_activeId;
    int m_contextTokens{8192};
    bool m_simulationMode{false};
    bool m_allowFullDiskRead{false};
    QString m_embeddingModel{QStringLiteral("nomic-embed-text")};

    static SettingsService* s_instance;
};

} // namespace NexusAI
