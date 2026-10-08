#include "viewmodels/AgentViewModel.h"

#include "viewmodels/PreferenceRegistry.h"
#include "viewmodels/PreferenceStore.h"
#include "viewmodels/ChangeBus.h"
#include "core/agent/AIAgent.h"
#include "core/memory/IMemory.h"
#include "core/rag/RagService.h"
#include "core/rag/KnowledgeChunk.h"
#include "core/tools/ToolRegistry.h"
#include "core/tools/Sandbox.h"
#include "core/utils/Logger.h"
#include "core/utils/DataUrlUtils.h"
#include "core/utils/AttachmentClassifier.h"

#include <QFileInfo>
#include <QSettings>
#include <QUrl>

#include <QBuffer>
#include <QClipboard>
#include <QDebug>
#include <QImage>
#include "core/utils/Activity.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFutureWatcher>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

namespace NexusAI {

AgentViewModel* AgentViewModel::s_instance = nullptr;

AgentViewModel* AgentViewModel::instance() {
    if (!s_instance) s_instance = new AgentViewModel(nullptr);
    return s_instance;
}

AgentViewModel* AgentViewModel::create(QQmlEngine*, QJSEngine*) {
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}

AgentViewModel::AgentViewModel(QObject* parent)
    : QObject(parent)
    , m_logModel(new LogModel(this)) {
    refreshToolRegistry();
    connect(&ToolRegistry::instance(), &ToolRegistry::toolsChanged,
            this, &AgentViewModel::refreshToolRegistry);

    // Layout defaults (General Settings) must load first: they seed the
    // fallbacks of the panel restore below.
    registerLayoutPreferences();
    loadLayoutDefaults();
    subscribeLayoutChanges();

    // Restore last UI layout (sidebar / context panel visibility + widths),
    // falling back to the configured defaults when nothing was persisted.
    const QSettings uiSettings("NexusAI", "NexusAI");
    m_isPanelVisible = uiSettings.value("ui/panelVisible", m_defaultPanelVisible).toBool();
    m_isSidebarExpanded = uiSettings.value("ui/sidebarExpanded", m_defaultSidebarExpanded).toBool();
    m_panelWidth = uiSettings.value("ui/panelWidth", m_defaultPanelWidth).toInt();
    m_sidebarWidth = uiSettings.value("ui/sidebarWidth", m_defaultSidebarWidth).toInt();

    // Splitter drags update the width per mouse move; coalesce the writes
    m_uiPersistTimer.setSingleShot(true);
    m_uiPersistTimer.setInterval(400);
    connect(&m_uiPersistTimer, &QTimer::timeout, this,
            &AgentViewModel::persistPanelWidths);

    // The UI always has at least one session tab (Batch 29).
    ensureTab();
}

AgentViewModel::~AgentViewModel() {
    if (m_uiPersistTimer.isActive()) {
        m_uiPersistTimer.stop();
        persistPanelWidths();
    }
}

void AgentViewModel::persistPanelWidths() {
    QSettings("NexusAI", "NexusAI").setValue("ui/sidebarWidth", m_sidebarWidth);
    QSettings("NexusAI", "NexusAI").setValue("ui/panelWidth", m_panelWidth);
}

void AgentViewModel::registerLayoutPreferences() {
    auto* reg = PreferenceRegistry::instance();
    if (!reg) return;
    if (reg->hasPreference(QStringLiteral("ui.defaultSidebarWidth"))) return;

    auto registerInt = [&](const QString& key, const QString& display,
                           int def, int min, int max) {
        PreferenceMeta meta;
        meta.key = key;
        meta.categoryId = QStringLiteral("interface");
        meta.displayName = display;
        meta.defaultValue = def;
        meta.type = PreferenceType::Int;
        meta.constraint.min = min;
        meta.constraint.max = max;
        meta.constraint.hasRange = true;
        reg->registerPreference(meta);
    };

    auto registerBool = [&](const QString& key, const QString& display, bool def) {
        PreferenceMeta meta;
        meta.key = key;
        meta.categoryId = QStringLiteral("interface");
        meta.displayName = display;
        meta.defaultValue = def;
        meta.type = PreferenceType::Bool;
        reg->registerPreference(meta);
    };

    registerInt(QStringLiteral("ui.defaultSidebarWidth"), QT_TRANSLATE_NOOP("NexusAI", "侧边栏默认宽度"), 260, 180, 560);
    registerInt(QStringLiteral("ui.defaultPanelWidth"), QT_TRANSLATE_NOOP("NexusAI", "面板默认宽度"), 300, 220, 640);
    registerBool(QStringLiteral("ui.defaultSidebarExpanded"), QT_TRANSLATE_NOOP("NexusAI", "侧边栏默认展开"), true);
    registerBool(QStringLiteral("ui.defaultPanelVisible"), QT_TRANSLATE_NOOP("NexusAI", "面板默认展开"), true);
    registerBool(QStringLiteral("ui.restoreWindowOnStartup"), QT_TRANSLATE_NOOP("NexusAI", "启动时恢复窗口"), true);
}

void AgentViewModel::loadLayoutDefaults() {
    auto* store = PreferenceStore::instance();
    if (!store) return;

    const QVariant sw = store->load(QStringLiteral("ui.defaultSidebarWidth"));
    if (sw.isValid()) m_defaultSidebarWidth = sw.toInt();
    const QVariant pw = store->load(QStringLiteral("ui.defaultPanelWidth"));
    if (pw.isValid()) m_defaultPanelWidth = pw.toInt();
    const QVariant se = store->load(QStringLiteral("ui.defaultSidebarExpanded"));
    if (se.isValid()) m_defaultSidebarExpanded = se.toBool();
    const QVariant pv = store->load(QStringLiteral("ui.defaultPanelVisible"));
    if (pv.isValid()) m_defaultPanelVisible = pv.toBool();
    const QVariant rw = store->load(QStringLiteral("ui.restoreWindowOnStartup"));
    if (rw.isValid()) m_restoreWindowOnStartup = rw.toBool();
}

void AgentViewModel::subscribeLayoutChanges() {
    auto* bus = ChangeBus::instance();
    if (!bus) return;
    connect(bus, &ChangeBus::changed, this, [this](const QString& key, const QVariant& value) {
        if (key == QStringLiteral("ui.defaultSidebarWidth")) {
            m_defaultSidebarWidth = value.toInt();
            emit defaultSidebarWidthChanged();
        } else if (key == QStringLiteral("ui.defaultPanelWidth")) {
            m_defaultPanelWidth = value.toInt();
            emit defaultPanelWidthChanged();
        } else if (key == QStringLiteral("ui.defaultSidebarExpanded")) {
            m_defaultSidebarExpanded = value.toBool();
            emit defaultSidebarExpandedChanged();
        } else if (key == QStringLiteral("ui.defaultPanelVisible")) {
            m_defaultPanelVisible = value.toBool();
            emit defaultPanelVisibleChanged();
        } else if (key == QStringLiteral("ui.restoreWindowOnStartup")) {
            m_restoreWindowOnStartup = value.toBool();
            emit restoreWindowOnStartupChanged();
        }
    });
}

// ── Session tabs (Batch 29) ──────────────────────────────

ChatSessionController* AgentViewModel::active() const {
    return m_tabs.at(m_activeTab);
}

ChatSessionController* AgentViewModel::ensureTab() {
    auto* controller = new ChatSessionController(this);

    // Active-tab forwarding: the facade signal is re-emitted only when the
    // change happened in the tab the UI is looking at.
    connect(controller, &ChatSessionController::stateChanged, this, [this, controller] {
        // docs/20 §4.1（D10）：Busy → 非 Busy 边沿 = 一次任务结束。任意 tab
        // 都发（后台任务完成也要通知），标题走 tabModel 的同一解析逻辑。
        const bool busy = controller->isBusy();
        if (m_prevBusy.contains(controller) && m_prevBusy.value(controller) && !busy) {
            QString title;
            for (const auto& entry : m_sessionList) {
                const QVariantMap session = entry.toMap();
                if (session.value("id").toString() == controller->currentSessionId()) {
                    title = session.value("title").toString();
                    break;
                }
            }
            if (title.isEmpty()) title = tr("新会话");
            emit taskFinished(title,
                controller->state() == AgentState::Completed);
        }
        m_prevBusy[controller] = busy;
        if (controller == active()) emit stateChanged();
        emit tabsChanged();   // tab busy indicator
    });
    connect(controller, &ChatSessionController::traceChanged, this, [this, controller] {
        if (controller == active()) emit traceChanged();
    });
    connect(controller, &ChatSessionController::responseReady, this, [this, controller] {
        if (controller == active()) emit responseReady();
    });
    connect(controller, &ChatSessionController::draftTextChanged, this, [this, controller] {
        if (controller == active()) emit draftTextChanged();
    });
    connect(controller, &ChatSessionController::tokenUsageChanged, this, [this, controller] {
        if (controller == active()) emit tokenUsageChanged();
    });
    connect(controller, &ChatSessionController::sessionTokensChanged, this, [this, controller] {
        if (controller == active()) emit sessionTokensChanged();
    });
    // docs/16 §8: forward per-request cost to the TopBar live display.
    connect(controller, &ChatSessionController::costChanged, this, [this, controller](qint64 costCents) {
        if (controller == active()) {
            m_lastCostCents = costCents;
            emit costChanged(costCents);
        }
    });
    connect(controller, &ChatSessionController::currentSessionIdChanged, this, [this, controller] {
        if (controller == active()) emit currentSessionIdChanged();
        emit tabsChanged();   // tab title follows the session
    });
    connect(controller, &ChatSessionController::permissionModeChanged, this, [this, controller] {
        if (controller == active()) emit permissionModeChanged();
        emit tabsChanged();   // background tabs show the mode badge
    });

    // Surfaced no matter which tab they happened in: errors and tool
    // confirmations are global UX (the confirm dialog routes the decision
    // back to the waiting tab through approveToolCall).
    connect(controller, &ChatSessionController::toolConfirmRequired, this,
            &AgentViewModel::toolConfirmRequired);
    connect(controller, &ChatSessionController::errorOccurred, this,
            &AgentViewModel::errorOccurred);
    // Session history is shared (every tab agent reads the same database).
    connect(controller, &ChatSessionController::sessionHistoryChanged, this,
            &AgentViewModel::refreshSessions);

    m_tabs.append(controller);

    // A fresh tab picks up its own agent when the pipeline is configured.
    if (m_agentFactory && m_templateAgent)
        controller->attachAgent(m_agentFactory(), true);

    // docs/18: 归属由 addTab 显式指定——ensureTab 不再从任何全局状态继承。

    // New tabs start at the configured global default permission mode
    // (no-op for ConfirmChanges; also covers the initial tab created
    // before main.cpp injected the setting).
    controller->applyDefaultPermissionMode(m_defaultPermissionMode);

    emit tabsChanged();
    return controller;
}

// docs/18: 新建标签并**显式**指定归属，随后激活。归属只作用于这个新标签——
// 其他标签的沙箱指向与项目上下文完全不受影响（旧模型会把全局值广播给所有
// 标签，这正是"点新会话把别人的项目清空"的根因）。
ChatSessionController* AgentViewModel::addTab(const QString& projectId) {
    ChatSessionController* controller = ensureTab();
    applyProjectTo(controller->agent(), projectId);
    activateTab(m_tabs.size() - 1);
    return controller;
}

// 低层入口：新建一个本地（无项目）标签。
void AgentViewModel::openTab() {
    addTab(QString());
}

// docs/17/18: 新建会话的默认归属 = 普通对话（本地）。项目会话只能由
// newProjectChat（侧栏项目行的「+」）创建。
void AgentViewModel::newLocalChat() {
    openTab();
}

// 在指定项目下新建会话（侧栏项目行的「+」）。空 id 退化为普通对话。
void AgentViewModel::newProjectChat(const QString& projectId) {
    addTab(projectId);
}

void AgentViewModel::activateTab(int index) {
    if (index < 0 || index >= m_tabs.size() || index == m_activeTab) return;
    // docs/18: activeProjectId 是派生值，切标签会改变它——先取旧值用于比对。
    const QString projectBefore = activeProjectId();
    m_activeTab = index;
    // Every forwarded conversation property changed with the tab.
    emit stateChanged();
    emit traceChanged();
    emit responseReady();
    emit draftTextChanged();
    emit tokenUsageChanged();
    emit sessionTokensChanged();
    emit costChanged(m_lastCostCents);
    emit currentSessionIdChanged();
    emit permissionModeChanged();
    emit activeTabChanged();
    emit tabsChanged();
    // 沙箱镜像随活动标签走；派生值确实变了才通知。
    syncSandboxMirror();
    if (projectBefore != activeProjectId())
        emit activeProjectIdChanged();
}

void AgentViewModel::closeTab(int index) {
    if (index < 0 || index >= m_tabs.size() || m_tabs.size() <= 1) return;
    ChatSessionController* controller = m_tabs.at(index);
    // Stop first: a running task gets aborted and its unanswered tool calls
    // sealed by the agent, so nothing keeps writing after the tab is gone.
    controller->stop();
    m_tabs.removeAt(index);
    controller->deleteLater();

    if (m_activeTab >= m_tabs.size())
        m_activeTab = m_tabs.size() - 1;
    else if (index < m_activeTab)
        --m_activeTab;

    emit stateChanged();
    emit traceChanged();
    emit responseReady();
    emit draftTextChanged();
    emit tokenUsageChanged();
    emit sessionTokensChanged();
    emit currentSessionIdChanged();
    emit activeTabChanged();
    emit tabsChanged();
    refreshSessions();
}

QVariantList AgentViewModel::tabModel() const {
    QVariantList model;
    for (int i = 0; i < m_tabs.size(); ++i) {
        const ChatSessionController* controller = m_tabs.at(i);
        const QString sessionId = controller->currentSessionId();
        QString title;
        for (const auto& entry : m_sessionList) {
            const QVariantMap session = entry.toMap();
            if (session.value("id").toString() == sessionId) {
                title = session.value("title").toString();
                break;
            }
        }
        if (title.isEmpty()) title = tr("新会话");

        QVariantMap tab;
        tab["index"] = i;
        tab["sessionId"] = sessionId;
        tab["title"] = title;
        tab["busy"] = controller->isBusy();
        tab["active"] = i == m_activeTab;
        tab["permissionMode"] = controller->permissionMode();
        model.append(tab);
    }
    return model;
}

// ── Real agent pipeline ──────────────────────────────────

void AgentViewModel::setAgent(AIAgent* agent) {
    if (m_templateAgent == agent) return;
    m_templateAgent = agent;

    // Tab 0 carries the shared template agent (unowned); other tabs receive
    // factory-built agents. Detaching mid-task terminates it cleanly.
    for (int i = 0; i < m_tabs.size(); ++i) {
        // docs/18: 挂接会换掉 agent（新 agent 无归属），先记下本标签的归属，
        // 挂接后原样还原——不能再拿"全局活动项目"覆盖，否则会把其他标签
        // 的项目一并冲掉。
        const QString tabProject = m_tabs[i]->activeProjectId();
        if (i == 0 && agent) {
            m_tabs[0]->attachAgent(agent, false);
        } else if (agent && m_agentFactory) {
            m_tabs[i]->attachAgent(m_agentFactory(), true);
        } else {
            m_tabs[i]->attachAgent(nullptr, false);
        }
        applyProjectTo(m_tabs[i]->agent(), tabProject);
    }

    emit tabsChanged();
    syncSandboxMirror();
    emit activeProjectIdChanged();
    refreshSessions();
}

void AgentViewModel::setAgentFactory(std::function<AIAgent*()> factory) {
    m_agentFactory = std::move(factory);
}

void AgentViewModel::applyToAgents(const std::function<void(AIAgent*)>& fn) {
    for (ChatSessionController* controller : m_tabs) {
        if (controller->agent()) fn(controller->agent());
    }
}

// Batch 17: attach the persistence backend (search + Batch 18 export).
// Takes no ownership; nullptr disables those features.
void AgentViewModel::setMemory(IMemory* memory) {
    if (m_memory == memory) return;
    m_memory = memory;
    // docs/17 A0: 记忆后端就绪后拉取项目列表（VM 早于 main.cpp 注入构建）。
    refreshProjects();
}

// ── docs/17 A0: 项目级会话 ─────────────────────────────

void AgentViewModel::refreshProjects() {
    const QVariantList projects = m_memory ? m_memory->listProjects()
                                          : QVariantList{};
    if (m_projects == projects) return;
    m_projects = projects;
    emit projectsChanged();
}

// docs/18: 派生值——活动标签的 agent 持有的归属。不存在全局活动项目，因此
// 这个值天然不可能与其他标签不一致（徽标"撒谎"从结构上被消除）。
QString AgentViewModel::activeProjectId() const {
    return active()->activeProjectId();
}

void AgentViewModel::setActiveProjectId(const QString& id) {
    // docs/18: 只作用于**活动标签**。旧实现把 id 广播给所有标签，是"点新会话
    // 把别人的项目清空"与"切标签徽标撒谎"的共同根因。
    ChatSessionController* controller = active();
    if (controller->activeProjectId() == id) return;
    // 活动标签尚未挂接 agent（例如启动早期）时无处可推，直接忽略。
    // 不暂存：空标签的默认归属就是本地，暂存会在挂接时把项目套上去，与
    // "新建默认普通对话"冲突（docs/18 期 3 移除了原 m_pendingProjectId）。
    if (!controller->agent()) return;
    applyProjectTo(controller->agent(), id);
    // 持久化（M4 直写先例）：重启后由 main.cpp 回灌。docs/18 之后它记录的
    // 语义是"上次浏览位置"，期 3 会改由侧栏写入。
    QSettings("NexusAI", "NexusAI").setValue("projects/activeId", id);
    syncSandboxMirror();
    emit activeProjectIdChanged();
}

// docs/18 G5：归属落定后只读。浏览、导入这类**非归属动作**不应改写已经承载
// 会话的标签的归属——否则标签归属与它正在显示的会话脱钩：顶栏徽标会"撒谎"，
// 系统提示与工具沙箱根也会指向另一个目录。判据与 TopBar 徽标的 editable 一致
// （活动标签尚无会话 = 归属未定，此时才允许写入）。
void AgentViewModel::assignProjectIfActiveTabUnbound(const QString& projectId) {
    if (!active()->currentSessionId().isEmpty()) return;
    setActiveProjectId(projectId);
}

// docs/18: 把指定项目（id+name+root）推给一个 agent。id 为空 → 推空（本地）。
// m_projects 里查不到（尚未加载或已被删）时 name/root 为空——此时仅标记归属，
// 不注入路径上下文，工具的相对路径锚定保持关闭。
void AgentViewModel::applyProjectTo(AIAgent* agent, const QString& projectId) const {
    if (!agent) return;
    QString projectName, projectRoot;
    if (!projectId.isEmpty()) {
        for (const QVariant& entry : std::as_const(m_projects)) {
            const QVariantMap p = entry.toMap();
            if (p.value("id").toString() == projectId) {
                projectName = p.value("name").toString();
                projectRoot = p.value("rootPath").toString();
                break;
            }
        }
    }
    agent->setActiveProject(projectId, projectName, projectRoot);
}

// docs/18: 全局沙箱基准 = 活动标签的镜像。工具执行实际读的是 ExecutionScope
// 的 per-agent 快照（docs/17 P1-5），这里只为无快照的读取路径兜底。
void AgentViewModel::syncSandboxMirror() {
    Sandbox::setProjectDir(active()->activeProjectRoot());
}

QString AgentViewModel::importProject(const QString& path) {
    // FolderDialog 传来的可能是 file:/// URL；归一为本地路径。
    QString local = path;
    if (local.startsWith(QLatin1String("file://")))
        local = QUrl(local).toLocalFile();
    const QFileInfo info(local);
    const QString canonical = info.canonicalFilePath();
    if (!info.isDir() || canonical.isEmpty()) {
        emit noticeOccurred(QStringLiteral("导入项目失败：目录不存在或不可访问"));
        return QString();
    }
    const QString root = QDir::toNativeSeparators(canonical);
    // 同一路径重复导入：复用已有记录而不是建第二条。
    for (const QVariant& entry : std::as_const(m_projects)) {
        const QVariantMap p = entry.toMap();
        if (p.value("rootPath").toString() == root) {
            assignProjectIfActiveTabUnbound(p.value("id").toString());
            return p.value("id").toString();
        }
    }
    const QString id = m_memory
        ? m_memory->createProject(info.fileName(), root) : QString();
    if (id.isEmpty()) {
        emit noticeOccurred(QStringLiteral("导入项目失败：无法写入项目记录"));
        return QString();
    }
    refreshProjects();
    assignProjectIfActiveTabUnbound(id);
    return id;
}

void AgentViewModel::removeProject(const QString& projectId) {
    if (!m_memory) return;
    m_memory->removeProject(projectId);   // 会话保留（project_id 置 NULL）
    // docs/18: 归属按标签持有，被删项目可能同时被多个标签引用——逐标签清理，
    // 不能只清"活动项目"那一个（旧模型下所有标签共享同一值，清一次就够）。
    bool activeCleared = false;
    for (ChatSessionController* controller : std::as_const(m_tabs)) {
        if (controller->activeProjectId() != projectId) continue;
        applyProjectTo(controller->agent(), QString());
        if (controller == active()) activeCleared = true;
    }
    refreshProjects();
    refreshSessions();
    if (activeCleared) {
        syncSandboxMirror();
        emit activeProjectIdChanged();
    }
}

// P2: 项目重命名（侧栏项目行内联编辑）。归属该项目的标签都要刷新系统提示里
// 的项目名——旧实现只处理"活动项目"，其他标签会留着旧名字。
void AgentViewModel::renameProject(const QString& projectId, const QString& name) {
    const QString trimmed = name.trimmed();
    if (!m_memory || projectId.isEmpty() || trimmed.isEmpty()) return;
    m_memory->renameProject(projectId, trimmed.left(80));
    refreshProjects();   // 先刷新，下面重新查表才能取到新名字
    for (ChatSessionController* controller : std::as_const(m_tabs)) {
        if (controller->activeProjectId() == projectId)
            applyProjectTo(controller->agent(), projectId);
    }
}

void AgentViewModel::searchMessages(const QString& keyword) {
    const QVariantList results = m_memory
        ? m_memory->searchMessages(keyword, 50) : QVariantList();
    if (m_searchResults == results) return;
    m_searchResults = results;
    emit searchResultsChanged();
}

// Batch 18 (FEAT-1801): export one session as Markdown or JSON. Reads go
// through the persistence backend attached in setMemory(); the file write is
// atomic (QSaveFile) so a failed export never leaves a half-written file.
// Metadata-only usage event: session id + format, never message content.
bool AgentViewModel::exportSession(const QString& sessionId, const QString& path,
                                   const QString& format) {
    if (!m_memory) {
        emit errorOccurred(tr("导出失败：未连接持久化后端"));
        return false;
    }
    const bool markdown = format == QLatin1String("md");
    if (!markdown && format != QLatin1String("json")) {
        emit errorOccurred(tr("导出失败：不支持的格式“%1”（可选 md / json）").arg(format));
        return false;
    }
    // QML FileDialog hands over a file:// URL; plain paths (tests) pass through.
    const QUrl urlArg(path);
    const QString localPath = urlArg.isLocalFile() ? urlArg.toLocalFile() : path;

    const QJsonArray messages = m_memory->loadMessages(sessionId);
    const QJsonArray traceNodes = m_memory->loadTraceNodes(sessionId);
    const QString exportedAt =
        QDateTime::currentDateTime().toString(Qt::ISODateWithMs);

    QByteArray payload;
    if (markdown) {
        QString md;
        md += tr("# NexusAI 会话导出\n\n");
        md += tr("- 会话 ID：%1\n").arg(sessionId);
        md += tr("- 导出时间：%1\n").arg(exportedAt);
        md += tr("- 消息数：%1\n\n---\n\n").arg(messages.size());
        for (const auto& entry : messages) {
            const QJsonObject msg = entry.toObject();
            const QString role = msg.value("role").toString();
            const QString label = role == QLatin1String("user") ? tr("用户")
                                : role == QLatin1String("assistant") ? tr("助手")
                                : role;
            const qint64 ts = msg.value("timestamp").toVariant().toLongLong();
            md += QStringLiteral("## %1 · %2\n\n")
                      .arg(label, QDateTime::fromMSecsSinceEpoch(ts)
                                      .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
            md += msg.value("content").toString() + QStringLiteral("\n\n");
        }
        if (!traceNodes.isEmpty()) {
            md += tr("---\n\n## 过程摘要\n\n");
            // Batch 21 (C2): node fields are emitted on a single bullet line,
            // so embedded CR/LF (from tool names/titles or model output) must
            // be flattened or the Markdown list structure breaks.
            auto oneLine = [](const QString& s) {
                return QString(s).remove(QLatin1Char('\r'))
                                 .replace(QLatin1Char('\n'), QLatin1Char(' '))
                                 .trimmed();
            };
            for (const auto& entry : traceNodes) {
                const QJsonObject node = entry.toObject();
                md += QStringLiteral("- [%1] %2 · %3\n")
                          .arg(oneLine(node.value("status").toString()),
                               oneLine(node.value("type").toString()),
                               oneLine(node.value("title").toString()));
            }
        }
        payload = md.toUtf8();
    } else {
        QJsonObject root;
        root["sessionId"] = sessionId;
        root["exportedAt"] = exportedAt;
        root["messages"] = messages;
        root["traceNodes"] = traceNodes;
        payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    }

    QSaveFile file(localPath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit errorOccurred(tr("导出失败：无法写入 %1").arg(localPath));
        return false;
    }
    file.write(payload);
    if (!file.commit()) {
        emit errorOccurred(tr("导出失败：写入未完成 %1").arg(localPath));
        return false;
    }

    Activity::usage(Activity::Kind::User, QStringLiteral("user.export"),
                    sessionId, 0, format);
    emit noticeOccurred(tr("已导出会话（%1 条消息）：%2")
                            .arg(messages.size()).arg(QFileInfo(localPath).fileName()));
    return true;
}

ChatSessionController* AgentViewModel::controllerWithAgent() const {
    if (active()->agent()) return active();
    for (ChatSessionController* controller : m_tabs) {
        if (controller->agent()) return controller;
    }
    return nullptr;
}

void AgentViewModel::refreshSessions() {
    // Every tab agent reads the same database, so pulling the history from
    // any attached one yields the shared list.
    ChatSessionController* controller = controllerWithAgent();
    const QVariantList all = controller
        ? controller->agent()->sessionList().toVariantList() : QVariantList{};
    // docs/17（树状共存）: 返回全量会话（每项带 projectId），侧栏按
    // 项目/本地分组展示，不再按活动视图过滤——所有会话始终可见，
    // "回到本地对话"不再依赖切换。
    const QVariantList sessions = all;
    if (m_sessionList == sessions) return;
    m_sessionList = sessions;
    emit sessionListChanged();
    emit tabsChanged();   // tab titles may resolve now
}

// ── Knowledge base (RAG) ─────────────────────────────────

void AgentViewModel::setRagService(RagService* rag) {
    if (m_rag == rag) return;
    if (m_rag) disconnect(m_rag, nullptr, this, nullptr);
    m_rag = rag;
    if (m_rag) {
        // changed() may be emitted from worker threads; the queued call to
        // refreshKnowledge() updates the property BEFORE the QML notify.
        // (Forwarding the signal straight to knowledgeChanged would make QML
        // re-read a stale list - documents imported fine but never showed up.)
        connect(m_rag, &RagService::changed, this, &AgentViewModel::refreshKnowledge);
    }
    refreshKnowledge();
}

void AgentViewModel::setKnowledgeContentProvider(std::function<QString(const QString&)> provider) {
    m_contentProvider = std::move(provider);
}

void AgentViewModel::refreshKnowledge() {
    const QVariantList docs = m_rag
        ? m_rag->documents().toVariantList() : QVariantList{};
    if (m_knowledgeDocuments == docs) return;
    m_knowledgeDocuments = docs;
    emit knowledgeChanged();
}

void AgentViewModel::importDocument(const QString& path) {
    if (!m_rag || m_knowledgeBusy) return;
    const QString cleanPath = QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path;
    const QFileInfo info(cleanPath);
    if (!info.isFile()) {
        emit errorOccurred(tr("无法读取文件：%1").arg(cleanPath));
        return;
    }
    QFile file(cleanPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit errorOccurred(tr("无法打开文件：%1").arg(file.errorString()));
        return;
    }
    const QString content = QString::fromUtf8(file.readAll());
    file.close();

    // FEAT-802: duplicate content is rejected up front (before the expensive
    // embedding); same-name-different-content files coexist under a
    // deduplicated display name.
    const QString hash = knowledgeContentHash(content);
    if (m_rag->containsContentHash(hash)) {
        emit noticeOccurred(tr("文档内容已存在，已跳过：%1").arg(info.fileName()));
        return;
    }
    const QString name = m_rag->uniqueDocumentName(info.fileName());
    Activity::usage(Activity::Kind::User, QStringLiteral("doc.import"), name);
    startIngestion(name, content, false);
}

void AgentViewModel::removeDocument(const QString& name) {
    Activity::usage(Activity::Kind::User, QStringLiteral("doc.remove"), name);
    if (m_rag && m_rag->removeDocument(name))
        emit documentRemoved(name);
}

void AgentViewModel::clearKnowledge() {
    Activity::usage(Activity::Kind::User, QStringLiteral("kb.clear"));
    if (!m_rag) return;
    m_rag->clear();
    emit knowledgeCleared();
}

QVariantList AgentViewModel::documentChunks(const QString& name) {
    return m_rag ? m_rag->documentChunks(name).toVariantList() : QVariantList{};
}

void AgentViewModel::reembedDocument(const QString& name) {
    if (!m_rag || m_knowledgeBusy) return;
    Activity::usage(Activity::Kind::User, QStringLiteral("doc.reembed"), name);
    const QString content = m_contentProvider ? m_contentProvider(name) : QString();
    if (content.trimmed().isEmpty()) {
        emit errorOccurred(tr("未找到文档内容，无法重新嵌入：%1").arg(name));
        return;
    }

    startIngestion(name, content, true);
}

void AgentViewModel::startIngestion(const QString& name, const QString& content, bool reembed) {
    setKnowledgeBusy(true);
    setEmbedProgress(0, 0);
    auto* watcher = new QFutureWatcher<int>(this);
    connect(watcher, &QFutureWatcherBase::finished, this,
            [this, watcher, name, content, reembed]() {
        const int chunks = watcher->result();
        watcher->deleteLater();
        setKnowledgeBusy(false);
        if (reembed) {
            if (chunks > 0)
                emit noticeOccurred(tr("已重新嵌入 %1（%2 段）").arg(name).arg(chunks));
            else
                emit errorOccurred(tr("重新嵌入失败：%1").arg(name));
            return;
        }
        if (chunks > 0) {
            emit noticeOccurred(tr("已导入 %1（%2 段）").arg(name).arg(chunks));
        } else if (chunks == 0) {
            emit errorOccurred(tr("导入失败（文件为空或无法分块）：%1").arg(name));
        } else if (chunks == -3) {
            emit errorOccurred(tr("知识库总量已达上限（最多 %1 段），请先删除部分文档后再导入")
                .arg(m_rag ? m_rag->maxTotalChunks() : RagService::MaxTotalChunks));
        } else {
            emit errorOccurred(tr("文件过大（超过单文档 %1 段上限）：%2")
                .arg(RagService::MaxChunksPerDocument).arg(name));
        }
    });

    // Chunking + batch embedding can take a while (single network request);
    // run off the GUI thread. The worker lambda captures only values - never
    // `this` - so tearing the view model down mid-import cannot dangle; the
    // watcher (parented to this) delivers the outcome on the GUI thread,
    // where touching the busy flag and emitting signals is safe.
    RagService* rag = m_rag;
    watcher->setFuture(QtConcurrent::run([rag, name, content]() -> int {
        return rag ? rag->ingestDocument(name, content) : 0;
    }));
}

void AgentViewModel::setKnowledgeBusy(bool busy) {
    if (m_knowledgeBusy == busy) return;
    m_knowledgeBusy = busy;
    emit knowledgeBusyChanged();
}

void AgentViewModel::setEmbedProgress(int done, int total) {
    if (m_embedDone == done && m_embedTotal == total) return;
    m_embedDone = done;
    m_embedTotal = total;
    emit knowledgeEmbedProgressChanged();
}

// ── Session / audit management ───────────────────────────
// Global operations route through any attached agent (all tab agents read
// the same database).

void AgentViewModel::deleteSession(const QString& sessionId) {
    Activity::usage(Activity::Kind::User, QStringLiteral("session.delete"),
                    sessionId);
    if (ChatSessionController* controller = controllerWithAgent())
        controller->deleteSession(sessionId);
}

void AgentViewModel::renameSession(const QString& sessionId, const QString& title) {
    Activity::usage(Activity::Kind::User, QStringLiteral("session.rename"),
                    sessionId);
    if (ChatSessionController* controller = controllerWithAgent())
        controller->renameSession(sessionId, title);
}

QVariantList AgentViewModel::auditLog(int limit) {
    ChatSessionController* controller = controllerWithAgent();
    return controller ? controller->auditLog(limit) : QVariantList{};
}

// M1: convert a QJsonArray of episodes (snake_case DB fields) into the
// camelCase QVariantList the QML delegate consumes.
namespace {
QVariantList episodesToVariantList(const QJsonArray& arr) {
    QVariantList out;
    out.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        out.append(QVariantMap{
            {QStringLiteral("id"), o.value(QStringLiteral("id")).toVariant()},
            {QStringLiteral("sessionId"), o.value(QStringLiteral("session_id")).toString()},
            {QStringLiteral("summary"), o.value(QStringLiteral("summary")).toString()},
            {QStringLiteral("tags"), o.value(QStringLiteral("tags")).toString()},
            {QStringLiteral("timestamp"), o.value(QStringLiteral("timestamp")).toVariant()},
        });
    }
    return out;
}
} // namespace

QVariantList AgentViewModel::listEpisodic(int limit) {
    if (!m_memory) return {};
    return episodesToVariantList(m_memory->listEpisodic(limit));
}

QVariantList AgentViewModel::searchEpisodic(const QString& keyword, int limit) {
    if (!m_memory) return {};
    return episodesToVariantList(m_memory->searchEpisodic(keyword, limit));
}

bool AgentViewModel::removeEpisodic(qint64 id) {
    if (!m_memory) return false;
    const bool ok = m_memory->deleteEpisodic(id);
    if (ok)
        Activity::usage(Activity::Kind::User,
                        QStringLiteral("memory.episodic.delete"),
                        QString::number(id));
    return ok;
}

int AgentViewModel::clearEpisodic() {
    if (!m_memory) return 0;
    const int removed = m_memory->deleteAllEpisodic();
    if (removed > 0)
        Activity::usage(Activity::Kind::User,
                        QStringLiteral("memory.episodic.clear"),
                        QString::number(removed));
    return removed;
}

QVariantMap AgentViewModel::tokenStats(int recentDays) {
    return m_memory ? m_memory->tokenStats(recentDays) : QVariantMap{};
}

// docs/16 §8: forward to IMemory (SqliteMemory) so QML can call
// AgentVM.tokenStatsDetailed / costStats / quotaStatus directly.
QVariantMap AgentViewModel::tokenStatsDetailed(int recentDays) {
    return m_memory ? m_memory->tokenStatsDetailed(recentDays) : QVariantMap{};
}

QVariantList AgentViewModel::tokenStatsBySource(int recentDays) {
    return m_memory ? m_memory->tokenStatsBySource(recentDays) : QVariantList{};
}

QVariantMap AgentViewModel::costStats(int recentDays) {
    return m_memory ? m_memory->costStats(recentDays) : QVariantMap{};
}

QVariantMap AgentViewModel::quotaStatus() {
    return m_memory ? m_memory->quotaStatus() : QVariantMap{};
}

// docs/16 §8.3: TopBar 悬停浮层的数据源。取当前激活 tab 的会话 id 做
// 会话级聚合；浮层每次打开时重新调用，天然跟随会话切换与请求完成刷新。
QVariantMap AgentViewModel::sessionTokenBreakdown() const {
    const QString sessionId = active()->currentSessionId();
    if (sessionId.isEmpty() || !m_memory) return QVariantMap{};
    return m_memory->sessionTokenBreakdown(sessionId);
}

// docs/16 §8.3: TopBar 容量占比浮层。取激活 tab 的实时上下文构成；
// 每次打开浮层时重新查询，跟随会话切换与任务进行刷新。
QVariantMap AgentViewModel::contextComposition() const {
    return active()->contextComposition();
}

QString AgentViewModel::logFilePath() const {
    return Logger::logFilePath();
}

// ── Token ────────────────────────────────────────────────

double AgentViewModel::tokenUsageRate() const {
    const int max = maxTokens();
    if (max == 0) return 0.0;
    return static_cast<double>(usedTokens()) / static_cast<double>(max);
}

// ── Tool Registry Init ────────────────────────────────────

void AgentViewModel::refreshToolRegistry() {
    m_toolRegistryModel.clear();
    const auto defs = ToolRegistry::instance().listAll();
    for (const auto& def : defs) {
        QVariantMap tool;
        tool["name"] = def.name;
        tool["description"] = def.description;
        tool["level"] = def.level == ToolLevel::Safe ? "safe"
                      : def.level == ToolLevel::RequiresConfirm ? "requires_confirm"
                      : "dangerous";
        tool["enabled"] = ToolRegistry::instance().isToolEnabled(def.name);
        // docs/17 B1: 策略拦截与用户禁用分离展示——被拦截时 Switch 仍反映
        // 用户层状态，页面据此显示"仅本地模式拦截"徽标。
        tool["blockedByPolicy"] =
            ToolRegistry::instance().isPolicyBlocked(def.name);
        tool["source"] = def.name.startsWith(QLatin1String("mcp__"))
                       ? QStringLiteral("MCP") : QStringLiteral("builtin");
        m_toolRegistryModel.append(tool);
    }
    emit toolRegistryModelChanged();
}

void AgentViewModel::setToolEnabled(const QString& name, bool enabled) {
    ToolRegistry::instance().setToolEnabled(name, enabled);
    // toolsChanged → refreshToolRegistry fires automatically.
}

// ── QML Actions ───────────────────────────────────────────

namespace {

// Batch 30 (F6): decode → downscale so the longest edge fits maxEdge →
// re-encode JPEG. Lives in the viewmodels layer because QImage needs QtGui,
// which nexus_core deliberately does not link.
QString compressedImageDataUrl(const QString& raw, int maxEdge = 1568, int quality = 85) {
    const auto parsed = DataUrlUtils::parseDataUrl(raw);
    if (!parsed.ok) return {};

    QImage image;
    if (!image.loadFromData(QByteArray::fromBase64(parsed.base64))) return {};

    if (image.width() > maxEdge || image.height() > maxEdge)
        image = image.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "JPEG", quality)) return {};

