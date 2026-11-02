#pragma once

#include <QObject>
#include <atomic>
#include <QQmlEngine>
#include <QJsonArray>
#include <QJsonObject>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QTimer>
#include <QHash>
#include <QDate>
#include <QPointer>
#include <QSet>
#include <QVector>
#include <functional>

#include "core/agent/AgentState.h"
#include "core/agent/PermissionMode.h"
#include "ChatSessionController.h"
#include "TraceModel.h"
#include "LogModel.h"

// Global-scope forward declaration: an inline `class QFileSystemWatcher`
// inside namespace NexusAI would declare NexusAI::QFileSystemWatcher and
// never resolve to the Qt class.
class QFileSystemWatcher;

namespace NexusAI {

class AIAgent;
class IMemory;
class RagService;

// Main ViewModel for the Agent chat interface (QML singleton facade).
// Batch 29 (F7): owns the open session tabs — every tab is a
// ChatSessionController with a dedicated AIAgent, so parallel sessions run
// independently and switching tabs never interrupts a running task. The
// QML-facing conversation properties are forwarded to the active tab (their
// notifications are re-emitted on every tab switch); the knowledge base,
// layout persistence and cross-session search stay global.
//
// When an AIAgent is attached via setAgent(), task actions are delegated to
// its real LLM pipeline. A task is rejected while no agent is configured.
class AgentViewModel : public QObject {
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(AgentVM)

    // ── State ──────────────────────────────────────────
    Q_PROPERTY(int state READ stateAsInt NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged FINAL)
    // Named AgentState values for QML bindings: the core enum is a plain
    // C++ enum class without QML registration, so these constants keep
    // state comparisons in QML free of magic numbers.
    Q_PROPERTY(int stateIdle READ stateIdle CONSTANT FINAL)
    Q_PROPERTY(int stateThinking READ stateThinking CONSTANT FINAL)
    Q_PROPERTY(int stateAwaitingToolConfirm READ stateAwaitingToolConfirm CONSTANT FINAL)
    Q_PROPERTY(int stateExecutingTool READ stateExecutingTool CONSTANT FINAL)
    Q_PROPERTY(int stateSummarizing READ stateSummarizing CONSTANT FINAL)
    Q_PROPERTY(int stateCompleted READ stateCompleted CONSTANT FINAL)
    Q_PROPERTY(int stateError READ stateError CONSTANT FINAL)
    // Thinking / AwaitingToolConfirm / ExecutingTool / Summarizing
    Q_PROPERTY(bool stateBusy READ stateBusy NOTIFY stateChanged FINAL)

    // ── Trace Model ───────────────────────────────────
    // Incremental QAbstractListModel: appended/updated rows are signaled as
    // such, so the QML ListView no longer rebuilds every delegate on each
    // agent update (see TraceModel).
    Q_PROPERTY(QAbstractListModel* traceModel READ traceModel NOTIFY traceChanged FINAL)
    Q_PROPERTY(int traceCount READ traceCount NOTIFY traceChanged FINAL)
    // thinking / tool_call / tool_result / context nodes (fold toggle badge)
    Q_PROPERTY(int intermediateCount READ intermediateCount NOTIFY traceChanged FINAL)
    // 过程节点（思考/工具调用/工具结果/context）是否展开显示。
    //
    // 曾经是 AgentChatPage 的页面局部状态，开关悬浮在对话区右上角——
    // 于是 TopBar 读不到它，用户也无法在切会话/重启后保持选择。它本质
    // 是"视图偏好"而不是"某条消息的展开态"，因此上移到门面并持久化。
    Q_PROPERTY(bool showProcess READ showProcess WRITE setShowProcess NOTIFY showProcessChanged FINAL)
    // 用户消息导航索引（对话区左侧导轨的数据源）。每条 =
    // {row, messageId, preview, length, timestamp}；row 直接喂给
    // TranscriptViewport::scrollToIndex()。
    //
    // 必须是 Q_PROPERTY 而不是 Q_INVOKABLE：QML 侧要绑定它（`model:` /
    // 计数），函数调用形式的绑定不会随 traceChanged 重算——与
    // webSearchProviderNeedsApiKey 踩过的是同一个坑。
    Q_PROPERTY(QVariantList userMessageIndex READ userMessageIndex NOTIFY traceChanged FINAL)

    // ── Response ──────────────────────────────────────
    Q_PROPERTY(QString finalResponse READ finalResponse NOTIFY responseReady FINAL)

    // ── Input ─────────────────────────────────────────
    Q_PROPERTY(QString draftText READ draftText WRITE setDraftText NOTIFY draftTextChanged FINAL)
    Q_PROPERTY(QVariantList draftAttachments READ draftAttachments WRITE setDraftAttachments NOTIFY draftAttachmentsChanged FINAL)
    Q_PROPERTY(QVariantMap pendingConfirmation READ pendingConfirmation NOTIFY pendingConfirmationsChanged FINAL)
    Q_PROPERTY(QVariantMap chatModelProfile READ chatModelProfile NOTIFY chatModelChanged FINAL)
    Q_PROPERTY(bool modelConfigured READ modelConfigured NOTIFY chatModelChanged FINAL)

    // ── Session permission mode (active tab) ──────────
    // 0=变更确认 1=自动编辑 2=完全访问 3=只读/计划；常量属性供 QML 选择器
    // 建立模型。
    Q_PROPERTY(int permissionMode READ permissionMode WRITE setPermissionMode NOTIFY permissionModeChanged FINAL)
    Q_PROPERTY(int permissionModeConfirmChanges READ permissionModeConfirmChanges CONSTANT FINAL)
    Q_PROPERTY(int permissionModeAutoEdit READ permissionModeAutoEdit CONSTANT FINAL)
    Q_PROPERTY(int permissionModeFullAccess READ permissionModeFullAccess CONSTANT FINAL)
    Q_PROPERTY(int permissionModePlanOnly READ permissionModePlanOnly CONSTANT FINAL)

    // ── 提示词预设（活动标签）─────────────────────────
    // 会话级系统提示覆盖。id 为空 = 未套用（用全局 systemPromptOverride），
    // name 供界面回显"当前套用的是哪个预设"。
    Q_PROPERTY(QString activePromptPresetId READ activePromptPresetId NOTIFY promptPresetChanged FINAL)
    Q_PROPERTY(QString activePromptPresetName READ activePromptPresetName NOTIFY promptPresetChanged FINAL)

    // ── Context Panel ─────────────────────────────────
    Q_PROPERTY(bool isPanelVisible READ isPanelVisible WRITE setIsPanelVisible NOTIFY panelVisibleChanged FINAL)
    // User-resizable width of the context panel (persisted)
    Q_PROPERTY(int panelWidth READ panelWidth WRITE setPanelWidth NOTIFY panelWidthChanged FINAL)
    Q_PROPERTY(double tokenUsageRate READ tokenUsageRate NOTIFY tokenUsageChanged FINAL)
    Q_PROPERTY(int usedTokens READ usedTokens NOTIFY tokenUsageChanged FINAL)
    // Follows the live agent's context limit when attached (needs refresh
    // notifications, hence not CONSTANT).
    Q_PROPERTY(int maxTokens READ maxTokens NOTIFY tokenUsageChanged FINAL)
    // docs/16 §8: per-request cost (cents) for the TopBar live display.
    Q_PROPERTY(qint64 lastCostCents READ lastCostCents NOTIFY costChanged FINAL)
    // docs/16 §8.2: 本会话累计消耗（input+output，跨任务累加）。
    Q_PROPERTY(qint64 sessionTokensUsed READ sessionTokensUsed NOTIFY sessionTokensChanged FINAL)

