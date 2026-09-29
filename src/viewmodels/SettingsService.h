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
    // Batch 25 (G11): Ollama endpoint for knowledge-base embeddings; the
    // protocol stays /api/embed(dings) - only the host:port is configurable.
    Q_PROPERTY(QString embeddingBaseUrl READ embeddingBaseUrl WRITE setEmbeddingBaseUrl NOTIFY embeddingBaseUrlChanged)
    Q_PROPERTY(QString logLevel READ logLevel WRITE setLogLevel NOTIFY logLevelChanged)
    // Batch 22 (F8): user-editable system prompt; empty string = built-in.
    Q_PROPERTY(QString systemPromptOverride READ systemPromptOverride WRITE setSystemPromptOverride NOTIFY systemPromptOverrideChanged)
    // T6 (F3): web search provider and API key (encrypted at rest via DPAPI).
    Q_PROPERTY(QString webSearchProvider READ webSearchProvider WRITE setWebSearchProvider NOTIFY webSearchProviderChanged)
    Q_PROPERTY(QString webSearchApiKey READ webSearchApiKey WRITE setWebSearchApiKey NOTIFY webSearchApiKeyChanged)
    // L1: whether a search API key is stored (the key itself is never echoed
    // to QML; the getter returns a masked empty string).
    Q_PROPERTY(bool webSearchApiKeySet READ webSearchApiKeySet NOTIFY webSearchApiKeyChanged)
    // L1: legal search provider keys for the settings UI (single source of
    // truth shared with the backend factory, L4).
    Q_PROPERTY(QStringList webSearchProviders READ webSearchProviders CONSTANT)
    // T8 (F4): episodic long-term memory; when enabled, session summaries
    // are generated on task completion and injected into new sessions.
    Q_PROPERTY(bool episodicMemoryEnabled READ episodicMemoryEnabled WRITE setEpisodicMemoryEnabled NOTIFY episodicMemoryEnabledChanged)
    // docs/16 §7.3: quota control. All quotas default to 0 = unlimited;
    // strictQuotaMode true = reject on exceed, false = warn only.
    Q_PROPERTY(int dailyTokenQuota READ dailyTokenQuota WRITE setDailyTokenQuota NOTIFY dailyTokenQuotaChanged)
    Q_PROPERTY(int dailyCostQuotaCents READ dailyCostQuotaCents WRITE setDailyCostQuotaCents NOTIFY dailyCostQuotaCentsChanged)
    Q_PROPERTY(int monthlyTokenQuota READ monthlyTokenQuota WRITE setMonthlyTokenQuota NOTIFY monthlyTokenQuotaChanged)
    Q_PROPERTY(int monthlyCostQuotaCents READ monthlyCostQuotaCents WRITE setMonthlyCostQuotaCents NOTIFY monthlyCostQuotaCentsChanged)
    Q_PROPERTY(bool strictQuotaMode READ strictQuotaMode WRITE setStrictQuotaMode NOTIFY strictQuotaModeChanged)
    // 会话权限模式的"全局默认"：新会话/tab 的初始模式。取值
    // "confirm"/"auto_edit"/"full_access"（PermissionMode 字符串形式）。
    Q_PROPERTY(QString defaultPermissionMode READ defaultPermissionMode WRITE setDefaultPermissionMode NOTIFY defaultPermissionModeChanged)
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
    QString embeddingBaseUrl() const { return m_embeddingBaseUrl; }
    void setEmbeddingBaseUrl(const QString& url);
    QString logLevel() const { return m_logLevel; }
    void setLogLevel(const QString& level);
    QString systemPromptOverride() const { return m_systemPromptOverride; }
    void setSystemPromptOverride(const QString& prompt);

    // T6 (F3): search provider ("tavily" | …) and encrypted API key.
    QString webSearchProvider() const { return m_webSearchProvider; }
    void setWebSearchProvider(const QString& provider);
    QString webSearchApiKey() const { return {}; }   // masked getter
    void setWebSearchApiKey(const QString& key);
    // L1: true when a non-empty search API key is stored.
    bool webSearchApiKeySet() const { return !m_webSearchApiKey.isEmpty(); }
    // L1: legal provider keys for the settings UI.
    QStringList webSearchProviders() const;

    // T8 (F4): episodic memory toggle (default off).
    bool episodicMemoryEnabled() const { return m_episodicMemoryEnabled; }
    void setEpisodicMemoryEnabled(bool enabled);

    // docs/16 §7.3: quota settings. 0 = unlimited for the int quotas.
    int dailyTokenQuota() const { return m_dailyTokenQuota; }
    void setDailyTokenQuota(int v);
    int dailyCostQuotaCents() const { return m_dailyCostQuotaCents; }
    void setDailyCostQuotaCents(int v);
    int monthlyTokenQuota() const { return m_monthlyTokenQuota; }
    void setMonthlyTokenQuota(int v);
    int monthlyCostQuotaCents() const { return m_monthlyCostQuotaCents; }
    void setMonthlyCostQuotaCents(int v);
    bool strictQuotaMode() const { return m_strictQuotaMode; }
    void setStrictQuotaMode(bool v);

    QString defaultPermissionMode() const { return m_defaultPermissionMode; }
    void setDefaultPermissionMode(const QString& v);

    QString activeProjectId() const { return m_activeProjectId; }
    void setActiveProjectId(const QString& id);

    // docs/17 A1: 读取黑名单用户追加项（QVariantList of QString）。
    Q_PROPERTY(QVariantList readDenyList READ readDenyList WRITE setReadDenyList NOTIFY readDenyListChanged FINAL)
    QVariantList readDenyList() const { return m_readDenyList; }
    void setReadDenyList(const QVariantList& v);

    // docs/17 A0: 当前活动项目 id 的持久化（运行时真值在 AgentViewModel，
    // main.cpp 双向桥接）。空 = 无项目。
    Q_PROPERTY(QString activeProjectId READ activeProjectId WRITE setActiveProjectId NOTIFY activeProjectIdChanged FINAL)

    static QString appVersion() { return QCoreApplication::applicationVersion(); }

    // ── Profile management (QML invokables) ───────────
    // Create a profile with provider defaults; returns its id.
    Q_INVOKABLE QString addProfile();
    // Merge the given fields (displayName/provider/baseUrl/apiKey/model/models)
    // into the profile; unknown fields are ignored.
    Q_INVOKABLE void updateProfile(const QString& id, const QVariantMap& fields);
    Q_INVOKABLE void removeProfile(const QString& id);
    // docs/17 B2: 把通用设置页的 9 个键回写 schema 默认值（安全开关回滚
    // 由调用方二次确认）。逐键走既有 setter，保证持久化与 NOTIFY 一致。
    Q_INVOKABLE void resetGeneralToDefaults();

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

    // Query the capability-table context window for a given provider+model.
    // Returns 0 when the provider or model is unknown (caller should fall
    // back to the global contextTokens). Invoked from QML (ModelConfigPage)
    // to render the per-model window chip.
    Q_INVOKABLE int modelContextWindow(const QString& provider, const QString& model) const;

    // Resolve the effective context budget for the active profile using a
    // three-level priority: Profile-level "contextLength" override >
    // capability-table contextWindow > global contextTokens. Consumed by
    // main.cpp when wiring AIAgent::setContextLimit.
    int resolveContextLimit() const;

