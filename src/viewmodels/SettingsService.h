#pragma once

#include <QCoreApplication>
#include <QQmlEngine>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// 步数预算的默认值与钳制边界都定义在 core 层（AIAgent），此处引用同一常量，
// 避免两处各写一个字面量后漂移。
#include "core/agent/AIAgent.h"

namespace NexusAI {

// Application settings persisted via QSettings.
//
// Model access is organized as named profiles. Each profile carries two
// orthogonal axes: a vendor ("openai" | "anthropic" | "ollama", which decides
// the endpoint defaults) and an API protocol ("openai-chat" |
// "anthropic-messages" | "ollama-chat", which decides the wire format and the
// service implementation — see infrastructure/llm/LlmProtocolRegistry), plus
// baseUrl / model / apiKey and a display name. Any number of profiles can
// coexist and the active one drives the live agent pipeline; switching is
// instant. Profiles persisted before the protocol axis existed are normalized
// on load (protocol inferred from vendor), and the legacy single-provider
// keys are migrated into profiles on first load.
//
// A profile additionally carries "kind" — where its defaults came from
// ("openai" | "anthropic" | "ollama" | "custom"). It is a settings-page axis
// only: kind="custom" means an openai / openai-chat profile whose baseUrl,
// model and model list are never pre-filled, and it gates the endpoint-owned
// refills in updateProfile() and load(). Wire format, local-only gating and
// the capability table keep reading provider / apiProtocol, so a custom
// profile is still an OpenAI-compatible one.
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
    // 每轮任务的步数预算（LLM 往返轮数）。曾硬编码为 10，普通多步任务经常
    // 还没得出结论就耗尽，只能拿已有信息拼一个"我无法确认"的答复。
    Q_PROPERTY(int maxSteps READ maxSteps WRITE setMaxSteps NOTIFY maxStepsChanged)
    Q_PROPERTY(int taskTimeoutMinutes READ taskTimeoutMinutes WRITE setTaskTimeoutMinutes NOTIFY taskTimeoutMinutesChanged)

    Q_PROPERTY(bool allowFullDiskRead READ allowFullDiskRead WRITE setAllowFullDiskRead NOTIFY allowFullDiskReadChanged)
    // docs/17 P1-4: 项目目录写入放行（默认关，安全页）。
    Q_PROPERTY(bool allowProjectWrite READ allowProjectWrite WRITE setAllowProjectWrite NOTIFY allowProjectWriteChanged FINAL)
    Q_PROPERTY(bool allowLocalNetworkFetch READ allowLocalNetworkFetch WRITE setAllowLocalNetworkFetch NOTIFY allowLocalNetworkFetchChanged)
    // Batch 14 (SEC-1401): when enabled, the knowledge-base tool is gated off
    // for cloud providers - document content must not leave this machine.
    Q_PROPERTY(bool localOnlyMode READ localOnlyMode WRITE setLocalOnlyMode NOTIFY localOnlyModeChanged)
    Q_PROPERTY(QString embeddingModel READ embeddingModel WRITE setEmbeddingModel NOTIFY embeddingModelChanged)
    // Batch 25 (G11): Ollama endpoint for knowledge-base embeddings; the
    // protocol stays /api/embed(dings) - only the host:port is configurable.
    Q_PROPERTY(QString embeddingBaseUrl READ embeddingBaseUrl WRITE setEmbeddingBaseUrl NOTIFY embeddingBaseUrlChanged)
    Q_PROPERTY(QString logLevel READ logLevel WRITE setLogLevel NOTIFY logLevelChanged)
    Q_PROPERTY(QString logDirectory READ logDirectory WRITE setLogDirectory NOTIFY logDirectoryChanged)
    Q_PROPERTY(int logMaxFileSizeMb READ logMaxFileSizeMb WRITE setLogMaxFileSizeMb NOTIFY logMaxFileSizeMbChanged)
    Q_PROPERTY(int logRetentionFiles READ logRetentionFiles WRITE setLogRetentionFiles NOTIFY logRetentionFilesChanged)
    // Batch 22 (F8): user-editable system prompt; empty string = built-in.
    Q_PROPERTY(QString systemPromptOverride READ systemPromptOverride WRITE setSystemPromptOverride NOTIFY systemPromptOverrideChanged)
    // T6 (F3): web search provider and API key (encrypted at rest via DPAPI).
    Q_PROPERTY(QString webSearchProvider READ webSearchProvider WRITE setWebSearchProvider NOTIFY webSearchProviderChanged)
    Q_PROPERTY(QString webSearchApiKey READ webSearchApiKey WRITE setWebSearchApiKey NOTIFY webSearchApiKeyChanged)
    // L1: whether a search API key is stored (the key itself is never echoed
    // to QML; the getter returns a masked empty string).
    Q_PROPERTY(bool webSearchApiKeySet READ webSearchApiKeySet NOTIFY webSearchApiKeyChanged)
    // docs/20 §5.3 (C3): self-hosted SearXNG base URL (required when the
    // provider is "searxng"; not a secret, stored in plain text).
    Q_PROPERTY(QString webSearchSearxBaseUrl READ webSearchSearxBaseUrl WRITE setWebSearchSearxBaseUrl NOTIFY webSearchSearxBaseUrlChanged)
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
    // docs/20 §4.3（D8）：开机自启（HKCU Run 键）。Windows 专属；
    // 非 Windows 平台读写都返回 false（预留扩展点）。
    Q_PROPERTY(bool autoStart READ autoStart WRITE setAutoStart NOTIFY autoStartChanged FINAL)

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
    void setContextTokens(int tokens);