    // ── Sidebar ───────────────────────────────────────
    Q_PROPERTY(bool isSidebarExpanded READ isSidebarExpanded WRITE setIsSidebarExpanded NOTIFY sidebarExpandedChanged FINAL)
    // User-resizable width of the sidebar (persisted)
    Q_PROPERTY(int sidebarWidth READ sidebarWidth WRITE setSidebarWidth NOTIFY sidebarWidthChanged FINAL)
    Q_PROPERTY(QVariantList sessionList READ sessionList NOTIFY sessionListChanged FINAL)
    Q_PROPERTY(QString currentSessionId READ currentSessionId NOTIFY currentSessionIdChanged FINAL)
    // docs/17 A0: 项目级会话。projects = 已导入项目列表；activeProjectId
    // 为空表示"本地对话"视图（只显示 project_id 为 NULL 的会话）。
    Q_PROPERTY(QVariantList projects READ projects NOTIFY projectsChanged FINAL)
    Q_PROPERTY(QString activeProjectId READ activeProjectId WRITE setActiveProjectId NOTIFY activeProjectIdChanged FINAL)
    // G2: the active tab's project root drives '@' completion; scanning is
    // asynchronous so the popup can show its loading state.
    Q_PROPERTY(QString activeProjectRoot READ activeProjectRoot NOTIFY activeProjectIdChanged FINAL)
    Q_PROPERTY(bool projectFilesScanning READ projectFilesScanning NOTIFY projectFilesChanged FINAL)
    // Batch 17 (FEAT-1702): cross-session message search results, aggregated
    // per session (Batch 22): {sessionId, sessionTitle, hitCount,
    // latestTimestamp, archived, bestSnippet, bestRole, firstHitMessageId}.
    Q_PROPERTY(QVariantList searchResults READ searchResults NOTIFY searchResultsChanged FINAL)

    // ── Layout defaults (General Settings) ─────────────
    Q_PROPERTY(int defaultSidebarWidth READ defaultSidebarWidth WRITE setDefaultSidebarWidth NOTIFY defaultSidebarWidthChanged FINAL)
    Q_PROPERTY(int defaultPanelWidth READ defaultPanelWidth WRITE setDefaultPanelWidth NOTIFY defaultPanelWidthChanged FINAL)
    Q_PROPERTY(bool defaultSidebarExpanded READ defaultSidebarExpanded WRITE setDefaultSidebarExpanded NOTIFY defaultSidebarExpandedChanged FINAL)
    Q_PROPERTY(bool defaultPanelVisible READ defaultPanelVisible WRITE setDefaultPanelVisible NOTIFY defaultPanelVisibleChanged FINAL)
    Q_PROPERTY(bool restoreWindowOnStartup READ restoreWindowOnStartup WRITE setRestoreWindowOnStartup NOTIFY restoreWindowOnStartupChanged FINAL)

    // ── Tool Registry ─────────────────────────────────
    Q_PROPERTY(QVariantList toolRegistryModel READ toolRegistryModel NOTIFY toolRegistryModelChanged FINAL)

    // P5a: task list managed by the todo_write tool (active tab).
    Q_PROPERTY(QVariantList todoList READ todoList NOTIFY todoListChanged FINAL)
    // G4: files the active session changed (newest first).
    Q_PROPERTY(QVariantList fileCheckpoints READ fileCheckpoints NOTIFY fileCheckpointsChanged FINAL)
    // UE-1: 有多少个本次会话改动的文件用户还没看过。> 0 时顶栏审阅按钮
    // 高亮；关闭审阅窗口即调用 acknowledgeFileReview() 清零。
    Q_PROPERTY(int unreviewedFileCount READ unreviewedFileCount NOTIFY unreviewedFileCountChanged FINAL)

    // ── Knowledge Base ────────────────────────────────
    Q_PROPERTY(QVariantList knowledgeDocuments READ knowledgeDocuments NOTIFY knowledgeChanged FINAL)
    Q_PROPERTY(bool knowledgeBusy READ knowledgeBusy NOTIFY knowledgeBusyChanged FINAL)
    Q_PROPERTY(int knowledgeEmbedDone READ knowledgeEmbedDone NOTIFY knowledgeEmbedProgressChanged)
    Q_PROPERTY(int knowledgeEmbedTotal READ knowledgeEmbedTotal NOTIFY knowledgeEmbedProgressChanged)


    // ── In-app log panel (日志开发 doc 6) ──────────────
    Q_PROPERTY(LogModel* logModel READ logModel CONSTANT)

    // ── Session tabs (Batch 29, F7) ───────────────────
    // [{index, sessionId, title, busy, active}] for the tab bar; title falls
    // back to "新会话" until the tab has a session.
    Q_PROPERTY(QVariantList tabModel READ tabModel NOTIFY tabsChanged FINAL)
    Q_PROPERTY(int activeTabIndex READ activeTabIndex NOTIFY activeTabChanged FINAL)

public:
    // docs/30：同时打开的会话数上限。侧栏模型下"点一次会话"就可能新起一个
    // controller（= 一个 AIAgent + 一条 SQLite 连接 + 最多 800 条 trace 节点
    // 恢复），标签时代那个"用户懒得点 +"的隐含限制不存在了，必须显式设界。
    // 12 的依据：每个 controller 占一条 SQLite 连接（WAL + busy_timeout 5s），
    // 12 条远低于 SQLite 的实际压力，也在桌面内存预算之内。
    static constexpr int kMaxOpenConversations = 12;

    // No default constructor on purpose: a default-constructible
    // QML_SINGLETON makes the engine prefer Constructor over the create()
    // factory (qqmlprivate's construction-mode check), yielding a second,
    // un-injected instance.
    explicit AgentViewModel(QObject* parent);
    ~AgentViewModel() override;

    // QML singleton plumbing: main.cpp owns the instance (dependency
    // injection happens there); the engine picks the same object up through
    // create() when loading the UI.
    static AgentViewModel* instance();

    // In-app log panel: live log model + log-directory opener (doc 6)
    LogModel* logModel() const { return m_logModel; }
    Q_INVOKABLE void openLogFolder();
    // docs/33 E1: 打开子代理证据工件（系统默认 .md 处理器）。仅放行证据
    // 根目录内的真实文件；返回 false = 路径校验未过或系统无法打开。
    Q_INVOKABLE bool openSubagentEvidence(const QString& path);

    // FEAT-803: batch-embedding progress (wired from OllamaEmbedding)
    int knowledgeEmbedDone() const { return m_embedDone; }
    int knowledgeEmbedTotal() const { return m_embedTotal; }
    void setEmbedProgress(int done, int total);
    static AgentViewModel* create(QQmlEngine*, QJSEngine*);

    // Attach the shared agent (tab 0 + template). Takes no ownership
    // (agent outlives the VM); nullptr detaches the current pipeline.
    void setAgent(AIAgent* agent);
    // Batch 29: factory building a fresh AIAgent (+ own SQLite connection)
    // for every additional tab.
    void setAgentFactory(std::function<AIAgent*()> factory);
    void setModelConfigurator(std::function<void(AIAgent*, const QVariantMap&)> configure);
    void updateModelProfiles(const QVariantList& profiles, const QVariantMap& defaultProfile);
    QVariantMap chatModelProfile() const;
    bool modelConfigured() const;
    Q_INVOKABLE void selectChatModel(const QString& profileId, const QString& model = QString());
    Q_INVOKABLE void setChatReasoningEffort(const QString& effort);
    Q_INVOKABLE void openModelSettings() { emit modelSettingsRequested(); }
    // Apply global runtime preferences to attached agents. Model profiles
    // are configured separately per controller and frozen while busy.
    void applyToAgents(const std::function<void(AIAgent*)>& fn);
    // Batch 17: attach the persistence backend for search/export. Takes no
    // ownership; nullptr disables those features.
    void setMemory(IMemory* memory);
    // Attach the knowledge-base service (optional). Takes no ownership.
    void setRagService(RagService* rag);
    // Provide persisted knowledge document content (used by re-embedding).
    // Called on the GUI thread; return empty when the document is unknown.
    void setKnowledgeContentProvider(std::function<QString(const QString&)> provider);