signals:
    void profilesChanged();
    void activeProfileChanged();
    void contextTokensChanged();
    void simulationModeChanged();
    void allowFullDiskReadChanged();
    void localOnlyModeChanged();
    void allowLocalNetworkFetchChanged();
    void embeddingModelChanged();
    void embeddingBaseUrlChanged();
    void logLevelChanged();
    void systemPromptOverrideChanged();
    void webSearchProviderChanged();
    void webSearchApiKeyChanged();
    void episodicMemoryEnabledChanged();
    void dailyTokenQuotaChanged();
    void dailyCostQuotaCentsChanged();
    void monthlyTokenQuotaChanged();
    void monthlyCostQuotaCentsChanged();
    void strictQuotaModeChanged();
    void defaultPermissionModeChanged();
    void activeProjectIdChanged();
    void readDenyListChanged();

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
    QString m_embeddingBaseUrl{QStringLiteral("http://localhost:11434")};
    QString m_logLevel{QStringLiteral("info")};
    QString m_systemPromptOverride;
    QString m_webSearchProvider{QStringLiteral("tavily")};
    QString m_webSearchApiKey;
    bool m_episodicMemoryEnabled{false};
    // docs/16 §7.3: quota control state (0 = unlimited for int quotas).
    int m_dailyTokenQuota{0};
    int m_dailyCostQuotaCents{0};
    int m_monthlyTokenQuota{0};
    int m_monthlyCostQuotaCents{0};
    bool m_strictQuotaMode{false};
    QString m_defaultPermissionMode{QStringLiteral("confirm")};
    QString m_activeProjectId;
    QVariantList m_readDenyList;

    static SettingsService* s_instance;
};

} // namespace NexusAI