    return QStringLiteral("data:image/jpeg;base64,")
        + QString::fromLatin1(bytes.toBase64());
}

} // namespace

void AgentViewModel::startTask(const QString& input, const QStringList& images,
                               const QVariantList& files) {
    QStringList compressed;
    compressed.reserve(images.size());
    for (const QString& raw : images) {
        const QString dataUrl = compressedImageDataUrl(raw);
        if (!dataUrl.isEmpty())
            compressed.append(dataUrl);
    }
    // Text attachments: {kind:"file", name, content} maps → JSON array.
    QJsonArray fileArray;
    for (const QVariant& file : files) {
        const QVariantMap f = file.toMap();
        if (f.value("kind").toString() != QLatin1String("file")) continue;
        fileArray.append(QJsonObject{
            {"name", f.value("name").toString()},
            {"content", f.value("content").toString()},
        });
    }
    active()->startTask(input, compressed, fileArray);
}

// Batch 30 (F6): read a text file as a chat attachment. Binary content
// (embedded NULs) and oversized files are rejected via noticeOccurred; the
// QML side just skips adding when the map comes back empty.
QVariantMap AgentViewModel::loadTextFileAttachment(const QString& path) {
    const QUrl urlArg(path);
    const QString localPath = urlArg.isLocalFile() ? urlArg.toLocalFile() : path;
    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray bytes = file.readAll();
    if (bytes.size() > MaxTextAttachmentBytes) {
        emit noticeOccurred(tr("文件过大（附件上限 %1 KB）：%2")
            .arg(MaxTextAttachmentBytes / 1024).arg(QFileInfo(localPath).fileName()));
        return {};
    }
    if (bytes.contains('\0')) {
        emit noticeOccurred(tr("暂不支持二进制文件作为附件：%1")
            .arg(QFileInfo(localPath).fileName()));
        return {};
    }
    return QVariantMap{
        {"kind", "file"},
        {"name", QFileInfo(localPath).fileName()},
        {"content", QString::fromUtf8(bytes)},
    };
}