    // State accessors (active tab)
    int stateAsInt() const { return static_cast<int>(active()->state()); }
    AgentState state() const { return active()->state(); }
    QString stateText() const { return active()->stateText(); }

    // Named enum constants for QML (see the Q_PROPERTY block above)
    int stateIdle() const { return static_cast<int>(AgentState::Idle); }
    int stateThinking() const { return static_cast<int>(AgentState::Thinking); }
    int stateAwaitingToolConfirm() const { return static_cast<int>(AgentState::AwaitingToolConfirm); }
    int stateExecutingTool() const { return static_cast<int>(AgentState::ExecutingTool); }

    // Session permission mode of the ACTIVE tab (draftText pattern): the
    // controller is authoritative per tab; switching tabs re-notifies.
    int permissionMode() const { return active()->permissionMode(); }
    void setPermissionMode(int mode);
    int permissionModeConfirmChanges() const { return static_cast<int>(PermissionMode::ConfirmChanges); }
    int permissionModeAutoEdit() const { return static_cast<int>(PermissionMode::AutoEdit); }
    int permissionModeFullAccess() const { return static_cast<int>(PermissionMode::FullAccess); }
    int permissionModePlanOnly() const { return static_cast<int>(PermissionMode::PlanOnly); }

    // 提示词预设（活动标签）：套用/清除都只影响当前标签。
    QString activePromptPresetId() const
    { return active() ? active()->promptPresetId() : QString(); }
    QString activePromptPresetName() const
    { return active() ? active()->promptPresetName() : QString(); }
    // Global default (from SettingsService) applied to newly created tabs;
    // main.cpp injects it and re-injects when the setting changes. Also
    // retro-applies to tabs the user has not explicitly switched.
    void setDefaultPermissionMode(int mode);
    int stateSummarizing() const { return static_cast<int>(AgentState::Summarizing); }
    int stateCompleted() const { return static_cast<int>(AgentState::Completed); }
    int stateError() const { return static_cast<int>(AgentState::Error); }
    bool stateBusy() const { return active()->isBusy(); }

    // Trace model (active tab)
    QAbstractListModel* traceModel() { return active()->trace(); }
    int traceCount() const { return active()->traceCount(); }
    int intermediateCount() const { return active()->intermediateCount(); }
    // 过程显示偏好是全局的（跨会话、跨标签），不随当前会话变化——
    // 用户调的是"我想不想看过程"，与正在看哪条会话无关。
    bool showProcess() const { return m_showProcess; }
    void setShowProcess(bool show);
    QVariantList userMessageIndex() const { return active()->userMessageIndex(); }
    // docs/20 §3.1/§3.2：该类型 trace 节点的最后一行（消息级操作按钮用）。
    Q_INVOKABLE int lastTraceIndexOfType(const QString& type) const
    { return active()->lastIndexOfType(type); }

    // Response (active tab)
    QString finalResponse() const { return active()->finalResponse(); }

    // Draft text (active tab)
    QString draftText() const { return active()->draftText(); }
    void setDraftText(const QString& text);
    QVariantList draftAttachments() const { return active()->draftAttachments(); }
    void setDraftAttachments(const QVariantList& attachments) { active()->setDraftAttachments(attachments); }
    QVariantMap pendingConfirmation() const;
    // docs/20 §3.3：输入历史回溯（活动标签）。↑ 返回上一条历史，↓ 返回下一条；
    // 无更多历史或已回到草稿态时返回空串。编辑后调用 resetInputHistory 复位游标。
    Q_INVOKABLE QString historyPrev() { return active()->historyPrev(); }
    Q_INVOKABLE QString historyNext() { return active()->historyNext(); }
    Q_INVOKABLE void resetInputHistory() { active()->resetHistoryCursor(); }

    // Panel
    bool isPanelVisible() const { return m_isPanelVisible; }
    void setIsPanelVisible(bool visible);
    int panelWidth() const { return m_panelWidth; }
    void setPanelWidth(int width);

    // Sidebar
    bool isSidebarExpanded() const { return m_isSidebarExpanded; }
    void setIsSidebarExpanded(bool expanded);
    int sidebarWidth() const { return m_sidebarWidth; }
    void setSidebarWidth(int width);

    // Layout defaults
    int defaultSidebarWidth() const { return m_defaultSidebarWidth; }
    void setDefaultSidebarWidth(int width);
    int defaultPanelWidth() const { return m_defaultPanelWidth; }
    void setDefaultPanelWidth(int width);
    bool defaultSidebarExpanded() const { return m_defaultSidebarExpanded; }
    void setDefaultSidebarExpanded(bool expanded);
    bool defaultPanelVisible() const { return m_defaultPanelVisible; }
    void setDefaultPanelVisible(bool visible);
    bool restoreWindowOnStartup() const { return m_restoreWindowOnStartup; }
    void setRestoreWindowOnStartup(bool restore);

    // Window geometry persistence (启动时恢复窗口). The QML window saves on
    // close and restores on startup; loadWindowGeometry returns an empty map
    // when nothing was saved and valid=false for off-screen positions.
    Q_INVOKABLE QVariantMap loadWindowGeometry() const;
    Q_INVOKABLE void saveWindowGeometry(double x, double y, double width, double height, bool maximized);

    // Sessions
    // docs/30：m_sessionList 是数据库快照（refreshSessions 写入），对 QML
    // 暴露时要叠加**运行时键**——哪些会话正被 controller 承载、哪个是活动、
    // 是否在跑、是否有草稿、是否后台跑完未读。叠在读取端而不是写入端，是因为
    // busy/unread 随任务变化而数据库快照不变，写进快照会反复失效重拉。
    QVariantList sessionList() const;

    // docs/17 A0: 项目级会话。projects 为已导入项目列表
    // [{id, name, rootPath}]；activeProjectId 为空 = 本地对话视图。
    QVariantList projects() const { return m_projects; }
    // docs/18: 派生自**活动标签**的 agent —— 不存在全局活动项目。
    QString activeProjectId() const;
    QString activeProjectRoot() const { return active()->activeProjectRoot(); }
    bool projectFilesScanning() const { return m_projectFilesScanning; }
    // G2: '@' file mentions. Completion is served from a background-scanned
    // cache (30s TTL); references are resolved against the active project
    // root when the message is submitted.
    Q_INVOKABLE QVariantList listProjectFiles(const QString& query, int limit = 50);
    Q_INVOKABLE QVariantMap activeFileMention(const QString& text, int cursor) const;
    Q_INVOKABLE QVariantList resolveFileReferences(const QString& text);
    // 设置**活动标签**的项目归属（不再广播给所有标签）；同时作为
    // activeProjectId 属性的 WRITE 端。
    Q_INVOKABLE void setActiveProjectId(const QString& id);
    // FolderDialog 的本地路径导入为项目（canonicalize，项目名取目录名），
    // 返回项目 id（失败返回空串 + noticeOccurred）。**仅当活动标签归属未定**
    // （尚无会话）时才顺带把它设为该标签的目标——docs/18 G5：归属落定后只读。
    Q_INVOKABLE QString importProject(const QString& path);
    // 删除项目：其会话保留为本地会话（project_id 置 NULL）。活动中的
    // 项目被删除时回落本地视图。
    Q_INVOKABLE void removeProject(const QString& projectId);
    // P2: 项目重命名（侧栏行内编辑），上限 80 字符；活动项目改名后
    // 系统提示里的项目名同步更新。
    Q_INVOKABLE void renameProject(const QString& projectId, const QString& name);
    QString currentSessionId() const { return active()->currentSessionId(); }
    // Batch 17: message-search results (set via the searchMessages invokable).
    // Batch 22 (search-aggregate): one row per session
    // {sessionId, sessionTitle, hitCount, latestTimestamp, archived,
    // bestSnippet, bestRole, firstHitMessageId}, newest hit first. A1 adds
    // firstHitMessageId (jump target) and archived (card badge).
    QVariantList searchResults() const { return m_searchResults; }

