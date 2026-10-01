#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QTimer>
#include <QVector>
#include <functional>

#include "core/agent/AgentState.h"
#include "core/agent/PermissionMode.h"
#include "ChatSessionController.h"
#include "TraceModel.h"
#include "LogModel.h"

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
// When a real AIAgent is attached via setAgent(), all actions are delegated
// to it (real LLM pipeline). Otherwise the built-in simulation runs, which
// allows UI demonstration without a configured backend.
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

    // ── Response ──────────────────────────────────────
    Q_PROPERTY(QString finalResponse READ finalResponse NOTIFY responseReady FINAL)

    // ── Input ─────────────────────────────────────────
    Q_PROPERTY(QString draftText READ draftText WRITE setDraftText NOTIFY draftTextChanged FINAL)

    // ── Session permission mode (active tab) ──────────
    // 0=变更确认 1=自动编辑 2=完全访问；常量属性供 QML 选择器建立模型。
    Q_PROPERTY(int permissionMode READ permissionMode WRITE setPermissionMode NOTIFY permissionModeChanged FINAL)
    Q_PROPERTY(int permissionModeConfirmChanges READ permissionModeConfirmChanges CONSTANT FINAL)
    Q_PROPERTY(int permissionModeAutoEdit READ permissionModeAutoEdit CONSTANT FINAL)
    Q_PROPERTY(int permissionModeFullAccess READ permissionModeFullAccess CONSTANT FINAL)

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
    // Batch 17 (FEAT-1702): cross-session message search results; each entry
    // is {sessionId, sessionTitle, messageId, snippet, role, timestamp}.
    Q_PROPERTY(QVariantList searchResults READ searchResults NOTIFY searchResultsChanged FINAL)

    // ── Layout defaults (General Settings) ─────────────
    Q_PROPERTY(int defaultSidebarWidth READ defaultSidebarWidth WRITE setDefaultSidebarWidth NOTIFY defaultSidebarWidthChanged FINAL)
    Q_PROPERTY(int defaultPanelWidth READ defaultPanelWidth WRITE setDefaultPanelWidth NOTIFY defaultPanelWidthChanged FINAL)
    Q_PROPERTY(bool defaultSidebarExpanded READ defaultSidebarExpanded WRITE setDefaultSidebarExpanded NOTIFY defaultSidebarExpandedChanged FINAL)
    Q_PROPERTY(bool defaultPanelVisible READ defaultPanelVisible WRITE setDefaultPanelVisible NOTIFY defaultPanelVisibleChanged FINAL)
    Q_PROPERTY(bool restoreWindowOnStartup READ restoreWindowOnStartup WRITE setRestoreWindowOnStartup NOTIFY restoreWindowOnStartupChanged FINAL)

    // ── Tool Registry ─────────────────────────────────
    Q_PROPERTY(QVariantList toolRegistryModel READ toolRegistryModel NOTIFY toolRegistryModelChanged FINAL)

    // ── Knowledge Base ────────────────────────────────
    Q_PROPERTY(QVariantList knowledgeDocuments READ knowledgeDocuments NOTIFY knowledgeChanged FINAL)
    Q_PROPERTY(bool knowledgeBusy READ knowledgeBusy NOTIFY knowledgeBusyChanged FINAL)
    Q_PROPERTY(int knowledgeEmbedDone READ knowledgeEmbedDone NOTIFY knowledgeEmbedProgressChanged)
    Q_PROPERTY(int knowledgeEmbedTotal READ knowledgeEmbedTotal NOTIFY knowledgeEmbedProgressChanged)

    // ── Pipeline mode ─────────────────────────────────
    Q_PROPERTY(bool liveMode READ liveMode NOTIFY liveModeChanged FINAL)

    // ── In-app log panel (日志开发 doc 6) ──────────────
    Q_PROPERTY(LogModel* logModel READ logModel CONSTANT)

    // ── Session tabs (Batch 29, F7) ───────────────────
    // [{index, sessionId, title, busy, active}] for the tab bar; title falls
    // back to "新会话" until the tab has a session.
    Q_PROPERTY(QVariantList tabModel READ tabModel NOTIFY tabsChanged FINAL)
    Q_PROPERTY(int activeTabIndex READ activeTabIndex NOTIFY activeTabChanged FINAL)