void AgentViewModel::notifyUnsupportedDrop(int count) {
    if (count <= 0) return;
    emit noticeOccurred(tr("已忽略 %1 个不支持的文件类型（仅支持图片与文本文件）")
                            .arg(count));
}

// docs/20 §3.4（A4）：窗口级拖放解析。入参是 QML DropArea 的 drop.urls
// 原始值——QML 的 url 对象没有 toLocalFile()，路径转换只能在 C++ 做
// （见头文件与 main.qml 的契约注释）。逐个分类 → 图片压缩 / 文本读取，
// 汇总为附件描述符后经 attachmentsDropped 通知 QML。
void AgentViewModel::handleDroppedFiles(const QList<QUrl>& urls) {
    QVariantList attachments;
    int unsupported = 0;
    for (const QUrl& url : urls) {
        // 只处理本地文件；非 file: 的 URL（http 等）计入不支持。
        if (!url.isLocalFile()) {
            ++unsupported;
            continue;
        }
        const QString path = url.toLocalFile();
        switch (AttachmentClassifier::classify(path)) {
        case AttachmentClassifier::Kind::Image: {
            const QString dataUrl = loadImageDataUrl(path);
            if (!dataUrl.isEmpty())
                attachments.append(QVariantMap{{"kind", "image"}, {"dataUrl", dataUrl}});
            break;
        }
        case AttachmentClassifier::Kind::Text: {
            const QVariantMap file = loadTextFileAttachment(path);
            if (!file.isEmpty())
                attachments.append(file);
            break;
        }
        case AttachmentClassifier::Kind::Unsupported:
            ++unsupported;
            break;
        }
    }
    if (!attachments.isEmpty())
        emit attachmentsDropped(attachments);
    if (unsupported > 0)
        notifyUnsupportedDrop(unsupported);
}