    // Token management (active tab)
    double tokenUsageRate() const;
    int usedTokens() const { return active()->usedTokens(); }
    int maxTokens() const { return active()->maxTokens(); }
    // docs/16 §8: last per-request cost (cents), updated via costChanged.
    qint64 lastCostCents() const { return m_lastCostCents; }
    // docs/16 §8.2: 会话累计 token（active tab 的 AIAgent 计数器）。
    qint64 sessionTokensUsed() const { return active()->sessionTokensUsed(); }

    // Tool registry
    QVariantList toolRegistryModel() const { return m_toolRegistryModel; }
    QVariantList todoList() const { return m_todoList; }
    QVariantList fileCheckpoints() const { return m_fileCheckpoints; }
    int unreviewedFileCount() const { return m_unreviewedFileCount; }

    // Knowledge base
    QVariantList knowledgeDocuments() const { return m_knowledgeDocuments; }
    bool knowledgeBusy() const { return m_knowledgeBusy; }

    // ── docs/25: 批量知识库导入 ─────────────────────────
    Q_PROPERTY(bool batchImportRunning READ batchImportRunning NOTIFY batchImportProgressChanged)
    Q_PROPERTY(int batchImportDone READ batchImportDone NOTIFY batchImportProgressChanged)
    Q_PROPERTY(int batchImportTotal READ batchImportTotal NOTIFY batchImportProgressChanged)
    Q_PROPERTY(QString batchImportCurrent READ batchImportCurrent NOTIFY batchImportProgressChanged)
    Q_PROPERTY(int batchImportFailedCount READ batchImportFailedCount NOTIFY batchImportProgressChanged)
    // 取消已请求、正在等在飞文件结束（docs/25）。
    Q_PROPERTY(bool batchImportCancelling READ batchImportCancelling NOTIFY batchImportProgressChanged)
    bool batchImportRunning() const;
    int batchImportDone() const;
    int batchImportTotal() const { return m_batchTotal; }
    QString batchImportCurrent() const { return m_batchCurrent; }
    int batchImportFailedCount() const;
    bool batchImportCancelling() const { return m_batchRunning && m_batchCancelRequested; }
    // 逐文件结果 {path, name, status, message, chunks}；status: ok | degraded |
    // duplicate | failed | cancelled。done = 该列表长度，total 为本轮
    // （含重试时保留的历史条目）的文件总数，二者随重试保持自洽。
    Q_INVOKABLE QVariantList batchImportResults() const { return m_batchResults; }

    // ── Session tabs (Batch 29) ───────────────────────
    QVariantList tabModel() const;
    int activeTabIndex() const { return m_activeTab; }
    Q_INVOKABLE void openTab();
    // docs/17: 新建会话的归属规则集中在 VM —— 默认新建的是**普通对话**
    // （本地，不带项目），只有项目行提供的「+」（newProjectChat）才创建
    // 项目会话。新 tab 的归属由 ensureTab 从"当前活动项目"继承，所以这里
    // 必须先切活动项目再开 tab；把这一步收进 VM 可避免每个 UI 入口各自
    // 记得顺序、漏一处就落进当前项目。
    Q_INVOKABLE void newLocalChat();
    Q_INVOKABLE void newProjectChat(const QString& projectId);
    // 会话模板一键开新会话：{id, name, profileId, permissionMode, firstMessage}。
    // 应用逻辑只在这里实现一份——侧栏窄条、标签栏「+」、空态入口都调它，
    // 避免每个入口各自复制那段"顺序敏感"的步骤。
    //
    // 顺序是**语义要求**而非风格：
    //  1. setActiveProfile 必须在 newLocalChat **之前**——addTab 内部的
    //     configureModel 只在 controller「未选模型且无 sessionId」时取
    //     m_defaultProfile 的快照，晚一步新标签就落回旧配置；
    //  2. permissionMode 必须在 newLocalChat **之后**——它写的是活动标签；
    //  3. firstMessage 进草稿待确认，绝不自动发送（对用户是硬承诺）。
    Q_INVOKABLE void startFromSessionTemplate(const QVariantMap& tmpl);
    // Closing a running tab stops its task (the agent seals unanswered tool
    // calls); the last remaining tab cannot be closed.
    Q_INVOKABLE void closeTab(int index);
    Q_INVOKABLE void activateTab(int index);

    // ── 单对话视图（docs/30）──────────────────────────
    // 侧栏是唯一的会话入口，主区只渲染**活动会话**。下面三个入口覆盖侧栏
    // 需要的全部语义；标签栏停用后，用户可及的切换/关闭路径改由它们承担
    // （activateTab/closeTab 保留，引擎语义与既有单测不变）。
    //
    // openConversation：点侧栏某条会话。幂等——已有 controller 承载该会话
    // 时直接激活（后台 busy 的会话由此切回来，任务不中断）；没有承载者时
    // 复用空闲 controller 或新建一个。**忙的 controller 不能换会话**
    // （AIAgent::resumeSession 在 m_active 时直接返回），所以"后台有任务时
    // 点另一条会话"必然多开一个 controller —— 这是既有约束，不是选择。
    Q_INVOKABLE void openConversation(const QString& sessionId);
    // closeConversation：结束一条会话的**视图**（停任务 + 释放 controller），
    // 不删数据——会话仍在侧栏历史里，点它可重新打开。与「删除会话」是两件事。
    Q_INVOKABLE void closeConversation(const QString& sessionId);
    // clearConversationUnread：会话被看到时清掉"完成未读"点（激活即清）。
    Q_INVOKABLE void clearConversationUnread(const QString& sessionId);
    // closeActiveConversation：Ctrl+W 的落点。等价于 closeConversation(活动会话
    // 的 id)；活动 controller 还没绑定会话（空白对话）时直接换一个新对话。
    Q_INVOKABLE void closeActiveConversation();
    // cycleConversation(delta)：Ctrl+Tab / Ctrl+Shift+Tab 在侧栏会话列表
    // （未归档）上循环，openConversation 幂等落点——已打开的直接激活，
    // 没打开的现场加载；归档会话不参与。
    Q_INVOKABLE void cycleConversation(int delta);
    // ── QML-invokable actions ─────────────────────────
    // Batch 30 (F6): images are compressed data URLs; files are
    // {kind:"file", name, content} text attachments folded into the request.
    Q_INVOKABLE bool startTask(const QString& input, const QStringList& images = {},
                               const QVariantList& files = {});
    // 斜杠指令展开（发送前由 InputConsole 调用）。commands 来自
    // Settings.slashCommands（{name, templ}），展开规则见
    // SlashCommand::expand——非斜杠/未知命令原样返回，匹配则整体替换。
    // 纯静态：expansion 逻辑在 core/utils/SlashCommand.h，可单测。
    Q_INVOKABLE static QString expandSlashCommand(const QString& input,
                                                  const QVariantList& commands);
    // 把提示词预设套用到活动标签（会话级系统提示覆盖）。id 为空 = 清除。
    // 仅影响当前标签，其它标签与全局设置（Settings.systemPromptOverride）
    // 不动；清除后回落到全局提示词。
    Q_INVOKABLE void applyPromptPreset(const QString& id, const QString& name,
                                       const QString& prompt);
    Q_INVOKABLE void clearPromptPreset();
    // main.cpp 的全局系统提示链路调用：记进每个标签（清除预设时回落用），
    // 只下发给尚未套用预设的标签。
    void applyGlobalSystemPrompt(const QString& prompt);
    // 预设列表变化（编辑/改名）时把新文本推给已套用该预设的标签。
    void syncPromptPresets(const QVariantList& presets);
    // Compressed data URL of the clipboard image (empty when none).
    Q_INVOKABLE QString clipboardImageDataUrl() const;
    // Compressed data URL of an image file picked in QML (empty when unreadable).
    Q_INVOKABLE QString loadImageDataUrl(const QString& path) const;
    // {kind:"file", name, content} for a text attachment; empty map on
    // rejection (too large / binary).
    Q_INVOKABLE QVariantMap loadTextFileAttachment(const QString& path);
    // docs/20 §3.4：拖入 N 个不支持的文件时统一提示（QML 无法直接 emit
    // noticeOccurred，经此转发）。
    Q_INVOKABLE void notifyUnsupportedDrop(int count);
    // docs/20 §3.4：处理窗口级拖放的 URL 列表（由 main.qml 的根级 DropArea
    // 直接传入 drop.urls）。**必须收 QUrl/URL 原始值而非 QML 侧解析好的
    // 路径**：QML 的 url 对象没有 toLocalFile()，在 QML 里转路径会抛
    // TypeError 并让整个拖放静默失效（A4 首版即栽在这里）。这里用 QUrl
    // 做 isLocalFile/toLocalFile（含百分号解码），再逐个分类 → 图片压缩 /
    // 文本读取 → 汇总为附件描述符，经 attachmentsDropped 通知 QML。
    Q_INVOKABLE void handleDroppedFiles(const QList<QUrl>& urls);
    Q_INVOKABLE void approveToolCall(const QString& id, bool approved);
    Q_INVOKABLE void setToolEnabled(const QString& name, bool enabled);
    Q_INVOKABLE void stop();                 // abort generation, keep trace
    // 只停活动标签在飞的子代理（主任务继续）；返回是否有子代理被中止。
    Q_INVOKABLE bool cancelSubAgents();
    // docs/35 P4.5: 只停止指定 trace 节点（ProcessCard 的「停止」）。
    Q_INVOKABLE bool cancelSubAgent(const QString& traceNodeId);
    // docs/25 A4：停**全部**标签的任务（托盘退出等收尾路径用），替代只停
    // active tab 的 stop()——后者让其余 busy tab 依赖 app 析构 → controller
    // 析构的被动兜底（隐式依赖 QObject 析构顺序）。
    Q_INVOKABLE void stopAllTabs();
    // docs/20 §3.1（FORK-701）：重新生成最后一条回答（活动标签）。
    Q_INVOKABLE void regenerate();