public:
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

    // FEAT-803: batch-embedding progress (wired from OllamaEmbedding)
    int knowledgeEmbedDone() const { return m_embedDone; }
    int knowledgeEmbedTotal() const { return m_embedTotal; }
    void setEmbedProgress(int done, int total);
    static AgentViewModel* create(QQmlEngine*, QJSEngine*);

    // Attach the shared real-pipeline agent (tab 0 + template). Takes no
    // ownership (agent outlives the VM). nullptr = simulation for every tab.
    void setAgent(AIAgent* agent);
    // Batch 29: factory building a fresh AIAgent (+ own SQLite connection)
    // for every additional tab. When unset, additional tabs run simulation.
    void setAgentFactory(std::function<AIAgent*()> factory);
    // Batch 29: apply to every attached agent — provider switches call this
    // with setLLMService, since the service instances are shared across tabs.
    void applyToAgents(const std::function<void(AIAgent*)>& fn);
    // Batch 17: attach the persistence backend for search/export. Takes no
    // ownership; nullptr disables those features (simulation mode).
    void setMemory(IMemory* memory);
    // Attach the knowledge-base service (optional). Takes no ownership.
    void setRagService(RagService* rag);
    // Provide persisted knowledge document content (used by re-embedding).
    // Called on the GUI thread; return empty when the document is unknown.
    void setKnowledgeContentProvider(std::function<QString(const QString&)> provider);
    bool liveMode() const { return m_templateAgent != nullptr; }

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

    // Response (active tab)
    QString finalResponse() const { return active()->finalResponse(); }

    // Draft text (active tab)
    QString draftText() const { return active()->draftText(); }
    void setDraftText(const QString& text);

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
    QVariantList sessionList() const { return m_sessionList; }

    // docs/17 A0: 项目级会话。projects 为已导入项目列表
    // [{id, name, rootPath}]；activeProjectId 为空 = 本地对话视图。
    QVariantList projects() const { return m_projects; }
    // docs/18: 派生自**活动标签**的 agent —— 不存在全局活动项目。
    QString activeProjectId() const;
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
    // Batch 17: message-search results (set via the searchMessages invokable)
    QVariantList searchResults() const { return m_searchResults; }

    // Token management (active tab)
    double tokenUsageRate() const;
    int usedTokens() const { return active()->usedTokens(); }
    int maxTokens() const { return active()->maxTokens(); }
    // docs/16 §8: last per-request cost (cents), updated via costChanged.
    qint64 lastCostCents() const { return m_lastCostCents; }

    // Tool registry
    QVariantList toolRegistryModel() const { return m_toolRegistryModel; }

    // Knowledge base
    QVariantList knowledgeDocuments() const { return m_knowledgeDocuments; }
    bool knowledgeBusy() const { return m_knowledgeBusy; }

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
    // Closing a running tab stops its task (the agent seals unanswered tool
    // calls); the last remaining tab cannot be closed.
    Q_INVOKABLE void closeTab(int index);
    Q_INVOKABLE void activateTab(int index);

    // ── QML-invokable actions ─────────────────────────
    // Batch 30 (F6): images are compressed data URLs; files are
    // {kind:"file", name, content} text attachments folded into the request.
    Q_INVOKABLE void startTask(const QString& input, const QStringList& images = {},
                               const QVariantList& files = {});
    // Compressed data URL of the clipboard image (empty when none).
    Q_INVOKABLE QString clipboardImageDataUrl() const;
    // Compressed data URL of an image file picked in QML (empty when unreadable).
    Q_INVOKABLE QString loadImageDataUrl(const QString& path) const;
    // {kind:"file", name, content} for a text attachment; empty map on
    // rejection (too large / binary).
    Q_INVOKABLE QVariantMap loadTextFileAttachment(const QString& path);
    Q_INVOKABLE void approveToolCall(const QString& id, bool approved);
    Q_INVOKABLE void setToolEnabled(const QString& name, bool enabled);
    Q_INVOKABLE void stop();                 // abort generation, keep trace
    Q_INVOKABLE void newChat();              // reset to a fresh session
    // Loads into the active tab; when the active tab is busy a new tab is
    // opened for the session instead — switching never interrupts a task.
    Q_INVOKABLE void loadSession(const QString& sessionId);
    Q_INVOKABLE void deleteSession(const QString& sessionId);
    Q_INVOKABLE void renameSession(const QString& sessionId, const QString& title);
    // Batch 17: refresh searchResults from the memory backend (empty keyword
    // → empty results, which the sidebar treats as "back to session list").
    Q_INVOKABLE void searchMessages(const QString& keyword);
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
    // M1 (stats): token/cost aggregation for the usage page.
    Q_INVOKABLE QVariantMap tokenStats(int recentDays = 30);
    // docs/16 §8: detailed aggregates for the extended usage page.
    Q_INVOKABLE QVariantMap tokenStatsDetailed(int recentDays = 30);
    Q_INVOKABLE QVariantList tokenStatsBySource(int recentDays = 30);
    Q_INVOKABLE QVariantMap costStats(int recentDays = 30);
    Q_INVOKABLE QVariantMap quotaStatus();
    // docs/16 §8.3: 当前激活会话的 token 来源拆分（TopBar 悬停浮层）。
    // 无激活会话 id 时返回空 map，QML 据此显示空态。
    Q_INVOKABLE QVariantMap sessionTokenBreakdown() const;
    // docs/16 §8.3: 激活 tab 的实时上下文构成（容量占比浮层数据源）。
    Q_INVOKABLE QVariantMap contextComposition() const;
    Q_INVOKABLE QString logFilePath() const;   // runtime log file path (for the audit dialog)