// Batch 30 (F6): compressed data URL of an image file chosen in QML (empty
// when unreadable). QML cannot reach QImage, so the conversion and
// downsizing happen here.
QString AgentViewModel::loadImageDataUrl(const QString& path) const {
    const QUrl urlArg(path);
    const QString localPath = urlArg.isLocalFile() ? urlArg.toLocalFile() : path;
    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    // NB: the header must match "data:image/<mime>;base64,..." or
    // DataUrlUtils::parseDataUrl rejects it (this broke file uploads once).
    return compressedImageDataUrl(
        QStringLiteral("data:image/png;base64,")
            + QString::fromLatin1(file.readAll().toBase64()));
}

// Batch 30 (F6): compressed data URL of the clipboard image (empty when the
// clipboard holds no image). QML cannot reach QImage, so the conversion and
// downsizing happen here.
QString AgentViewModel::clipboardImageDataUrl() const {
    const QImage image = QGuiApplication::clipboard()->image();
    if (image.isNull()) return {};
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return compressedImageDataUrl(
        QStringLiteral("data:image/png;base64,")
            + QString::fromLatin1(png.toBase64()));
}

void AgentViewModel::approveToolCall(const QString& id, bool approved) {
    // Route to whichever tab is waiting for this confirmation — the dialog
    // is global, so it may belong to a background tab.
    for (ChatSessionController* controller : m_tabs) {
        if (controller->state() == AgentState::AwaitingToolConfirm) {
            controller->approveToolCall(id, approved);
            return;
        }
    }
}