    // docs/21：从第 traceIndex 行这条回答结尾分叉成新会话并切过去。
    Q_INVOKABLE bool branchFromAnswer(int traceIndex);
    // docs/20 §3.2（FORK-701）：编辑最后一条提问并重发（截断后按新内容重新发起）。
    Q_INVOKABLE bool editAndResend(const QString& newText);
    Q_INVOKABLE void newChat();              // reset to a fresh session
    // Loads into the active tab; when the active tab is busy a new tab is
    // opened for the session instead — switching never interrupts a task.
    Q_INVOKABLE void loadSession(const QString& sessionId);
    Q_INVOKABLE void deleteSession(const QString& sessionId);
    // docs/23 G7: undo a soft-deleted session within the toast window.
    Q_INVOKABLE bool undoDeleteSession(const QString& sessionId);
    // docs/23 D1: write a manual VACUUM INTO snapshot into the backups
    // directory. Empty string = success, otherwise a user-facing error.
    Q_INVOKABLE QString backupDatabaseNow();
    // docs/23 D1: read-only peek into a backup file for the preview
    // dialog: {sessions, messages, knowledgeDocs, newestTimestamp,
    // integrityOk, error?}.
    Q_INVOKABLE QVariantMap previewDatabaseBackup(const QString& path);
    // docs/24 R4: swap the live database for a verified snapshot (closes
    // all tabs, restores, re-runs the knowledge restore). Empty string =
    // success, otherwise a user-facing error.
    Q_INVOKABLE QString restoreDatabaseFrom(const QString& path);
    // docs/23 D1: newest snapshot in the backups directory (status line).
    Q_INVOKABLE QString lastDatabaseBackupPath() const;
    // docs/31 O3: 备份目录里实际存在的快照，最新在前。
    // [{path, fileName, sizeBytes, mtimeMs, isAuto}] — 让备份页能列出、
    // 预览、恢复与删除，而不是只能靠文件选择框猜文件名。
    Q_INVOKABLE QVariantList databaseBackups() const;
    // 删除一份快照。只接受备份目录内的文件（按目录相对路径校验，不信任
    // 传入路径）。空串 = 成功，否则是给用户看的错误原因。
    Q_INVOKABLE QString deleteDatabaseBackup(const QString& path);
    // 在文件管理器里打开备份目录（目录不存在时先建）。
    Q_INVOKABLE void openDatabaseBackupsFolder();
    // docs/31 S4.5 N5（数据管理总览）：异步统计。绝不跑在 GUI 线程连接上
    //（QtConcurrent + 自包含短连接，见 SqliteMemory::collectStats），
    // 结果经 dataStatsReady 回 GUI 线程；QML 侧 Connections 接收。
    Q_INVOKABLE void requestDataStats();
    // docs/31 S4.5 N5：按当前保留天数立即清理审计与 token 用量
    //（memory/auditRetentionDays、memory/tokenRetentionDays）。空串 = 成功。
    Q_INVOKABLE QString pruneByRetentionNow();
    // docs/31 S4.5 N5：清空全部 token 用量。空串 = 成功。
    Q_INVOKABLE QString clearUsageNow();
    // docs/31 S4.5 N5：VACUUM 压缩数据库文件（GUI 连接上执行，期间界面
    // 短暂卡顿由确认对话框预告；SQLITE_BUSY 时返回"稍后重试"提示）。
    Q_INVOKABLE QString vacuumDatabaseNow();
    Q_INVOKABLE void renameSession(const QString& sessionId, const QString& title);
    Q_INVOKABLE void setSessionPinned(const QString& sessionId, bool pinned);
    Q_INVOKABLE void setSessionArchived(const QString& sessionId, bool archived);
    // Batch 17: refresh searchResults from the memory backend (empty keyword
    // → empty results, which the sidebar treats as "back to session list").
    // Batch 22 (search-aggregate): the backend aggregates per session; the
    // hardcoded limit caps the number of SESSIONS (50).
    Q_INVOKABLE void searchMessages(const QString& keyword, bool includeArchived = false);
    // A1 (hit-locate): switch to a session and, when messageId > 0, ask the
    // chat page to scroll to & briefly highlight that persisted message.
    // The switch runs synchronously (resumeSession fills the trace model);
    // the locate intent is broadcast on the ChangeBus so the QML message
    // viewport can react after its Repeater has laid out the new session.
    Q_INVOKABLE void revealMessage(const QString& sessionId, qint64 messageId);
    // Batch 18: export a session's messages (+ trace nodes) as Markdown or
    // JSON. Returns false (and emits errorOccurred) on failure.
    Q_INVOKABLE bool exportSession(const QString& sessionId, const QString& path,
                                   const QString& format);
    Q_INVOKABLE void cancel();               // alias of newChat
    Q_INVOKABLE void togglePanel();
    Q_INVOKABLE void toggleSidebar();
    Q_INVOKABLE void copyToClipboard(const QString& text);