signals:
    void stateChanged();
    void traceChanged();
    void responseReady();
    void draftTextChanged();
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
    void sessionListChanged();
    // docs/17 A0: 项目列表 / 活动项目变化（侧栏两区数据源）。
    void projectsChanged();
    void activeProjectIdChanged();
    void currentSessionIdChanged();
    // Active tab's session permission mode changed (switch or tab switch).
    void permissionModeChanged();
    void searchResultsChanged();
    void knowledgeChanged();
    void knowledgeBusyChanged();
    void knowledgeEmbedProgressChanged();
    void liveModeChanged();
    void toolConfirmRequired(const QString& id, const QString& name, const QVariantMap& args);
    void toolRegistryModelChanged();
    void errorOccurred(const QString& error);
    // Non-error user feedback (import finished, etc.)
    void noticeOccurred(const QString& notice);
    // Emitted after a successful knowledge-base mutation, for persistence
    void documentRemoved(const QString& name);
    void knowledgeCleared();
    void tabsChanged();
    void activeTabChanged();

private:
    ChatSessionController* active() const;
    // 低层：新建一个标签并挂接 agent（不决定归属）。
    ChatSessionController* ensureTab();
    // docs/18: 新建标签并**显式**指定归属（projectId 空 = 本地），随后激活。
    // 归属不再从任何全局状态继承。
    ChatSessionController* addTab(const QString& projectId);
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
    void startIngestion(const QString& name, const QString& content, bool reembed);
    void persistPanelWidths();

    void registerLayoutPreferences();
    void loadLayoutDefaults();
    void subscribeLayoutChanges();

    // ── Tabs (Batch 29) ───────────────────────────────
    QVector<ChatSessionController*> m_tabs;
    int m_activeTab{0};
    std::function<AIAgent*()> m_agentFactory;
    AIAgent* m_templateAgent{nullptr};
    // docs/16 §8: last per-request cost for the TopBar display.
    qint64 m_lastCostCents{0};
    // 全局默认权限模式（设置键 agent/defaultPermissionMode），仅用于新 tab。
    int m_defaultPermissionMode{0};

    // ── Global services ───────────────────────────────
    RagService* m_rag{nullptr};
    IMemory* m_memory{nullptr};   // Batch 17/18: search + export backend (no ownership)
    QVariantList m_searchResults;
    std::function<QString(const QString&)> m_contentProvider;
    QVariantList m_sessionList;
    QVariantList m_projects;         // docs/17 A0: 已导入项目
    // docs/18: 归属按标签持有，故这里没有全局活动项目成员。

    // ── Panel ──────────────────────────────────────────
    bool m_isPanelVisible{true};
    int m_panelWidth{300};

    // ── Sidebar ────────────────────────────────────────
    bool m_isSidebarExpanded{true};
    int m_sidebarWidth{260};
    // Debounces QSettings writes while the user drags a splitter
    QTimer m_uiPersistTimer;

    // ── Layout defaults ────────────────────────────────
    int m_defaultSidebarWidth{260};
    int m_defaultPanelWidth{300};
    bool m_defaultSidebarExpanded{true};
    bool m_defaultPanelVisible{true};
    bool m_restoreWindowOnStartup{true};

    // ── Tool Registry ──────────────────────────────────
    QVariantList m_toolRegistryModel;

    // ── Knowledge Base ────────────────────────────────
    QVariantList m_knowledgeDocuments;
    bool m_knowledgeBusy{false};
    int m_embedDone{0};
    int m_embedTotal{0};
    LogModel* m_logModel;

    static AgentViewModel* s_instance;

    // Batch 30 (F6): upper bound for one text attachment (kept modest so a
    // mis-picked binary does not blow up the context budget).
    static constexpr int MaxTextAttachmentBytes = 512 * 1024;
};

} // namespace NexusAI