void AgentViewModel::stop() {
    active()->stop();
}

// docs/20 §3.1（FORK-701）：重新生成最后一条回答（活动标签）。busy 时由
// Controller 拒绝；agent 缺失时不执行。
void AgentViewModel::regenerate() {
    active()->regenerate();
}

// docs/20 §3.2（FORK-701）：编辑重发。等价于「对最后一条 user 消息执行
// regenerate + 替换内容」：删除持久层最后一条 user 行及之后全部消息，
// 以 newText 重新发起。图片/文件附件不保留（与 resume 契约一致：干净
// 白。空文本/无历史/无 agent 时静默返回。
void AgentViewModel::editAndResend(const QString& newText) {
    const QString trimmed = newText.trimmed();
    if (trimmed.isEmpty()) return;
    ChatSessionController* controller = active();
    if (controller->isBusy()) return;
    AIAgent* agent = controller->agent();
    if (agent == nullptr) return;   // 无持久层可修改

    const qint64 userId = agent->memory()
                              ? agent->memory()->lastUserMessageId(
                                    controller->currentSessionId())
                              : 0;
    if (userId > 0)
        agent->memory()->deleteMessagesFrom(controller->currentSessionId(), userId);
    controller->startTask(trimmed);
}

void AgentViewModel::newChat() {
    active()->newChat();
}