    // Knowledge base management (file import happens on a worker thread;
    // knowledgeChanged notifies on completion)
    Q_INVOKABLE void importDocument(const QString& path);
    // docs/25: 批量知识库导入——单个文件走既有 importDocument 原路径
    // （保留其分类错误提示）；多个文件进入串行批处理链（每文件一个池
    // 任务，GUI 线程链式推进）。进度/取消/失败重试见下方属性与方法。
    // 取消语义：不再开始队列中的后续文件，正在导入的文件会完成
    // （单文件嵌入受 120s 超时约束）。
    Q_INVOKABLE void importDocuments(const QStringList& paths);
    // docs/26 H6: fetch a web page and ingest the stripped text. Runs the
    // fetch on a worker thread (sliced event loop inside WebContent::fetchUrl),
    // then routes through the same dedupe/naming/ingestion chain as file
    // imports. source_path records the URL (the "original" is remote, not
    // local). allowPrivateTargets comes from the settings escape hatch.
    Q_INVOKABLE void importFromUrl(const QString& url);
    // Test probes for the source-path rule inside importExtractedText: a
    // display-name-only info records nothing, an override passes through.
    Q_INVOKABLE QString probeSourcePathForName(const QString& displayName);
    Q_INVOKABLE QString probeSourcePathForUrl(const QString& url);
    // G3 rules-file authoring (settings page): read/write the AGENTS.md under
    // a project root, and a state probe for the UI ("empty" | "seed" |
    // "truncated" | "active" | "missing").
    Q_INVOKABLE QString loadProjectRules(const QString& projectRoot);
    Q_INVOKABLE bool writeProjectRules(const QString& projectRoot,
                                       const QString& content);
    Q_INVOKABLE QString projectRulesState(const QString& projectRoot);
    Q_PROPERTY(bool urlImportRunning READ urlImportRunning NOTIFY urlImportRunningChanged FINAL)
    // SSRF escape hatch, mirrored from SettingsService by main.cpp (same value
    // the http_fetch tool uses).
    void setAllowPrivateTargets(bool allowed) { m_allowPrivateFetchTargets = allowed; }
    bool urlImportRunning() const { return m_urlImportRunning; }
    Q_INVOKABLE void cancelBatchImport();
    Q_INVOKABLE void retryFailedBatchImports();
    // docs/20 §5.1 (CITE-801): 打开知识库文档详情。AssistantMessage 的引用
    // 徽标点击后调用它；main.qml 把 knowledgeDocumentOpenRequested 连到
    // KnowledgeDocDialog.openFor。由 view model 中转而不是让组件去窗口里
    // 反射找对话框：显式信号链有编译期保护，也不会因对话框改名/换父级而
    // 静默失效。docs/25: chunk >= 0 时对话框定位到该 0 基分块并高亮。
    Q_INVOKABLE void openKnowledgeDocument(const QString& name, int chunk = -1);
    Q_INVOKABLE void removeDocument(const QString& name);
    Q_INVOKABLE void clearKnowledge();
    // Chunk metadata of one imported document (document detail dialog)
    Q_INVOKABLE QVariantList documentChunks(const QString& name);
    // Re-run embedding for a degraded (pseudo-mode) document, e.g. after the
    // embedding service came back online.
    Q_INVOKABLE void reembedDocument(const QString& name);
    Q_INVOKABLE QVariantList auditLog(int limit = 200);
    // M1: long-term episodic memory management (browse / search / forget).
    Q_INVOKABLE QVariantList listEpisodic(int limit = 200);
    Q_INVOKABLE QVariantList searchEpisodic(const QString& keyword, int limit = 50);
    Q_INVOKABLE bool removeEpisodic(qint64 id);
    Q_INVOKABLE int clearEpisodic();
    // usage_log 活动时间线（设置中心「用量统计」页的“活动记录”区块）：
    // 透传 IMemory::loadUsageEvents（契约注释见 IMemory.h）。每项为 map：
    // timestamp(qint64 ms) / category / action / target / requestId(double) /
    // detail，按 id DESC 最新在前；limit 为返回条数上限（默认 500）。
    // UsageStatsPage.qml 的 delegate 按这些 modelData.* 字段名消费，不可偏离。
    Q_INVOKABLE QVariantList listUsageEvents(int limit = 500) const;
    // M1 (stats): token/cost aggregation for the usage page.
    Q_INVOKABLE QVariantMap tokenStats(int recentDays = 30);
    // docs/16 §8: detailed aggregates for the extended usage page.
    // Batch 6 T1: fromTs/toTs（epoch ms，本地口径）任一 > 0 时按
    // [fromTs, toTs) 范围聚合（recentDays 被忽略），均 0 回退
    // recentDays 窗口——语义细节见 IMemory.h。
    Q_INVOKABLE QVariantMap tokenStatsDetailed(int recentDays = 30,
                                               qint64 fromTs = 0, qint64 toTs = 0);
    Q_INVOKABLE QVariantList tokenStatsBySource(int recentDays = 30,
                                                qint64 fromTs = 0, qint64 toTs = 0);
    Q_INVOKABLE QVariantMap costStats(int recentDays = 30,
                                      qint64 fromTs = 0, qint64 toTs = 0);
    Q_INVOKABLE QVariantMap quotaStatus();
    // Batch 6 T1: token_usage 明细分页（每次 LLM 请求一行）。返回
    // {rows, total}: rows 按 timestamp DESC（同刻按 id DESC）分页，
    // 每行 {requestId, sessionId, model, timestamp, inputTokens,
    // outputTokens, cacheHitTokens, latencyMs, costCents}（驼峰键）；
    // total = 范围内总行数。fromTs/toTs 均 0 = 全部记录。见 IMemory.h。
    Q_INVOKABLE QVariantMap tokenUsageRecords(qint64 fromTs = 0, qint64 toTs = 0,
                                              int limit = 50, int offset = 0);
    // 用量页「导出 CSV」：把表格当前视图（筛选 + 排序后的行）写成
    // UTF-8（带 BOM）CSV。行结构与 tokenUsageRecords 的 rows 相同。
    // 返回 false（并 emit errorOccurred）表示没有行或写入失败。
    Q_INVOKABLE bool exportUsageCsv(const QString& path, const QVariantList& rows);
    // docs/16 §8.3: 当前激活会话的 token 来源拆分（TopBar 悬停浮层）。
    // 无激活会话 id 时返回空 map，QML 据此显示空态。
    Q_INVOKABLE QVariantMap sessionTokenBreakdown() const;
    // docs/16 §8.3: 激活 tab 的实时上下文构成（容量占比浮层数据源）。
    Q_INVOKABLE QVariantMap contextComposition() const;
    Q_INVOKABLE QString logFilePath() const;   // runtime log file path (for the audit dialog)
    // Per-token status for the composer without reading file contents:
    // [{token, path, error}] where error is "", "no-project", "missing",
    // "outside" or "sensitive". Declared last so adding it does not shift the
    // metaobject method indices of existing Q_INVOKABLEs (QML AOT caches embed
    // those indices and are not invalidated by header-only changes).
    Q_INVOKABLE QVariantList inspectFileReferences(const QString& text) const;
    // G4: undo all of this session's changes to the file that owns the id.
    // Declared last for the same AOT-index reason as inspectFileReferences.
    Q_INVOKABLE bool undoFileCheckpoint(qint64 id);
    // Review's accept verdict: the change is kept, but it can no longer be
    // undone and the row reads "已确认". Returns the number of files newly
    // accepted (1 for the single-file form, N for acceptAll).
    Q_INVOKABLE int acceptFileCheckpoint(qint64 id);
    Q_INVOKABLE int acceptAllFileCheckpoints();
    // Reverts every file still awaiting a decision (the mirror of acceptAll).
    // Returns {reverted, skipped, failed[]}; the caller reports the numbers,
    // so a partial failure never reads as a full success.
    Q_INVOKABLE QVariantMap undoAllFileCheckpoints();
    Q_INVOKABLE QVariantMap fileCheckpointDetail(qint64 id) const;
    // UE-1: 用户已经看过本次会话的文件改动了（审阅窗口关闭时调用）。
    // 只把"未查看"计数清零，不动检查点本身；之后再有新改动会重新点亮。
    Q_INVOKABLE void acknowledgeFileReview();
    // R3 external-edit coordination: the settings page watches the rules
    // candidates of the project it is editing so an external editor's save
    // can reload the page (or warn, when local edits are pending) instead of
    // being silently clobbered by the next debounced write. Both candidates
    // (AGENTS.md and .nexus/rules.md) are watched; only existing files can be
    // added to a QFileSystemWatcher, but a successful writeProjectRules()
    // re-arms the watcher itself, so a file created by the page's own save
    // is covered from then on.
    // Appended at the class tail per the AOT-index convention above.
    Q_INVOKABLE void watchProjectRules(const QString& projectRoot);
    Q_INVOKABLE void unwatchProjectRules();
    // Test probe (same convention as probeSourcePathFor*): the armed watch
    // set. Async fileChanged delivery depends on OS notification latency
    // (Windows AV can hold events for tens of seconds) and is deliberately
    // not unit-asserted; the QML handler tolerates late/duplicate events.
    Q_INVOKABLE QStringList watchedRulesFiles() const;
    // The rules injection cap in bytes (AIAgent::kProjectRulesMaxBytes) for
    // the settings page's byte counter and over-limit tinting - the page no
    // longer hard-codes the number the injector truncates at. Defined
    // out-of-line: AIAgent is only forward-declared here.
    Q_INVOKABLE int projectRulesMaxBytes() const;

signals:
    void stateChanged();
    void traceChanged();
    void showProcessChanged();
    void responseReady();
    void draftTextChanged();
    void draftAttachmentsChanged();
    void pendingConfirmationsChanged();
    void chatModelChanged();
    void modelSettingsRequested();
    void panelVisibleChanged();
    void panelWidthChanged();
    void sidebarExpandedChanged();
    void sidebarWidthChanged();
    void defaultSidebarWidthChanged();
    void defaultPanelWidthChanged();
    void defaultSidebarExpandedChanged();
    void defaultPanelVisibleChanged();
    void restoreWindowOnStartupChanged();
    void tokenUsageChanged();
    // docs/16 §8: per-request cost changed (cents).
    void costChanged(qint64 costCents);
    // 会话累计 token 变化（新请求记账 / 换会话恢复 / 新会话清零）。
    void sessionTokensChanged();
    void sessionListChanged();
    // docs/17 A0: 项目列表 / 活动项目变化（侧栏两区数据源）。
    void projectsChanged();
    void activeProjectIdChanged();
    // R3: the watched project's rules file changed on disk (external editor
    // save, or the page's own debounced write - the page distinguishes the
    // two by comparing disk content against the editor text).
    void projectRulesFileChanged(const QString& projectRoot);
    // G2: project file cache ready / scanning state changed.
    void projectFilesChanged();
    void currentSessionIdChanged();
    // Active tab's session permission mode changed (switch or tab switch).
    void permissionModeChanged();
    // 活动标签套用的提示词预设变了（套用/清除/切标签/预设文本被编辑）。
    void promptPresetChanged();
    void searchResultsChanged();
    void knowledgeChanged();
    // docs/20 §5.1 (CITE-801): 引用徽标要求打开某文档的详情。
    // docs/25: chunk >= 0 时 KnowledgeDocDialog 定位到该分块
    void knowledgeDocumentOpenRequested(const QString& name, int chunk);
    void knowledgeBusyChanged();
    void knowledgeEmbedProgressChanged();
    void urlImportRunningChanged();
    // docs/25: 批量导入进度（running/done/total/current/failedCount 共用）
    void batchImportProgressChanged();

