#pragma once

#include <QCoreApplication>
#include <QHash>
#include <QQmlEngine>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

// 步数预算的默认值与钳制边界都定义在 core 层（AIAgent），此处引用同一常量，
// 避免两处各写一个字面量后漂移。
#include "core/agent/AIAgent.h"
#include "core/utils/NetworkProxyConfig.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace NexusAI {
// 前置声明：测试缝的参数只需要一个不完整类型，避免把 core/tools 的头文件
// 拉进每个包含 SettingsService.h 的编译单元。
class WebSearchBackend;
}

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
    // 用户自建文本资产（提示词与指令页）：三组互相独立的 JSON 列表。
    // promptPresets: {id, name, prompt} — 系统提示预设，TopBar 一键套用到
    // 活动标签的 agent override；slashCommands: {id, name, templ, note} —
    // 发送前经 SlashCommand::expand 展开的斜杠指令，模板可含 {text} 占位；
    // sessionTemplates: {id, name, profileId, permissionMode, firstMessage}
    // — 新会话模板（profileId 空 = 不动当前配置，permissionMode -1 = 不动）。
    Q_PROPERTY(QVariantList promptPresets READ promptPresets NOTIFY promptPresetsChanged)
    Q_PROPERTY(QVariantList slashCommands READ slashCommands NOTIFY slashCommandsChanged)
    Q_PROPERTY(QVariantList sessionTemplates READ sessionTemplates NOTIFY sessionTemplatesChanged)
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
    // 外部向量模型（任务 #16）：知识库 embedding 后端选择与外部服务的
    // API key（DPAPI 加密落盘，同 webSearch 先例）。key 永不回显给 QML
    // （getter 返回掩码空串），UI 只看 embeddingApiKeySet。
    Q_PROPERTY(QString embeddingProvider READ embeddingProvider WRITE setEmbeddingProvider NOTIFY embeddingProviderChanged)
    Q_PROPERTY(QString embeddingApiKey READ embeddingApiKey WRITE setEmbeddingApiKey NOTIFY embeddingApiKeyChanged)
    Q_PROPERTY(bool embeddingApiKeySet READ embeddingApiKeySet NOTIFY embeddingApiKeyChanged)
    // 合法 provider 键（设置 UI 的单一事实源，与 main.cpp 的后端工厂共用）。
    Q_PROPERTY(QStringList embeddingProviders READ embeddingProviders CONSTANT)
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
    // 当前 provider 是否需要 API Key。QML 用它决定密钥输入框的可见性。
    // 必须是 Q_PROPERTY（不能只给 Q_INVOKABLE）：函数调用形式的绑定没有
    // 依赖任何 notify 信号，切换 provider 后 QML 不会重算，输入框要等
    // 重新打开设置页才出现——NOTIFY 指向 webSearchProviderChanged 才能
    // 让绑定跟着切换即时更新。
    Q_PROPERTY(bool webSearchProviderNeedsApiKey READ webSearchProviderNeedsApiKey NOTIFY webSearchProviderChanged)
    // 搜索连通性测试（设置页的「测试连接」按钮）。结果三态由
    // webSearchTestChanged 统一通知，语义与 proxyTesting/proxyTestOk/
    // proxyTestResult 一致：进行中 / 是否连通 / 可直接展示的结论。
    Q_PROPERTY(bool webSearchTesting READ webSearchTesting NOTIFY webSearchTestChanged FINAL)
    Q_PROPERTY(bool webSearchTestOk READ webSearchTestOk NOTIFY webSearchTestChanged FINAL)
    Q_PROPERTY(QString webSearchTestResult READ webSearchTestResult NOTIFY webSearchTestChanged FINAL)
    // H1: outbound HTTP proxy. "system" follows the OS configuration,
    // "manual" uses host/port + optional credentials, "off" disables proxies.
    // Credentials are persisted as one DPAPI-encrypted blob; the password
    // getter is masked (same contract as the API-key properties).
    Q_PROPERTY(QString proxyMode READ proxyMode WRITE setProxyMode NOTIFY proxyModeChanged FINAL)
    Q_PROPERTY(QString proxyHost READ proxyHost WRITE setProxyHost NOTIFY proxyHostChanged FINAL)
    Q_PROPERTY(int proxyPort READ proxyPort WRITE setProxyPort NOTIFY proxyPortChanged FINAL)
    Q_PROPERTY(QString proxyUsername READ proxyUsername WRITE setProxyUsername NOTIFY proxyAuthChanged FINAL)
    Q_PROPERTY(QString proxyPassword READ proxyPassword WRITE setProxyPassword NOTIFY proxyAuthChanged FINAL)
    Q_PROPERTY(bool proxyPasswordSet READ proxyPasswordSet NOTIFY proxyAuthChanged FINAL)
    Q_PROPERTY(bool proxyTesting READ proxyTesting NOTIFY proxyTestChanged FINAL)
    Q_PROPERTY(bool proxyTestOk READ proxyTestOk NOTIFY proxyTestChanged FINAL)
    Q_PROPERTY(QString proxyTestResult READ proxyTestResult NOTIFY proxyTestChanged FINAL)
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
    // 出定义在 .cpp：成员里有 unique_ptr<WebSearchBackend>（前置声明），
    // 隐式析构会在每个包含本头文件的 TU 里要求完整类型。
    ~SettingsService() override;

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
    // 外部向量模型（任务 #16）：provider（"ollama" | "openai"）与加密
    // API key。掩码 getter 对齐 webSearchApiKey：值不回显，只暴露
    // embeddingApiKeySet。
    QString embeddingProvider() const { return m_embeddingProvider; }
    void setEmbeddingProvider(const QString& provider);
    QString embeddingApiKey() const { return {}; }   // masked getter
    void setEmbeddingApiKey(const QString& key);
    bool embeddingApiKeySet() const { return !m_embeddingApiKey.isEmpty(); }
    // 真实 key 的 C++ 出口（main.cpp 装配层注入后端用；任务 #20 修 B1：
    // 此前 main.cpp 误用掩码 getter，后端永远拿到空串导致 401）。非
    // Q_PROPERTY 且无 Q_INVOKABLE——QML 侧仍只能看到掩码空串与
    // embeddingApiKeySet，掩码语义不变。
    QString embeddingApiKeyValue() const { return m_embeddingApiKey; }
    // 合法 provider 键（与 main.cpp 的后端工厂保持同步）。
    QStringList embeddingProviders() const;
    // 拦截决策（批次三设计决策 2）：provider 请求外部服务且纯本地模式
    // 未开启时才走外部后端；纯本地模式拦截外部 embedding 请求回退本地
    // Ollama。提取为纯函数使 main.cpp applyEmbeddingConfig 的后端切换
    // 决策可单测（任务 #20 修 Mj1）。
    static bool embeddingUsesExternalProvider(const QString& provider,
                                              bool localOnlyMode);
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

    // T6 (F3): search provider ("duckduckgo" | "zhipu" | "deepseek").
    QString webSearchProvider() const { return m_webSearchProvider; }
    void setWebSearchProvider(const QString& provider);
    // 密钥按 provider 分桶（webSearch/<provider>ApiKey）。QML 侧接口保持
    // 单键语义：getter/setter 针对「当前选中的 provider」，切换 provider 时
    // 补发 webSearchApiKeyChanged，输入框据此刷新「已配置/未配置」。
    QString webSearchApiKey() const { return {}; }   // masked getter
    void setWebSearchApiKey(const QString& key);
    // L1: true when the CURRENT provider has a non-empty key stored.
    bool webSearchApiKeySet() const;
    // 真实 key 的 C++ 出口（任务 #20 修 Mj2：main.cpp 注入 webSearch 后端
    // 此前误用掩码 getter 拿到空串，用户配了 key 也报"未配置"）。
    // 非 Q_PROPERTY 且无 Q_INVOKABLE——掩码语义对 QML 保持不变。
    QString webSearchApiKeyValue() const;
    // L1: legal provider keys for the settings UI（来自
    // WebSearchProviderRegistry，与后端工厂同源）。
    QStringList webSearchProviders() const;
    // 当前 provider 是否需要 API Key（QML 据此决定是否显示密钥输入框，
    // 不再硬编码 provider 字符串）。
    bool webSearchProviderNeedsApiKey() const;
    bool webSearchTesting() const { return m_webSearchTesting; }
    bool webSearchTestOk() const { return m_webSearchTestOk; }
    QString webSearchTestResult() const { return m_webSearchTestResult; }
    // 用当前 provider + 当前密钥发一次真实检索（固定查询词），把「网络能
    // 通吗」「密钥有效吗」「配额够吗」一次问清楚。
    //
    // 为什么真的去检索而不是探端点：只有真实查询才能同时验证鉴权、配额与
    // 响应结构；探 GET / 端点只能证明 DNS 与 TLS 通，用户配错的 key 会显示
    // 「连接正常」——比不测更误导。
    //
    // 在 QtConcurrent 池线程执行：这里是 GUI 线程，而 WebSearchBackend 的
    // search() 用一个 QNetworkAccessManager + QEventLoop 阻塞等待，直接在
    // GUI 线程调会冻结界面（最长到后端超时）。
    Q_INVOKABLE void testWebSearch();
    // provider 的展示名（错误/成功文案用）；未知 key 原样返回。
    QString webSearchProviderLabel(const QString& providerKey) const;
    // 测试缝（非 Q_INVOKABLE、不进 QML）：注入替身后端，让单测能覆盖完整的
    // 异步状态机而不打真实网络。对齐 testProxyUrl() 的既有做法——那条缝
    // 存在的原因就是"让测试能把探针指向脚本化的本地服务"。
    void setWebSearchTestBackendForTesting(std::unique_ptr<WebSearchBackend> backend);

    // H1: HTTP proxy settings. proxyPassword() is a masked getter; the real
    // value stays in C++ (proxyPasswordValue) just like the API keys.
    QString proxyMode() const { return m_proxyMode; }
    void setProxyMode(const QString& mode);
    QString proxyHost() const { return m_proxyHost; }
    void setProxyHost(const QString& host);
    int proxyPort() const { return m_proxyPort; }
    void setProxyPort(int port);
    QString proxyUsername() const { return m_proxyUsername; }
    void setProxyUsername(const QString& user);
    QString proxyPassword() const { return {}; }   // masked getter
    void setProxyPassword(const QString& password);
    bool proxyPasswordSet() const { return !m_proxyPassword.isEmpty(); }
    QString proxyPasswordValue() const { return m_proxyPassword; }
    // Snapshot consumed by main() to configure the application proxy factory.
    NetworkProxyConfig proxyConfig() const;
    bool proxyTesting() const { return m_proxyTesting; }
    bool proxyTestOk() const { return m_proxyTestOk; }
    QString proxyTestResult() const { return m_proxyTestResult; }
    // Probe through the current proxy settings. Any HTTP response counts as
    // success (the proxy forwarded the request); transport errors are
    // surfaced verbatim so a dead proxy is readable, not a silent timeout.
    // QML uses testProxy(); testProxyUrl() exists so tests can point the
    // probe at a scripted local proxy.
    Q_INVOKABLE void testProxy();
    Q_INVOKABLE void testProxyUrl(const QString& url);

    // T8 (F4): episodic memory toggle (default off).
    bool episodicMemoryEnabled() const { return m_episodicMemoryEnabled; }
    void setEpisodicMemoryEnabled(bool enabled);

    // docs/23 G3: audit/token-log retention in days (0 = keep forever).
    // Enforced by the periodic prune timer in main.
    Q_PROPERTY(int auditRetentionDays READ auditRetentionDays WRITE setAuditRetentionDays NOTIFY auditRetentionDaysChanged)
    Q_PROPERTY(int tokenRetentionDays READ tokenRetentionDays WRITE setTokenRetentionDays NOTIFY tokenRetentionDaysChanged)
    int auditRetentionDays() const { return m_auditRetentionDays; }
    void setAuditRetentionDays(int days);
    int tokenRetentionDays() const { return m_tokenRetentionDays; }
    void setTokenRetentionDays(int days);

    // docs/23 D1: startup auto-backup (VACUUM INTO snapshot) + rotation.
    Q_PROPERTY(bool backupOnStartup READ backupOnStartup WRITE setBackupOnStartup NOTIFY backupOnStartupChanged)
    Q_PROPERTY(int backupKeepCount READ backupKeepCount WRITE setBackupKeepCount NOTIFY backupKeepCountChanged)
    bool backupOnStartup() const { return m_backupOnStartup; }
    void setBackupOnStartup(bool enabled);
    int backupKeepCount() const { return m_backupKeepCount; }
    void setBackupKeepCount(int count);

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

    // ── 提示词与指令（用户自建文本资产，QML invokables）──
    // 返回新条目 id；name/命令词为空时返回空串（QML 据此提示）。斜杠指令的
    // 命令词统一去掉前导 '/' 并转小写存储。所有改动即时持久化并 NOTIFY。
    Q_INVOKABLE QString addPromptPreset(const QString& name, const QString& prompt);
    Q_INVOKABLE void updatePromptPreset(const QString& id, const QString& name,
                                        const QString& prompt);
    Q_INVOKABLE void removePromptPreset(const QString& id);
    Q_INVOKABLE QString addSlashCommand(const QString& command, const QString& templ,
                                        const QString& note);
    Q_INVOKABLE void updateSlashCommand(const QString& id, const QString& command,
                                        const QString& templ, const QString& note);
    Q_INVOKABLE void removeSlashCommand(const QString& id);
    Q_INVOKABLE QString addSessionTemplate(const QString& name, const QString& profileId,
                                           int permissionMode, const QString& firstMessage);
    Q_INVOKABLE void updateSessionTemplate(const QString& id, const QVariantMap& fields);
    Q_INVOKABLE void removeSessionTemplate(const QString& id);
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
    // 暴露给 QML（GeneralPage embeddingProviderLabels 容错 fallback）。
    Q_INVOKABLE QString providerDisplayName(const QString& provider) const;

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

    QVariantList promptPresets() const { return m_promptPresets; }
    QVariantList slashCommands() const { return m_slashCommands; }
    QVariantList sessionTemplates() const { return m_sessionTemplates; }

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
    void promptPresetsChanged();
    void slashCommandsChanged();
    void sessionTemplatesChanged();
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
    void embeddingProviderChanged();
    void embeddingApiKeyChanged();
    void logLevelChanged();
    void logDirectoryChanged();
    void logMaxFileSizeMbChanged();
    void logRetentionFilesChanged();
    void systemPromptOverrideChanged();
    void webSearchProviderChanged();
    void webSearchApiKeyChanged();
    void proxyModeChanged();
    void proxyHostChanged();
    void proxyPortChanged();
    // Username/password/password-set changed together: the auth blob is
    // persisted as a unit.
    void proxyAuthChanged();
    // Any proxy setting changed; main() re-applies the factory snapshot.
    void proxyConfigChanged();
    void proxyTestChanged();
    void proxyTestFinished(bool ok, const QString& message);
    // 搜索连通性测试的状态/结果变化（三态属性共用一个通知，同代理测试）。
    void webSearchTestChanged();
    void episodicMemoryEnabledChanged();
    void auditRetentionDaysChanged();
    void tokenRetentionDaysChanged();
    void backupOnStartupChanged();
    void backupKeepCountChanged();
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
    bool persistProxyAuth();
    void finishProxyTest(bool ok, const QString& message);
    void load();
    QVariantList migrateLegacy();
    bool persist();

    QVariantList m_profiles;   // each entry: {id, displayName, kind, provider, apiProtocol, baseUrl, apiKey, model, models}
    // 提示词与指令页的三组用户资产（JSON blob 持久化，同 modelProfiles 模式）。
    QVariantList m_promptPresets;    // {id, name, prompt}
    QVariantList m_slashCommands;    // {id, name, templ, note}
    QVariantList m_sessionTemplates; // {id, name, profileId, permissionMode, firstMessage}

    // 提示词与指令资产的共享持久化：key 不存在/坏 JSON → 空列表；写回为
    // 紧凑 JSON 数组。与 modelProfiles 的手工 QJson 处理同模式，但三组
    // 资产结构简单，共用这一对函数而不是各写一份。
    static QVariantList loadStringList(const QString& key);
    bool writeStringList(const QString& key, const QVariantList& list);
    // 首次启动（键从未写入）种入默认预设/指令/模板，随 locale 本地化；
    // 用户清空列表持久化为 "[]"（非空 raw），不触发回灌——删光默认后
    // 重启仍是空。固定 id（default-*）供测试与将来升级合并定位。
    void maybeSeedPromptAssets();
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
    // 外部向量模型（任务 #16）：provider 与加密存储的 API key。
    QString m_embeddingProvider;
    QString m_embeddingApiKey;
    QString m_logLevel;
    QString m_logDirectory;
    int m_logMaxFileSizeMb{};
    int m_logRetentionFiles{};
    QString m_systemPromptOverride;
    QString m_webSearchProvider;
    // provider 键 → 解密后的 API key（"webSearch/<provider>ApiKey" 的存储舱）。
    // 只读内存持有明文，落盘一律走 protectApiKey。
    QHash<QString, QString> m_webSearchApiKeys;
    // H1: proxy state (auth password is kept decrypted in memory only).
    QString m_proxyMode{QStringLiteral("system")};
    QString m_proxyHost;
    int m_proxyPort{8080};
    QString m_proxyUsername;
    QString m_proxyPassword;
    bool m_proxyTesting{false};
    bool m_proxyTestOk{false};
    QString m_proxyTestResult;
    QNetworkAccessManager* m_proxyProbeNam{nullptr};
    QNetworkReply* m_proxyProbeReply{nullptr};

    // 搜索连通性测试的状态。测试在池线程跑，结果回到 GUI 线程后才写这三个
    // 成员并发信号（与 proxy 测试同一套「只在 GUI 线程改状态」的约定）。
    bool m_webSearchTesting{false};
    bool m_webSearchTestOk{false};
    QString m_webSearchTestResult;
    // 见 setWebSearchTestBackendForTesting；生产路径恒为空。
    std::unique_ptr<WebSearchBackend> m_webSearchTestBackend;
    bool m_episodicMemoryEnabled{};
    // docs/23 G3: retention state (0 = keep forever).
    int m_auditRetentionDays{};
    int m_tokenRetentionDays{};
    // docs/23 D1: backup state.
    bool m_backupOnStartup{};
    int m_backupKeepCount{};
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