void AgentViewModel::loadSession(const QString& sessionId) {
    // docs/17 P0-3 + docs/18: 会话与项目联动，且只作用于**承载它的那个标签**
    // （旧实现改的是全局活动项目，会连带影响其他标签）。项目会话 → 该标签
    // 归属其项目；本地会话 → 该标签回到本地（空）。
    QString target;
    bool found = false;
    for (const QVariant& entry : std::as_const(m_sessionList)) {
        const QVariantMap s = entry.toMap();
        if (s.value("id").toString() == sessionId) {
            target = s.value("projectId").toString();
            found = true;
            break;
        }
    }
    // 会话不在列表里（列表陈旧）时保持当前归属，不要误清成空。
    if (!found)
        target = activeProjectId();
    // Never interrupt a running task: when the active tab is busy the session
    // opens in a fresh tab instead——新标签的归属直接就是该会话的归属。
    if (active()->isBusy())
        addTab(target);
    else
        setActiveProjectId(target);
    active()->loadSession(sessionId);
}

void AgentViewModel::cancel() {
    newChat();
}

void AgentViewModel::copyToClipboard(const QString& text) {
    if (!text.isEmpty())
        QGuiApplication::clipboard()->setText(text);
}

// 日志开发 doc 6 §3.3: open the rotating log directory at the current file
void AgentViewModel::openLogFolder() {
    const QString dir = Logger::logDir();
    if (!dir.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

// ── Draft Text ───────────────────────────────────────────

void AgentViewModel::setDraftText(const QString& text) {
    active()->setDraftText(text);
}

// ── Session permission mode ─────────────────────────────

// QML 切换器的写入点：只作用于当前激活 tab，标记 overridden。
void AgentViewModel::setPermissionMode(int mode) {
    active()->setPermissionMode(mode);
}

void AgentViewModel::setDefaultPermissionMode(int mode) {
    if (mode == m_defaultPermissionMode) return;
    m_defaultPermissionMode = mode;
    // 追补：VM 构造早于设置注入（首个 tab 已存在），且用户可能改了
    // 默认值——未显式切换过的 tab 跟随新默认。
    for (ChatSessionController* controller : m_tabs)
        controller->applyDefaultPermissionMode(mode);
}

void AgentViewModel::setIsPanelVisible(bool visible) {
    if (m_isPanelVisible == visible) return;
    m_isPanelVisible = visible;
    QSettings("NexusAI", "NexusAI").setValue("ui/panelVisible", visible);
    emit panelVisibleChanged();
}

void AgentViewModel::togglePanel() {
    setIsPanelVisible(!m_isPanelVisible);
}

void AgentViewModel::setPanelWidth(int width) {
    width = qBound(220, width, 640);
    if (m_panelWidth == width) return;
    m_panelWidth = width;
    emit panelWidthChanged();
    m_uiPersistTimer.start();
}

void AgentViewModel::setIsSidebarExpanded(bool expanded) {
    if (m_isSidebarExpanded == expanded) return;
    m_isSidebarExpanded = expanded;
    QSettings("NexusAI", "NexusAI").setValue("ui/sidebarExpanded", expanded);
    emit sidebarExpandedChanged();
}

void AgentViewModel::toggleSidebar() {
    setIsSidebarExpanded(!m_isSidebarExpanded);
}

void AgentViewModel::setSidebarWidth(int width) {
    width = qBound(180, width, 560);
    if (m_sidebarWidth == width) return;
    m_sidebarWidth = width;
    emit sidebarWidthChanged();
    m_uiPersistTimer.start();
}

// ── Layout defaults ──────────────────────────────────────

void AgentViewModel::setDefaultSidebarWidth(int width) {
    if (m_defaultSidebarWidth == width) return;
    m_defaultSidebarWidth = width;
    PreferenceStore::instance()->save(QStringLiteral("ui.defaultSidebarWidth"), width);
    emit defaultSidebarWidthChanged();
}

void AgentViewModel::setDefaultPanelWidth(int width) {
    if (m_defaultPanelWidth == width) return;
    m_defaultPanelWidth = width;
    PreferenceStore::instance()->save(QStringLiteral("ui.defaultPanelWidth"), width);
    emit defaultPanelWidthChanged();
}

void AgentViewModel::setDefaultSidebarExpanded(bool expanded) {
    if (m_defaultSidebarExpanded == expanded) return;
    m_defaultSidebarExpanded = expanded;
    PreferenceStore::instance()->save(QStringLiteral("ui.defaultSidebarExpanded"), expanded);
    emit defaultSidebarExpandedChanged();
}

void AgentViewModel::setDefaultPanelVisible(bool visible) {
    if (m_defaultPanelVisible == visible) return;
    m_defaultPanelVisible = visible;
    PreferenceStore::instance()->save(QStringLiteral("ui.defaultPanelVisible"), visible);
    emit defaultPanelVisibleChanged();
}

void AgentViewModel::setRestoreWindowOnStartup(bool restore) {
    if (m_restoreWindowOnStartup == restore) return;
    m_restoreWindowOnStartup = restore;
    PreferenceStore::instance()->save(QStringLiteral("ui.restoreWindowOnStartup"), restore);
    emit restoreWindowOnStartupChanged();
}

// ── Window geometry ──────────────────────────────────────

QVariantMap AgentViewModel::loadWindowGeometry() const {
    const QSettings s("NexusAI", "NexusAI");
    if (!s.contains("ui/windowX")) return {};

    const double x = s.value("ui/windowX").toDouble();
    const double y = s.value("ui/windowY").toDouble();

    QVariantMap geo;
    // Reject a position that landed off every screen (e.g. an unplugged
    // monitor): the window would restore out of the user's reach.
    const QPoint pos(static_cast<int>(x), static_cast<int>(y));
    bool onScreen = false;
    const QList<QScreen*> screens = QGuiApplication::screens();
    for (const QScreen* screen : screens) {
        if (screen->availableGeometry().adjusted(-40, -40, 40, 40).contains(pos)) {
            onScreen = true;
            break;
        }
    }

    geo.insert(QStringLiteral("valid"), onScreen);
    geo.insert(QStringLiteral("x"), x);
    geo.insert(QStringLiteral("y"), y);
    geo.insert(QStringLiteral("width"), s.value("ui/windowWidth").toDouble());
    geo.insert(QStringLiteral("height"), s.value("ui/windowHeight").toDouble());
    geo.insert(QStringLiteral("maximized"), s.value("ui/windowMaximized", false).toBool());
    return geo;
}

void AgentViewModel::saveWindowGeometry(double x, double y, double width, double height, bool maximized) {
    QSettings s("NexusAI", "NexusAI");
    s.setValue("ui/windowX", x);
    s.setValue("ui/windowY", y);
    s.setValue("ui/windowWidth", width);
    s.setValue("ui/windowHeight", height);
    s.setValue("ui/windowMaximized", maximized);
}

} // namespace NexusAI