    void toolConfirmRequired(const QString& id, const QString& name, const QVariantMap& args, const QString& preview);
    void toolRegistryModelChanged();
    void todoListChanged();
    void fileCheckpointsChanged();
    // UE-1: 未查看改动的文件数变了（新增改动，或被确认清零）。
    void unreviewedFileCountChanged();
    void episodicChanged();
    void errorOccurred(const QString& error);
    // Non-error user feedback (import finished, etc.)
    void noticeOccurred(const QString& notice);
    // docs/24 R4: the live database file was swapped for a snapshot — the
    // wiring layer re-runs the knowledge-base restore against it.
    void databaseRestored();
    // docs/31 S4.5 N5: 数据库统计就绪（工作线程收集完毕回 GUI 线程）。
    // 结构见 SqliteMemory::collectStats；打开失败时带 error 字段。
    void dataStatsReady(const QVariantMap& stats);
    // docs/20 §3.4（A4）：窗口级拖放解析完成，携带附件描述符列表
    // （图片 {kind:"image", dataUrl} / 文本 {kind:"file", name, content}）。
    void attachmentsDropped(const QVariantList& attachments);
    // Emitted after a successful knowledge-base mutation, for persistence
    void documentRemoved(const QString& name);
    void knowledgeCleared();
    void tabsChanged();
    void activeTabChanged();
    // docs/20 §4.1（D10）：某个 tab 的任务从 Busy 变为非 Busy 时发射（携带
    // 会话标题与结果状态）。**任意 tab** 都发（不是仅活动 tab）——切走窗口
    // 后后台长任务完成时，托盘通知靠它。UI 不消费；main.cpp 联动托盘。
    void taskFinished(const QString& sessionTitle, bool success);
    // docs/31 S4.1 N9：任意 tab 的用量达到配额的提醒阈值（未超限）。
    // 已在门面层按 (日期, quotaType) 去重——多标签不会重复弹同一条通知。
    // UI 不消费；main.cpp 联动托盘气泡（notice.enabled 门控）。
    void quotaAlert(const QString& quotaType, qint64 used, qint64 quota);

private:
    struct Confirmation {
        QPointer<ChatSessionController> controller;
        QString id;
        QString name;
        QVariantMap arguments;
        QString preview;
    };
    QList<Confirmation> m_confirmations;
    void pruneConfirmations();
    // UE-1: 按当前检查点列表与已确认水位重算"未查看"文件数。
    // listChanged=true 时表示检查点列表本身刚被替换（可能是切换会话），
    // 此时若活动会话变了就把水位归零，避免把上一个会话的已读状态带过来。
    void refreshUnreviewedFileCount(bool listChanged);
    void configureModel(ChatSessionController* controller);
    QVariantList m_availableProfiles;
    QVariantMap m_defaultProfile;
    std::function<void(AIAgent*, const QVariantMap&)> m_configureModel;
    ChatSessionController* active() const;
    // 低层：新建一个标签并挂接 agent（不决定归属）。
    ChatSessionController* ensureTab();
    // docs/18: 新建标签并**显式**指定归属（projectId 空 = 本地），随后激活。
    // 归属不再从任何全局状态继承。
    ChatSessionController* addTab(const QString& projectId);
    // docs/24 R3: stop every tab and clear the list WITHOUT emitting —
    // active() is unguarded (m_tabs.at), so the caller MUST leave the
    // method and reach openTab() within the same call stack (the database
    // restore orchestration does exactly that; deferred deletes are flushed
    // by the caller via sendPostedEvents before touching shared state).
    void closeAllTabs();
    // First controller with an attached agent (active tab preferred) — used
    // for global session-list operations (delete/rename/audit/history pull).
    ChatSessionController* controllerWithAgent() const;
    void refreshSessions();
    // docs/17 A0: 从记忆后端重拉项目列表并 NOTIFY（去重刷新）。
    void refreshProjects();
    // docs/18: 把指定项目（id+name+root）推给一个 agent；id 为空 → 推空
    // （本地）。m_projects 未加载或查不到时 name/root 为空——此时仅标记
    // 归属，不注入提示上下文、不锚定相对路径。
    void applyProjectTo(AIAgent* agent, const QString& projectId) const;
    // docs/18 G5：归属与"活动标签是否已有会话"绑定——尚无会话时归属未定，
    // 可被设置；已有会话后归属落定且只读。导入这类**非归属动作**只在未定
    // 阶段写入，避免把已承载会话的标签改写掉（表现为顶栏徽标切换、且沙箱与
    // 系统提示指向的目录与该标签正在显示的会话脱钩）。
    void assignProjectIfActiveTabUnbound(const QString& projectId);
    // docs/18: 把全局沙箱基准同步为"活动标签的镜像"。工具执行实际用的是
    // ExecutionScope 的 per-agent 快照（docs/17 P1-5），此处的全局写入只为
    // 无快照的读取路径兜底。
    void syncSandboxMirror();
    void refreshToolRegistry();
    void refreshKnowledge();
    void setKnowledgeBusy(bool busy);