    int maxSteps() const { return m_maxSteps; }
    void setMaxSteps(int steps);
    int taskTimeoutMinutes() const { return m_taskTimeoutMinutes; }
    void setTaskTimeoutMinutes(int minutes);

    // Security: allow the agent to read files outside the user-directory
    // allowlist (audit P1-8). Opt-in, default off.
    bool allowFullDiskRead() const { return m_allowFullDiskRead; }
    void setAllowFullDiskRead(bool enabled);

    // docs/17 P1-4: allow the agent to write into the active project
    // directory (A1 keeps project dirs read-only by default). Opt-in,
    // default off; consumed by Sandbox via main.cpp applySettings.
    bool allowProjectWrite() const { return m_allowProjectWrite; }
    void setAllowProjectWrite(bool enabled);

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
    QString logDirectory() const;
    void setLogDirectory(const QString& directory);
    int logMaxFileSizeMb() const { return m_logMaxFileSizeMb; }
    void setLogMaxFileSizeMb(int sizeMb);
    int logRetentionFiles() const { return m_logRetentionFiles; }
    void setLogRetentionFiles(int count);
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
    // docs/20 §5.3 (C3): self-hosted SearXNG base URL ("" = not configured).
    QString webSearchSearxBaseUrl() const { return m_webSearchSearxBaseUrl; }
    void setWebSearchSearxBaseUrl(const QString& url);

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

    // docs/20 §4.3（D8）：开机自启。实现为 HKCU\...\Run 的 "NexusAI" 键
    // （值 = exe 全路径）。Windows 之外的实现为 no-op（false）。
    bool autoStart() const;
    void setAutoStart(bool enabled);

    // docs/17 A1: 读取黑名单用户追加项（QVariantList of QString）。
    Q_PROPERTY(QVariantList readDenyList READ readDenyList WRITE setReadDenyList NOTIFY readDenyListChanged FINAL)
    QVariantList readDenyList() const { return m_readDenyList; }
    void setReadDenyList(const QVariantList& v);

    // docs/17 A0: 当前活动项目 id 的持久化（运行时真值在 AgentViewModel，
    // main.cpp 双向桥接）。空 = 无项目。
    Q_PROPERTY(QString activeProjectId READ activeProjectId WRITE setActiveProjectId NOTIFY activeProjectIdChanged FINAL)

    static QString appVersion() { return QCoreApplication::applicationVersion(); }

    // ── Profile management (QML invokables) ───────────
    // Create a profile of the given kind and return its id. 合法 kind 是各厂商
    // 键加 "custom"；"custom" 得到一条空白的 OpenAI 兼容配置（baseUrl / model /
    // models 都不预填，供自建与第三方网关用），其余 kind 按厂商默认端点预填。
    // 未知 kind 回落 "openai"。
    Q_INVOKABLE QString addProfile(const QString& kind);
    // Merge the given fields (displayName/kind/provider/apiProtocol/baseUrl/
    // apiKey/model/models) into the profile; unknown fields and unregistered
    // kind/provider/apiProtocol values are ignored. provider and apiProtocol are
    // validated against the protocol registry and kept consistent as a pair:
    // submitting a vendor resets the protocol to that vendor's default,
    // submitting a protocol rewrites the vendor to the one that speaks it, and
    // submitting kind="custom" moves the vendor axis to openai (custom rides
    // openai-chat — it is a UI axis, not a fifth wire protocol).
    Q_INVOKABLE void updateProfile(const QString& id, const QVariantMap& fields);
    Q_INVOKABLE void removeProfile(const QString& id);
    // 通用设置页中由 SettingsService 管理的 9 项回写 schema 默认值；偏好
    // 存储项由 GeneralPage 另行通过 PreferenceStore.resetBatch() 复位。
    // 逐项走既有 setter，保证持久化与 NOTIFY 一致。
    Q_INVOKABLE void resetGeneralToDefaults();