    // Runs ingestDocument on a worker thread; the outcome (busy flag, signals)
    // is handled back on the GUI thread via a QFutureWatcher.
    void startIngestion(const QString& name, const QString& content, bool reembed,
                        const QStringList& pageTexts = QStringList(),
                        const QString& sourcePath = QString());
    // docs/25: 批量导入的串行推进——GUI 线程取队首文件、派发池任务，
    // 任务完成回调里写结果并链到下一个。取消即不再派发后续文件。
    void importNextBatchFile();
    void startBatchQueue(const QStringList& paths);
    void finishBatchImport();
    // docs/20 §5.2 (C2): 与 importDocument 的文本路径共用「提取后」步骤。
    // PDF 的后台提取（startPdfImport）完成后也汇到这里，再按内容去重、
    // 取唯一显示名并交给 startIngestion。
    void importExtractedText(const QFileInfo& info, const QString& content,
                             const QStringList& pageTexts = QStringList(),
                             // Overrides the recorded source. File imports
                             // leave it empty (the info IS the source); the
                             // URL path passes the address, because the
                             // QFileInfo it hands over carries only a display
                             // name whose absoluteFilePath() would resolve to
                             // the CWD + name (a fabricated local path).
                             const QString& sourcePathOverride = QString());
    // docs/20 §5.2 (C2): PDF 文本提取的后台化（QPdfDocument 解析不在
    // GUI 线程跑，避免大文件冻结界面）。
    void startPdfImport(const QString& path, const QFileInfo& info);
    void persistPanelWidths();
    void refreshProjectFiles(const QString& root);

    void loadLayoutDefaults();
    void subscribeLayoutChanges();

    // ── Tabs (Batch 29) ───────────────────────────────
    QVector<ChatSessionController*> m_tabs;
    int m_activeTab{0};
    std::function<AIAgent*()> m_agentFactory;
    AIAgent* m_templateAgent{nullptr};
    // docs/20 §4.1（D10）：每个 tab 上一轮的 busy 状态，用于识别 Busy→非 Busy
    // 边沿（后台任务完成通知）。键随 controller 生命周期增长，但 controller
    // 数量与 tab 数一致（关 tab 即销毁），不会无限增长。
    QHash<const ChatSessionController*, bool> m_prevBusy;
    // docs/16 §8: last per-request cost for the TopBar display.
    qint64 m_lastCostCents{0};
    // docs/31 S4.1 N9: 接近配额上限提醒的跨 tab 去重（同一天同一类型一次）。
    QDate m_quotaAlertDate;
    QSet<QString> m_quotaAlertsToday;
    // 全局默认权限模式（设置键 agent/defaultPermissionMode），仅用于新 tab。
    int m_defaultPermissionMode{0};

    // ── 单对话视图（docs/30）──────────────────────────
    // 后台跑完、用户还没回来看的会话 id（侧栏"完成未读"点）。激活即清。
    QSet<QString> m_unreadSessions;
    // 打开集合的 LRU（最近激活/打开在后）。超上限时只淘汰**队首的、空闲且
    // 无草稿**的 controller——busy 与有草稿的永不淘汰（前者会中断任务，
    // 后者会丢用户输入）。容量与 kMaxOpenConversations 一致，不会无限增长。
    QVector<QString> m_sessionLru;
    // 承载某条会话的 controller；没有则 nullptr（会话只在数据库里）。
    ChatSessionController* controllerForSession(const QString& sessionId) const;
    // 记录/推进会话的 LRU 位置。
    void touchSessionLru(const QString& sessionId);
    // 打开集合超过上限时淘汰最久未用的空闲会话；无可淘汰对象时返回 false
    // （调用方负责提示用户，不静默失败）。
    bool evictIdleConversation();
    // 该 controller 是否还该留在打开集合里（busy 或存着草稿就不能动）。
    static bool conversationPinned(ChatSessionController* controller);

    // ── Global services ───────────────────────────────
    RagService* m_rag{nullptr};
    IMemory* m_memory{nullptr};   // Batch 17/18: search + export backend (no ownership)
    QVariantList m_searchResults;
    std::function<QString(const QString&)> m_contentProvider;
    QVariantList m_sessionList;
    QVariantList m_projects;         // docs/17 A0: 已导入项目
    // R3: watches the edited project's rules candidates for external saves.
    QFileSystemWatcher* m_rulesWatcher{nullptr};
    QString m_watchedRulesRoot;      // native-separators root being watched
    // docs/18: 归属按标签持有，故这里没有全局活动项目成员。
    // G2: active project's indexable file cache (background scan).
    QStringList m_projectFiles;
    QString m_projectFilesRoot;
    qint64 m_projectFilesScannedAt{0};
    quint64 m_projectFilesGeneration{0};
    bool m_projectFilesScanning{false};

    // 过程节点是否展开（见 showProcess 属性的注释）。
    bool m_showProcess{true};

    // ── Panel ──────────────────────────────────────────
    bool m_isPanelVisible{};
    int m_panelWidth{};

    // ── Sidebar ────────────────────────────────────────
    bool m_isSidebarExpanded{};
    int m_sidebarWidth{};
    // Debounces QSettings writes while the user drags a splitter
    QTimer m_uiPersistTimer;

    // ── Layout defaults ────────────────────────────────
    int m_defaultSidebarWidth{};
    int m_defaultPanelWidth{};
    bool m_defaultSidebarExpanded{};
    bool m_defaultPanelVisible{};
    bool m_restoreWindowOnStartup{};

    // ── Tool Registry ──────────────────────────────────
    QVariantList m_toolRegistryModel;
    QVariantList m_todoList;
    QVariantList m_fileCheckpoints;
    // UE-1: 用户已确认看过的检查点 id 上限（按活动会话）。
    // 用 id 水位而不是"清空列表"来表达已读：id 单调递增，任何新写入的
    // 检查点 id 都更大，于是"又有新改动"天然重新点亮，不需要额外挂钩。
    // 仅内存态：进程重启后未读提示重新出现是合理行为（用户确实没看过
    // 上一次运行的改动）。
    qint64 m_reviewAckCheckpointId{0};
    // 派生值缓存：fileCheckpoints / ack 变化时重算，只在真的变了才发信号。
    int m_unreviewedFileCount{0};

    // ── Knowledge Base ────────────────────────────────
    QVariantList m_knowledgeDocuments;
    bool m_knowledgeBusy{false};
    bool m_urlImportRunning{false};
    bool m_allowPrivateFetchTargets{false};
    std::atomic<bool> m_urlFetchCancel{false};
    // docs/25: 批量导入状态（全部 GUI 线程独占；worker 只返回结果值）
    QStringList m_batchQueue;             // 尚未开始的文件
    QVariantList m_batchResults;          // {path, name, status, message}
    int m_batchTotal{0};
    int m_batchFailedCount{0};
    bool m_batchRunning{false};
    bool m_batchCancelRequested{false};
    QString m_batchCurrent;
    int m_embedDone{0};
    int m_embedTotal{0};
    LogModel* m_logModel;

    static AgentViewModel* s_instance;

    // Batch 30 (F6): upper bound for one text attachment (kept modest so a
    // mis-picked binary does not blow up the context budget).
    static constexpr int MaxTextAttachmentBytes = 512 * 1024;
};

} // namespace NexusAI