    Q_INVOKABLE void setActiveProfile(const QString& id);
    // Switch active model for a specific profile (or active profile if id is empty)
    Q_INVOKABLE void selectModelForProfile(const QString& id, const QString& model);
    // Add a model name to a profile's model list if not present. 当前模型为空时
    // （新建配置没有预填）顺手把它设为当前模型，省得用户加完还卡在需填模型。
    Q_INVOKABLE void addModelToProfile(const QString& id, const QString& model);
    // Remove a model name from a profile's model list
    Q_INVOKABLE void removeModelFromProfile(const QString& id, const QString& model);
    // 某厂商的全部可选协议（{apiProtocol, label, baseUrl, model}）。只有一条时
    // UI 不显示协议选择行——anthropic / ollama 目前就是如此。切换端点后的
    // baseUrl / 当前模型 / 模型列表由 updateProfile 单点重算，UI 不反查默认值。
    Q_INVOKABLE QVariantList endpointOptions(const QString& provider) const;
    // Localized display name for a provider key (legacy profile migration).
    QString providerDisplayName(const QString& provider) const;

    // Query the capability-table context window for a given protocol+model.
    // Returns 0 when the protocol or model is unknown (caller should fall
    // back to the global contextTokens). 按协议查：同一厂商的两条协议可用的
    // 模型集合不一样。Invoked from QML (ModelConfigPage / ModelSelector).
    Q_INVOKABLE int modelContextWindow(const QString& apiProtocol, const QString& model) const;

    // Resolve the effective context budget for the active profile using a
    // three-level priority: Profile-level "contextLength" override >
    // capability-table contextWindow > global contextTokens. Consumed by
    // main.cpp when wiring AIAgent::setContextLimit.
    int resolveContextLimit() const;
    int resolveContextLimitForProfile(const QVariantMap& profile) const;

    // docs/20 §4.3：开机自启的注册表读写封装（可注入，test_settings 用它
    // 在不碰真实 Run 键的前提下校验读写路径）。默认实现（nullptr）为
    // HKCU\...\Run 键的读写；非 Windows 平台恒 false。
    class AutoStartBackend {
    public:
        virtual ~AutoStartBackend() = default;
        virtual bool isEnabled() const = 0;
        virtual bool setEnabled(bool enabled, const QString& exePath) = 0;
    };
    void setAutoStartBackend(AutoStartBackend* backend);

signals:
    void persistenceError(const QString& message);
    void profilesChanged();
    void activeProfileChanged();
    void contextTokensChanged();
    void maxStepsChanged();
    void taskTimeoutMinutesChanged();

    void allowFullDiskReadChanged();
    void allowProjectWriteChanged();
    void localOnlyModeChanged();
    void allowLocalNetworkFetchChanged();
    void embeddingModelChanged();
    void embeddingBaseUrlChanged();
    void logLevelChanged();
    void logDirectoryChanged();
    void logMaxFileSizeMbChanged();
    void logRetentionFilesChanged();
    void systemPromptOverrideChanged();
    void webSearchProviderChanged();
    void webSearchApiKeyChanged();
    void webSearchSearxBaseUrlChanged();
    void episodicMemoryEnabledChanged();
    void dailyTokenQuotaChanged();
    void dailyCostQuotaCentsChanged();
    void monthlyTokenQuotaChanged();
    void monthlyCostQuotaCentsChanged();
    void strictQuotaModeChanged();
    void defaultPermissionModeChanged();
    void activeProjectIdChanged();
    void readDenyListChanged();
    void autoStartChanged();

private:
    bool persistValue(const QString& key, const QVariant& value);
    void load();
    QVariantList migrateLegacy();
    bool persist();

    QVariantList m_profiles;   // each entry: {id, displayName, kind, provider, apiProtocol, baseUrl, apiKey, model, models}
    QString m_activeId;
    int m_contextTokens{};
    int m_maxSteps{};
    int m_taskTimeoutMinutes{};

    bool m_allowFullDiskRead{};
    bool m_allowProjectWrite{};   // docs/17 P1-4: 项目目录写入放行
    bool m_localOnlyMode{};
    bool m_allowLocalNetworkFetch{};
    QString m_embeddingModel;
    QString m_embeddingBaseUrl;
    QString m_logLevel;
    QString m_logDirectory;
    int m_logMaxFileSizeMb{};
    int m_logRetentionFiles{};
    QString m_systemPromptOverride;
    QString m_webSearchProvider;
    QString m_webSearchApiKey;
    QString m_webSearchSearxBaseUrl;   // docs/20 §5.3: SearXNG instance URL
    bool m_episodicMemoryEnabled{};
    // docs/16 §7.3: quota control state (0 = unlimited for int quotas).
    int m_dailyTokenQuota{};
    int m_dailyCostQuotaCents{};
    int m_monthlyTokenQuota{};
    int m_monthlyCostQuotaCents{};
    bool m_strictQuotaMode{};
    QString m_defaultPermissionMode;
    QString m_activeProjectId;
    QVariantList m_readDenyList;

    AutoStartBackend* m_autoStart{nullptr};   // null = platform default

    static SettingsService* s_instance;
};

} // namespace NexusAI
