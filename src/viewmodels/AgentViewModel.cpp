#include "viewmodels/AgentViewModel.h"

#include "viewmodels/PreferenceStore.h"
#include "viewmodels/ChangeBus.h"
#include "viewmodels/SettingsService.h"
#include "core/agent/AIAgent.h"
#include "core/memory/IMemory.h"
#include "core/memory/SqliteMemory.h"
#include "core/rag/RagService.h"
#include "core/rag/KnowledgeChunk.h"
#include "viewmodels/PdfTextExtractor.h"
#include "core/tools/ToolRegistry.h"
#include "core/tools/Sandbox.h"
#include "core/utils/Logger.h"
#include "core/utils/DataUrlUtils.h"
#include "core/utils/AttachmentClassifier.h"
#include "core/utils/ProjectFileIndex.h"
#include "core/utils/SettingsRepository.h"
#include "core/utils/SettingsSchema.h"

#include <QFileInfo>
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
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

namespace NexusAI {
namespace {

// docs/25: 单文件批量导入结果。status:
//   ok | degraded | duplicate | failed | cancelled
struct BatchFileResult {
    QString path;
    QString name;
    QString status;
    QString message;
    int chunks{0};

    QVariantMap toMap() const {
        return QVariantMap{{QStringLiteral("path"), path},
                           {QStringLiteral("name"), name},
                           {QStringLiteral("status"), status},
                           {QStringLiteral("message"), message},
                           {QStringLiteral("chunks"), chunks}};
    }
};

// docs/25: worker 侧的单文件导入——读取（PDF 走 QPdf 提取，其余按 UTF-8
// 文本）→ 内容去重 → 唯一命名 → 入库。与单文件路径（importExtractedText）
// 的语义一致，但以状态值而非信号/提示上报。只捕获值与 rag（过程生命
// 周期对象，docs/24 H6），绝不捕获 view model。
BatchFileResult readAndIngestFile(const QString& path, RagService* rag) {
    BatchFileResult result;
    result.path = path;
    const QFileInfo info(QUrl(path).isLocalFile()
                             ? QUrl(path).toLocalFile()
                             : path);
    result.name = info.fileName();

    QString content;
    QStringList pageTexts;
    if (info.suffix().compare(QLatin1String("pdf"), Qt::CaseInsensitive) == 0) {
        const PdfExtractResult pdf = extractPdfText(info.absoluteFilePath());
        if (!pdf.ok()) {
            result.status = QStringLiteral("failed");
            switch (pdf.status) {
            case PdfExtractStatus::Encrypted:
                result.message = QObject::tr("PDF 已加密或需要密码");
                break;
            case PdfExtractStatus::Corrupted:
                result.message = QObject::tr("PDF 文件已损坏或格式无效");
                break;
            case PdfExtractStatus::NoText:
                result.message = QObject::tr("PDF 没有文本层（可能是扫描件）");
                break;
            default:
                result.message = QObject::tr("PDF 读取失败");
                break;
            }
            return result;
        }
        content = pdf.text;
        pageTexts = pdf.pageTexts;   // docs/25: chunk 页码映射数据源
    } else {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            result.status = QStringLiteral("failed");
            result.message = QObject::tr("无法打开文件");
            return result;
        }
        content = QString::fromUtf8(file.readAll());
        file.close();
    }

    if (content.trimmed().isEmpty()) {
        result.status = QStringLiteral("failed");
        result.message = QObject::tr("内容为空或无可用分段");
        return result;
    }

    const QString hash = knowledgeContentHash(content);
    if (rag->containsContentHash(hash)) {
        result.status = QStringLiteral("duplicate");
        result.message = QObject::tr("相同内容的文档已存在");
        return result;
    }

    const QString name = rag->uniqueDocumentName(info.fileName());
    result.name = name;
    const int chunks = rag->ingestDocument(name, content, pageTexts);
    result.chunks = chunks;
    if (chunks > 0) {
        if (rag->usesPseudoEmbedding(name)) {
            result.status = QStringLiteral("degraded");
            result.message =
                QObject::tr("嵌入服务不可用，已降级为本地伪嵌入（可稍后重新嵌入）");
        } else {
            result.status = QStringLiteral("ok");
        }
    } else if (chunks == -1) {
        result.status = QStringLiteral("failed");
        result.message = QObject::tr("文档过大，超过单文档分段上限");
    } else if (chunks == -3) {
        result.status = QStringLiteral("failed");
        result.message = QObject::tr("超出知识库总段数预算");
    } else {
        result.status = QStringLiteral("failed");
        result.message = QObject::tr("导入失败");
    }
    return result;
}

void persistSettingsValue(const QString& key, const QVariant& value) {
    QString error;
    if (!SettingsRepository::writeValue(key, value, &error))
        qWarning() << "Failed to persist setting" << key << error;
}

void persistSettingsBatch(const QVariantMap& values) {
    QString error;
    if (!SettingsRepository::writeBatch(values, &error))
        qWarning() << "Failed to persist settings" << values.keys() << error;
}

} // namespace

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

    // Layout defaults must load first: they seed the panel restore below.
    loadLayoutDefaults();
    subscribeLayoutChanges();

    // Restore last UI layout (sidebar / context panel visibility + widths),
    // falling back to the configured defaults when nothing was persisted.
    m_isPanelVisible = SettingsRepository::value("ui/panelVisible", m_defaultPanelVisible).toBool();
    m_isSidebarExpanded = SettingsRepository::value("ui/sidebarExpanded", m_defaultSidebarExpanded).toBool();
    m_panelWidth = SettingsRepository::value("ui/panelWidth", m_defaultPanelWidth).toInt();
    m_sidebarWidth = SettingsRepository::value("ui/sidebarWidth", m_defaultSidebarWidth).toInt();

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
    persistSettingsBatch({
        {QStringLiteral("ui/sidebarWidth"), m_sidebarWidth},
        {QStringLiteral("ui/panelWidth"), m_panelWidth}});
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
    if (m_activeTab < 0 || m_activeTab >= m_tabs.size())
        return nullptr;
    return m_tabs.at(m_activeTab);
}

ChatSessionController* AgentViewModel::ensureTab() {
    auto* controller = new ChatSessionController(this);

    // Active-tab forwarding: the facade signal is re-emitted only when the
    // change happened in the tab the UI is looking at.
    connect(controller, &ChatSessionController::stateChanged, this, [this, controller] {
        pruneConfirmations();
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
    connect(controller, &ChatSessionController::draftAttachmentsChanged, this, [this, controller] {
        if (controller == active()) emit draftAttachmentsChanged();
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
            [this, controller](const QString& id, const QString& name, const QVariantMap& args, const QString& preview) {
        m_confirmations.append({controller, id, name, args, preview});
        emit pendingConfirmationsChanged();
        emit toolConfirmRequired(id, name, args, preview);
    });
    connect(controller, &QObject::destroyed, this, [this, controller] {
        m_prevBusy.remove(controller);
        pruneConfirmations();
    });
    connect(controller, &ChatSessionController::errorOccurred, this,
            &AgentViewModel::errorOccurred);
    connect(controller, &ChatSessionController::todoListChanged, this,
            [this, controller](const QVariantList& todos) {
        if (controller != active()) return;
        m_todoList = todos;
        emit todoListChanged();
    });
    connect(controller, &ChatSessionController::fileCheckpointsChanged, this,
            [this, controller](const QVariantList& checkpoints) {
        if (controller != active()) return;
        m_fileCheckpoints = checkpoints;
        emit fileCheckpointsChanged();
    });
    connect(controller, &ChatSessionController::episodicChanged,
            this, &AgentViewModel::episodicChanged);
    // Session history is shared (every tab agent reads the same database).
    connect(controller, &ChatSessionController::sessionHistoryChanged, this,
            &AgentViewModel::refreshSessions);

    m_tabs.append(controller);

    // A fresh tab picks up its own agent when the pipeline is configured.
    if (m_agentFactory && m_templateAgent)
        controller->attachAgent(m_agentFactory(), true);
    configureModel(controller);

    // docs/18: 归属由 addTab 显式指定——ensureTab 不再从任何全局状态继承。

    // New tabs start at the configured global default permission mode
    // (no-op for ConfirmChanges; also covers the initial tab created
    // before main.cpp injected the setting).
    controller->applyDefaultPermissionMode(m_defaultPermissionMode);

    // The first tab is active implicitly (m_activeTab == -1 -> last).
    if (controller == active()) {
        m_fileCheckpoints = controller->fileCheckpoints();
        emit fileCheckpointsChanged();
    }

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

// docs/24 R3: database-restore precondition — zero per-tab SQLite
// connections must survive the call. Silent by design: active() indexes
// m_tabs unguarded, so no signal may fire while the list is empty; the
// orchestration ends with openTab(), whose signal burst re-syncs the UI.
void AgentViewModel::closeAllTabs() {
    for (ChatSessionController* controller : std::as_const(m_tabs)) {
        // Stop first, exactly like closeTab: running tasks abort and their
        // unanswered tool calls are sealed by the agent.
        controller->stop();
        controller->deleteLater();
    }
    m_tabs.clear();
    m_activeTab = -1;
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
    emit draftAttachmentsChanged();
    emit chatModelChanged();
    emit tokenUsageChanged();
    emit sessionTokensChanged();
    emit costChanged(m_lastCostCents);
    emit currentSessionIdChanged();
    emit permissionModeChanged();
    m_todoList = active()->todoList();
    emit todoListChanged();
    m_fileCheckpoints = active()->fileCheckpoints();
    emit fileCheckpointsChanged();
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
    emit draftAttachmentsChanged();
    emit chatModelChanged();
    emit tokenUsageChanged();
    emit sessionTokensChanged();
    emit currentSessionIdChanged();
    m_todoList = active()->todoList();
    emit todoListChanged();
    m_fileCheckpoints = active()->fileCheckpoints();
    emit fileCheckpointsChanged();
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
        configureModel(m_tabs[i]);
    }

    emit tabsChanged();
    syncSandboxMirror();
    emit activeProjectIdChanged();
    refreshSessions();
}

void AgentViewModel::setAgentFactory(std::function<AIAgent*()> factory) {
    m_agentFactory = std::move(factory);
}

void AgentViewModel::setModelConfigurator(std::function<void(AIAgent*, const QVariantMap&)> configure) {
    m_configureModel = std::move(configure);
}

void AgentViewModel::updateModelProfiles(const QVariantList& profiles, const QVariantMap& defaultProfile) {
    m_availableProfiles = profiles;
    m_defaultProfile = defaultProfile;
    for (auto* controller : m_tabs) configureModel(controller);
}

void AgentViewModel::configureModel(ChatSessionController* controller) {
    if (!m_configureModel || controller->isBusy()) return;
    QVariantMap profile;
    const QString id = controller->modelProfile.value("id").toString();
    if (id.isEmpty() || (!controller->modelSelected && controller->currentSessionId().isEmpty())) {
        profile = m_defaultProfile;
    } else {
        for (const auto& entry : m_availableProfiles) {
            if (entry.toMap().value("id").toString() == id) {
                profile = entry.toMap();
                break;
            }
        }
    }
    if (!controller->selectedModel.isEmpty()) profile["model"] = controller->selectedModel;
    if (controller->selectedReasoning.isValid()) profile["reasoningEffort"] = controller->selectedReasoning;
    controller->modelProfile = profile;
    if (controller->agent()) m_configureModel(controller->agent(), profile);
    if (controller == active()) emit chatModelChanged();
}

QVariantMap AgentViewModel::chatModelProfile() const {
    auto profile = active()->modelProfile;
    profile.remove("apiKey");
    return profile;
}

bool AgentViewModel::modelConfigured() const {
    return active()->agent() && active()->agent()->hasModelService()
        && (!m_configureModel || !active()->modelProfile.value("model").toString().trimmed().isEmpty());
}

void AgentViewModel::selectChatModel(const QString& profileId, const QString& model) {
    if (active()->isBusy()) {
        emit noticeOccurred(tr("请等待当前任务完成，或停止后切换模型。"));
        return;
    }
    for (const auto& entry : m_availableProfiles) {
        const auto profile = entry.toMap();
        if (profile.value("id").toString() != profileId) continue;
        active()->modelProfile = profile;
        active()->modelSelected = true;
        active()->selectedModel = model;
        active()->selectedReasoning = QVariant();
        configureModel(active());
        return;
    }
}

void AgentViewModel::setChatReasoningEffort(const QString& effort) {
    if (active()->isBusy()) return;
    if (!QStringList{"", "off", "low", "medium", "high"}.contains(effort)) return;
    active()->selectedReasoning = effort;
    configureModel(active());
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
    // 持久化走 SettingsService（docs/25 A5：projects/activeId 的唯一写入口），
    // 内存镜像与 QSettings 同步刷新——Sidebar 读 Settings.activeProjectId 恢复
    // "上次浏览位置"时不再拿到陈旧值。重启后由 main.cpp 回灌。
    SettingsService::instance()->setActiveProjectId(id);
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

// ── G2: '@' project file completion / references ──────────

QVariantList AgentViewModel::listProjectFiles(const QString& query, int limit) {
    const QString root = activeProjectRoot();
    if (root.isEmpty())
        return {};

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool stale = m_projectFilesRoot != root
        || m_projectFilesScannedAt == 0
        || now - m_projectFilesScannedAt > 30000;
    if (stale && !m_projectFilesScanning)
        refreshProjectFiles(root);
    if (m_projectFilesRoot != root)
        return {};

    const QStringList matches = ProjectFileIndex::complete(
        m_projectFiles, query, qBound(1, limit, 200));
    QVariantList out;
    out.reserve(matches.size());
    for (const QString& path : matches)
        out.append(path);
    return out;
}

QVariantMap AgentViewModel::activeFileMention(const QString& text, int cursor) const {
    const ProjectFileIndex::Mention mention =
        ProjectFileIndex::mentionAt(text, cursor);
    return QVariantMap{
        {"active", mention.active},
        {"start", mention.start},
        {"query", mention.query},
        {"quoted", mention.quoted},
    };
}

QVariantList AgentViewModel::resolveFileReferences(const QString& text) {
    const QString root = activeProjectRoot();
    if (root.isEmpty())
        return {};

    const QVector<ProjectFileIndex::Reference> refs =
        ProjectFileIndex::resolveReferences(root, text);
    QVariantList out;
    out.reserve(refs.size());
    for (const auto& ref : refs) {
        out.append(QVariantMap{
            {"kind", QStringLiteral("file")},
            {"name", ref.path},
            {"content", ref.content},
        });
    }
    return out;
}

QVariantList AgentViewModel::inspectFileReferences(const QString& text) const {
    const QVector<ProjectFileIndex::ReferenceCheck> checks =
        ProjectFileIndex::checkReferences(activeProjectRoot(), text);
    QVariantList out;
    out.reserve(checks.size());
    for (const auto& check : checks) {
        out.append(QVariantMap{
            {"token", check.token},
            {"path", check.path},
            {"error", check.error},
        });
    }
    return out;
}

bool AgentViewModel::undoFileCheckpoint(qint64 id) {
    if (!active() || !active()->undoFileCheckpoint(id)) return false;
    // The agent emits fileCheckpointsChanged synchronously, which refreshes
    // this facade; keep a direct read for the return contract's immediacy.
    m_fileCheckpoints = active()->fileCheckpoints();
    emit fileCheckpointsChanged();
    return true;
}

// The scan runs off the GUI thread; a generation counter drops results that
// belong to a project the user already switched away from. The cache is
// refreshed lazily (30s TTL) so typing '@' never blocks on a repo walk.
void AgentViewModel::refreshProjectFiles(const QString& root) {
    if (root.isEmpty()) {
        ++m_projectFilesGeneration;
        m_projectFiles.clear();
        m_projectFilesRoot.clear();
        m_projectFilesScannedAt = 0;
        m_projectFilesScanning = false;
        emit projectFilesChanged();
        return;
    }

    const quint64 generation = ++m_projectFilesGeneration;
    m_projectFilesRoot = root;
    m_projectFilesScanning = true;
    emit projectFilesChanged();

    QPointer<AgentViewModel> self(this);
    (void)QtConcurrent::run([self, root, generation]() {
        const QStringList files = ProjectFileIndex::scan(root);
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, root, generation, files]() {
            if (!self || generation != self->m_projectFilesGeneration)
                return;
            self->m_projectFiles = files;
            self->m_projectFilesRoot = root;
            self->m_projectFilesScannedAt = QDateTime::currentMSecsSinceEpoch();
            self->m_projectFilesScanning = false;
            emit self->projectFilesChanged();
        }, Qt::QueuedConnection);
    });
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

void AgentViewModel::setSessionPinned(const QString& sessionId, bool pinned) {
    if (!m_memory || !m_memory->setSessionPinned(sessionId, pinned)) {
        emit errorOccurred(tr("无法更新会话置顶状态，请重试。"));
        return;
    }
    refreshSessions();
}

void AgentViewModel::setSessionArchived(const QString& sessionId, bool archived) {
    if (!m_memory || !m_memory->setSessionArchived(sessionId, archived)) {
        emit errorOccurred(tr("无法更新会话归档状态，请重试。"));
        return;
    }
    refreshSessions();
    emit noticeOccurred(archived ? tr("会话已归档，可在“包含已归档”中恢复。") : tr("会话已恢复。"));
}

// Batch 17 (FEAT-1702) / Batch 22 (search-aggregate): pass-through to the
// memory backend, which returns one aggregated row per session
// {sessionId, sessionTitle, hitCount, latestTimestamp, bestSnippet,
// bestRole} (the limit passed through caps SESSIONS at 50). The sidebar
// renders these verbatim; an empty keyword yields empty results, which it
// treats as "back to the session list".
void AgentViewModel::searchMessages(const QString& keyword, bool includeArchived) {
    const QVariantList results = m_memory
        ? m_memory->searchMessages(keyword, 50, includeArchived) : QVariantList();
    if (m_searchResults == results) return;
    m_searchResults = results;
    emit searchResultsChanged();
}

// A1 (hit-locate): the sidebar search card hands us a session + the id of
// its newest matching message. loadSession() fills the trace model
// synchronously (resumeSession is synchronous); the locate intent is then
// broadcast on the ChangeBus so the chat page can scroll to & briefly
// highlight that message after its Repeater lays out the new session. A
// messageId <= 0 (e.g. pass-2 failure) just opens the session with no jump.
void AgentViewModel::revealMessage(const QString& sessionId, qint64 messageId) {
    if (sessionId.isEmpty()) return;
    loadSession(sessionId);
    if (messageId <= 0) return;
    auto* bus = ChangeBus::instance();
    if (bus)
        bus->notify(QStringLiteral("search.revealMessage"),
                    QVariant::fromValue(messageId));
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

    // docs/20 §5.2 (C2): PDFs are extracted through QPdfDocument (text layer,
    // page by page); every other suffix keeps the plain text read. The rest of
    // the chain - content hash → uniqueDocumentName → startIngestion - is
    // identical for both paths, so PDF only swaps the "content extraction" step.
    //
    // The PDF parse itself is NOT cheap (QPdfDocument::load + getAllText over
    // every page) and this entry point is a Q_INVOKABLE hit straight from the
    // FileDialog's onAccepted on the GUI thread. Reading a .txt/.md could
    // stay synchronous because it is a single readAll(); a 100+ page PDF
    // would freeze the UI for seconds, so the parse runs on a worker thread
    // and the (cheap) dedup/ingestion steps happen when it reports back.
    if (info.suffix().compare(QLatin1String("pdf"), Qt::CaseInsensitive) == 0) {
        startPdfImport(cleanPath, info);
        return;
    }

    QFile file(cleanPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit errorOccurred(tr("无法打开文件：%1").arg(file.errorString()));
        return;
    }
    const QString content = QString::fromUtf8(file.readAll());
    file.close();

    importExtractedText(info, content);
}

// docs/20 §5.2 (C2): PDF 提取的后台化。与 startIngestion 同一套
// QFutureWatcher 模式：worker lambda 只捕获值（绝不捕获 this），watcher
// 挂在 this 上，结果回到 GUI 线程后才触碰 busy 标志与信号——导入中途销毁
// view model 不会悬垂。
void AgentViewModel::startPdfImport(const QString& path, const QFileInfo& info) {
    setKnowledgeBusy(true);
    setEmbedProgress(0, 0);

    auto* watcher = new QFutureWatcher<PdfExtractResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, info]() {
        const PdfExtractResult pdf = watcher->result();
        watcher->deleteLater();
        if (!pdf.ok()) {
            QString message;
            switch (pdf.status) {
            case PdfExtractStatus::Encrypted:
                message = tr("PDF 已加密或需要密码，无法导入：%1");
                break;
            case PdfExtractStatus::Corrupted:
                message = tr("PDF 文件已损坏或格式无效，无法导入：%1");
                break;
            case PdfExtractStatus::NoText:
                message = tr("未能从 PDF 中提取到文本（可能是扫描件，没有文本层）：%1");
                break;
            case PdfExtractStatus::OpenFailed:
            case PdfExtractStatus::Ok:   // unreachable: !ok() excludes it
                message = tr("无法读取 PDF 文件：%1");
                break;
            }
            // detail 是未翻译的技术原因（"incorrect password" 等），只进日志：
            // 用户看分类后的中文提示，排障时从日志拿到根因。
            qWarning() << "PDF import rejected:" << info.fileName()
                       << "status" << int(pdf.status) << pdf.detail;
            emit errorOccurred(message.arg(info.fileName()));
            setKnowledgeBusy(false);
            return;
        }
        importExtractedText(info, pdf.text, pdf.pageTexts);
    });

    watcher->setFuture(QtConcurrent::run([path]() { return extractPdfText(path); }));
}

// ── docs/25: 批量知识库导入 ──────────────────────────────────

void AgentViewModel::importDocuments(const QStringList& paths) {
    if (!m_rag || paths.isEmpty()) return;
    // 单文件走既有路径：保留其分类错误提示（errorOccurred 弹窗）。
    if (paths.size() == 1) {
        importDocument(paths.first());
        return;
    }
    if (m_knowledgeBusy || m_batchRunning) {
        emit errorOccurred(tr("知识库正忙（导入或重新嵌入进行中），请稍后再试"));
        return;
    }
    QStringList queue;
    for (const QString& p : paths) {
        const QString clean = QUrl(p).isLocalFile() ? QUrl(p).toLocalFile() : p;
        if (!clean.isEmpty()) queue.append(clean);
    }
    if (queue.isEmpty()) return;
    // 新一轮批量导入：清空上一轮结果，避免 done/total 与结果列表跨轮累加。
    // 重试路径（retryFailedBatchImports）不经此处，保留历史条目。
    m_batchResults.clear();
    startBatchQueue(queue);
}

// GUI 线程：取队首文件、派发池任务；任务完成回调里写结果并链到下一个。
// 取消即不再派发后续文件（正在导入的文件会完成——单文件嵌入受 120s
// 超时约束，UI 文案已说明）。每文件一个独立池任务 + watcher 挂 this：
// worker 只捕获值与 rag（过程生命周期对象，docs/24 H6），VM 中途销毁
// 不会悬垂。
void AgentViewModel::importNextBatchFile() {
    if (!m_batchRunning) return;

    if (m_batchCancelRequested) {
        for (const QString& path : std::as_const(m_batchQueue)) {
            m_batchResults.append(BatchFileResult{
                path, QFileInfo(path).fileName(),
                QStringLiteral("cancelled"), QString()}.toMap());
        }
        m_batchQueue.clear();
        finishBatchImport();
        return;
    }
    if (m_batchQueue.isEmpty()) {
        finishBatchImport();
        return;
    }

    const QString path = m_batchQueue.takeFirst();
    m_batchCurrent = QFileInfo(path).fileName();
    emit batchImportProgressChanged();

    auto* watcher = new QFutureWatcher<BatchFileResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this,
            [this, watcher, path]() {
        const BatchFileResult result = watcher->result();
        watcher->deleteLater();
        m_batchResults.append(result.toMap());
        if (result.status == QLatin1String("failed")) ++m_batchFailedCount;
        m_batchCurrent.clear();
        emit batchImportProgressChanged();
        importNextBatchFile();
    });
    watcher->setFuture(QtConcurrent::run([rag = m_rag, path]() {
        return readAndIngestFile(path, rag);
    }));
}

void AgentViewModel::startBatchQueue(const QStringList& paths) {
    m_batchQueue = paths;
    // total 覆盖"本轮已保留的结果 + 待处理文件"，done = 结果数；重试时
    // 分母因此与结果列表保持自洽（不会出现 done > total）。
    m_batchTotal = m_batchResults.size() + paths.size();
    m_batchFailedCount = 0;
    m_batchCancelRequested = false;
    m_batchRunning = true;
    m_batchCurrent.clear();
    setKnowledgeBusy(true);
    Activity::usage(Activity::Kind::User, QStringLiteral("doc.import.batch"),
                    QString::number(paths.size()));
    emit batchImportProgressChanged();
    importNextBatchFile();
}

void AgentViewModel::cancelBatchImport() {
    // 立即置位并广播，让 UI 能显示"正在取消"；后续文件在下一个文件完成
    // 回调里统一标记 cancelled（正在导入的文件会先跑完）。
    if (m_batchRunning && !m_batchCancelRequested) {
        m_batchCancelRequested = true;
        emit batchImportProgressChanged();
    }
}

// 仅重跑失败项：从结果中移除 failed 条目重新入队（成功/降级/重复条目
// 原样保留，文档内容以文件系统当前状态为准）。
void AgentViewModel::retryFailedBatchImports() {
    // 空闲才允许重试：UI 已在 knowledgeBusy 时隐藏入口，这里再挡一次，
    // 避免编程式调用与在跑的导入/重新嵌入并发写同一批状态。
    if (m_batchRunning || m_knowledgeBusy || !m_rag) return;
    QStringList paths;
    QVariantList kept;
    for (const QVariant& entryValue : std::as_const(m_batchResults)) {
        const QVariantMap entry = entryValue.toMap();
        if (entry.value(QStringLiteral("status")).toString()
                == QLatin1String("failed"))
            paths.append(entry.value(QStringLiteral("path")).toString());
        else
            kept.append(entryValue);
    }
    if (paths.isEmpty()) return;
    m_batchResults = kept;
    startBatchQueue(paths);
}

void AgentViewModel::finishBatchImport() {
    m_batchRunning = false;
    m_batchCancelRequested = false;
    m_batchCurrent.clear();
    setKnowledgeBusy(false);
    emit batchImportProgressChanged();
}

bool AgentViewModel::batchImportRunning() const { return m_batchRunning; }
// 已完成文件数 = 结果条目数；在飞文件尚未写结果，故进度不会提前跳到 1。
int AgentViewModel::batchImportDone() const {
    return m_batchResults.size();
}
int AgentViewModel::batchImportFailedCount() const { return m_batchFailedCount; }

// FEAT-802: duplicate content is rejected up front (before the expensive
// embedding); same-name-different-content files coexist under a
// deduplicated display name. 文本路径与 PDF 提取完成后的回调都走这里。
void AgentViewModel::importExtractedText(const QFileInfo& info, const QString& content,
                                         const QStringList& pageTexts) {
    const QString hash = knowledgeContentHash(content);
    if (m_rag->containsContentHash(hash)) {
        emit noticeOccurred(tr("文档内容已存在，已跳过：%1").arg(info.fileName()));
        return;
    }
    const QString name = m_rag->uniqueDocumentName(info.fileName());
    Activity::usage(Activity::Kind::User, QStringLiteral("doc.import"), name);
    startIngestion(name, content, false, pageTexts);
}

void AgentViewModel::openKnowledgeDocument(const QString& name, int chunk) {
    if (name.isEmpty()) return;
    emit knowledgeDocumentOpenRequested(name, chunk);
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

void AgentViewModel::startIngestion(const QString& name, const QString& content,
                                    bool reembed, const QStringList& pageTexts) {
    setKnowledgeBusy(true);
    setEmbedProgress(0, 0);
    auto* watcher = new QFutureWatcher<int>(this);
    connect(watcher, &QFutureWatcherBase::finished, this,
            [this, watcher, name, content, reembed, pageTexts]() {
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
    watcher->setFuture(QtConcurrent::run([rag, name, content, pageTexts]() -> int {
        return rag ? rag->ingestDocument(name, content, pageTexts) : 0;
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

// docs/23 G7: undo a soft-deleted session — routed from the undo toast.
bool AgentViewModel::undoDeleteSession(const QString& sessionId) {
    if (sessionId.isEmpty()) return false;
    ChatSessionController* controller = controllerWithAgent();
    const bool ok = controller ? controller->restoreSession(sessionId) : false;
    if (ok)
        Activity::usage(Activity::Kind::User,
                        QStringLiteral("session.delete_undo"), sessionId);
    return ok;
}

// ── docs/23 D1 / docs/24 R4: database backup & restore ───────

namespace {
// QML file dialogs hand out file URLs; accept both forms.
QString toLocalDatabasePath(const QString& path) {
    const QUrl url(path);
    return url.isLocalFile() ? url.toLocalFile() : path;
}
} // namespace

QString AgentViewModel::backupDatabaseNow() {
    const QString dbPath = m_memory ? m_memory->databasePath() : QString();
    if (dbPath.isEmpty()) return tr("数据库未就绪");
    QDir().mkpath(SqliteMemory::defaultBackupsDir());
    const QString target = SqliteMemory::defaultBackupsDir()
        + QStringLiteral("/manual_%1.db")
              .arg(QDateTime::currentDateTime().toString(
                  QStringLiteral("yyyyMMdd_HHmmss")));
    if (!SqliteMemory::createBackupFile(dbPath, target))
        return tr("备份失败，详见日志");
    Activity::usage(Activity::Kind::User, QStringLiteral("db.backup"), target);
    return QString();   // empty = success
}

QVariantMap AgentViewModel::previewDatabaseBackup(const QString& path) {
    return SqliteMemory::inspectBackupFile(toLocalDatabasePath(path));
}

QString AgentViewModel::lastDatabaseBackupPath() const {
    const QDir dir(SqliteMemory::defaultBackupsDir());
    const QFileInfoList files = dir.entryInfoList(
        {QStringLiteral("auto_*.db"), QStringLiteral("manual_*.db")},
        QDir::Files, QDir::Time);
    return files.isEmpty() ? QString() : files.first().absoluteFilePath();
}

// docs/24 R4: the restore orchestration. Synchronous on the GUI thread —
// an explicit user-confirmed action; the confirm dialog announces that all
// tabs close and running tasks stop.
QString AgentViewModel::restoreDatabaseFrom(const QString& path) {
    const QString local = toLocalDatabasePath(path);
    if (!m_memory || m_memory->databasePath().isEmpty())
        return tr("数据库未就绪");

    // 0. The candidate must pass integrity before anything is touched.
    const QVariantMap info = SqliteMemory::inspectBackupFile(local);
    if (!info.value("error").toString().isEmpty())
        return tr("无法读取该备份文件：%1").arg(info.value("error").toString());
    if (!info.value("integrityOk").toBool())
        return tr("备份完整性检查未通过，已取消恢复");

    // 1. Zero per-tab connections, flushed synchronously (deferred deletes
    //    deliver here; the destroyed hooks clean the wiring maps).
    closeAllTabs();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    // 2. Invalidate in-flight knowledge-restore workers before the swap.
    if (m_rag)
        m_rag->bumpRestoreGeneration();

    // 3. Swap the file (any failure leaves the original in place and the
    //    connection usable — docs/24 R1). The captured usageMemory /
    //    modelConfigurator raw pointers survive: restore reopens in place.
    if (!m_memory->restoreFromBackup(local))
        return tr("恢复失败，原数据库已还原");

    // 4. Re-run the knowledge restore against the swapped file (wiring
    //    layer owns the glue; direct connection runs in-stack).
    emit databaseRestored();

    // 5. Back to the single-tab steady state — openTab's signal burst
    //    re-syncs the whole UI from the restored database.
    openTab();
    Activity::usage(Activity::Kind::User, QStringLiteral("db.restore"), local);
    return QString();   // empty = success
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

// usage_log 活动时间线：usage events 的 JSON 字段名即 QML 契约
// （timestamp/category/action/target/requestId/detail —— 完整契约见
// IMemory::loadUsageEvents），此处只做 QJsonArray → QVariantList 的容器
// 转换与字段透传，不做重命名（区别于 episodes 的 snake_case → camelCase
// 映射）。UsageStatsPage.qml 的 delegate 按 modelData.* 直接消费这些
// 字段名，签名与字段名不可偏离。
QVariantList usageEventsToVariantList(const QJsonArray& arr) {
    QVariantList out;
    out.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        out.append(QVariantMap{
            {QStringLiteral("timestamp"), o.value(QStringLiteral("timestamp")).toVariant()},
            {QStringLiteral("category"), o.value(QStringLiteral("category")).toString()},
            {QStringLiteral("action"), o.value(QStringLiteral("action")).toString()},
            {QStringLiteral("target"), o.value(QStringLiteral("target")).toString()},
            {QStringLiteral("requestId"), o.value(QStringLiteral("requestId")).toVariant()},
            {QStringLiteral("detail"), o.value(QStringLiteral("detail")).toString()},
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
    if (ok) {
        Activity::usage(Activity::Kind::User,
                        QStringLiteral("memory.episodic.delete"),
                        QString::number(id));
        emit episodicChanged();
    }
    return ok;
}

int AgentViewModel::clearEpisodic() {
    if (!m_memory) return 0;
    const int removed = m_memory->deleteAllEpisodic();
    if (removed > 0) {
        Activity::usage(Activity::Kind::User,
                        QStringLiteral("memory.episodic.clear"),
                        QString::number(removed));
        emit episodicChanged();
    }
    return removed;
}

// usage_log 活动时间线：设置中心「用量统计」页“活动记录”区块的数据源。
// 本 VM 持有 IMemory*，数据经 IMemory::loadUsageEvents 虚方法接线
// （SqliteMemory override）；纯只读透传，不产生状态变化与信号。
QVariantList AgentViewModel::listUsageEvents(int limit) const {
    if (!m_memory) return {};
    return usageEventsToVariantList(m_memory->loadUsageEvents(limit));
}

QVariantMap AgentViewModel::tokenStats(int recentDays) {
    return m_memory ? m_memory->tokenStats(recentDays) : QVariantMap{};
}

// docs/16 §8: forward to IMemory (SqliteMemory) so QML can call
// AgentVM.tokenStatsDetailed / costStats / quotaStatus directly.
// Batch 6 T1: fromTs/toTs 范围语义（[fromTs, toTs)，均 0 回退
// recentDays）见 IMemory.h。
QVariantMap AgentViewModel::tokenStatsDetailed(int recentDays, qint64 fromTs, qint64 toTs) {
    return m_memory ? m_memory->tokenStatsDetailed(recentDays, fromTs, toTs)
                    : QVariantMap{};
}

QVariantList AgentViewModel::tokenStatsBySource(int recentDays, qint64 fromTs, qint64 toTs) {
    return m_memory ? m_memory->tokenStatsBySource(recentDays, fromTs, toTs)
                    : QVariantList{};
}

QVariantMap AgentViewModel::costStats(int recentDays, qint64 fromTs, qint64 toTs) {
    return m_memory ? m_memory->costStats(recentDays, fromTs, toTs)
                    : QVariantMap{};
}

QVariantMap AgentViewModel::quotaStatus() {
    return m_memory ? m_memory->quotaStatus() : QVariantMap{};
}

// Batch 6 T1: token_usage 明细分页透传（{rows, total}，timestamp DESC）。
QVariantMap AgentViewModel::tokenUsageRecords(qint64 fromTs, qint64 toTs, int limit, int offset) {
    return m_memory ? m_memory->tokenUsageRecords(fromTs, toTs, limit, offset)
                    : QVariantMap{};
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

bool AgentViewModel::startTask(const QString& input, const QStringList& images,
                               const QVariantList& files) {
    configureModel(active());
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
    return active()->startTask(input, compressed, fileArray);
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
    for (qsizetype i = 0; i < m_confirmations.size(); ++i) {
        if (m_confirmations.at(i).id != id) continue;
        const auto request = m_confirmations.takeAt(i);
        emit pendingConfirmationsChanged();
        if (request.controller && request.controller->agent()
                && request.controller->agent()->pendingConfirmationId() == id)
            request.controller->approveToolCall(id, approved);
        return;
    }
}

void AgentViewModel::pruneConfirmations() {
    const auto removed = m_confirmations.removeIf([](const Confirmation& request) {
        return !request.controller || !request.controller->agent()
            || request.controller->state() != AgentState::AwaitingToolConfirm
            || request.controller->agent()->pendingConfirmationId() != request.id;
    });
    if (removed) emit pendingConfirmationsChanged();
}

QVariantMap AgentViewModel::pendingConfirmation() const {
    if (m_confirmations.isEmpty()) return {};
    const auto& request = m_confirmations.first();
    if (!request.controller) return {};
    const QString sessionId = request.controller->currentSessionId();
    QString title = tr("新会话");
    for (const auto& entry : m_sessionList) {
        const auto session = entry.toMap();
        if (session.value("id").toString() == sessionId) {
            title = session.value("title").toString();
            break;
        }
    }
    const QVariantMap result = {{"id", request.id}, {"name", request.name}, {"arguments", request.arguments},
            {"preview", request.preview},
            {"sessionId", sessionId}, {"sessionTitle", title}, {"count", m_confirmations.size()}};
    return result;
}

void AgentViewModel::stop() {
    active()->stop();
}

// docs/25 A4：停全部标签的任务。与「关 tab 停任务」同一封口语义（agent 封口
// 未答复的工具调用），托盘退出路径据此不再依赖析构顺序的被动兜底。
void AgentViewModel::stopAllTabs() {
    for (ChatSessionController* controller : std::as_const(m_tabs))
        controller->stop();
}

// docs/20 §3.1（FORK-701）：重新生成最后一条回答（活动标签）。busy 时由
// Controller 拒绝；agent 缺失时不执行。
void AgentViewModel::regenerate() {
    configureModel(active());
    active()->regenerate();
}

// docs/21 分叉为新会话：一条回答结束后，把**包括这条回答在内**的历史整体复制成一
// 个新会话，切过去让用户接着问。源会话一字不动，所以可以无限次分叉、也永远回得来。
//
// 顺序很重要：先 fork（纯持久层动作，失败就到此为止，界面不动），再刷新会话列表让
// 它出现在侧栏，最后 loadSession 切过去。当前标签忙时 loadSession 会自己开新标签承
// 载，所以分叉不会打断正在生成的回答。
bool AgentViewModel::branchFromAnswer(int traceIndex) {
    if (!m_memory) return false;
    ChatSessionController* source = active();
    AIAgent* sourceAgent = source ? source->agent() : nullptr;
    const QString sourceSession = sourceAgent ? sourceAgent->currentSessionId() : QString();
    if (sourceSession.isEmpty()) {
        emit errorOccurred(tr("这个会话还没有可分叉的历史记录。"));
        return false;
    }
    const int rank = source->rankOfTypeAt(traceIndex, QStringLiteral("summary"));
    if (rank <= 0) {
        emit errorOccurred(tr("这条回答还没有存进会话记录，稍等一下再试。"));
        return false;
    }

    QString sourceTitle;
    for (const QVariant& entry : std::as_const(m_sessionList)) {
        const QVariantMap session = entry.toMap();
        if (session.value(QStringLiteral("id")).toString() == sourceSession) {
            sourceTitle = session.value(QStringLiteral("title")).toString();
            break;
        }
    }
    if (sourceTitle.isEmpty()) sourceTitle = tr("新会话");

    // 同一条对话可以分好几次，只写"（分支）"会在侧栏里撞车，重名就顺延编号。
    QSet<QString> takenTitles;
    for (const QVariant& entry : std::as_const(m_sessionList))
        takenTitles.insert(entry.toMap().value(QStringLiteral("title")).toString());
    QString title = tr("%1（分支）").arg(sourceTitle);
    for (int n = 2; takenTitles.contains(title); ++n)
        title = tr("%1（分支%2）").arg(sourceTitle).arg(n);

    const QString forked = m_memory->forkSessionAtAnswer(sourceSession, rank, title);
    if (forked.isEmpty()) {
        emit errorOccurred(tr("无法创建分叉出来的会话，请重试。"));
        return false;
    }
    refreshSessions();
    loadSession(forked);
    emit sessionListChanged();
    return true;
}

// The agent replaces one complete turn, including live and persisted state.
bool AgentViewModel::editAndResend(const QString& newText) {
    configureModel(active());
    const QString trimmed = newText.trimmed();
    if (trimmed.isEmpty()) return false;
    ChatSessionController* controller = active();
    if (controller->isBusy()) return false;
    AIAgent* agent = controller->agent();
    return agent && agent->editLastUserMessage(trimmed);
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
    persistSettingsValue(QStringLiteral("ui/panelVisible"), visible);
    emit panelVisibleChanged();
}

void AgentViewModel::togglePanel() {
    setIsPanelVisible(!m_isPanelVisible);
}

void AgentViewModel::setPanelWidth(int width) {
    width = SettingsSchema::clampInt(QStringLiteral("ui/panelWidth"), width);
    if (m_panelWidth == width) return;
    m_panelWidth = width;
    emit panelWidthChanged();
    m_uiPersistTimer.start();
}

void AgentViewModel::setIsSidebarExpanded(bool expanded) {
    if (m_isSidebarExpanded == expanded) return;
    m_isSidebarExpanded = expanded;
    persistSettingsValue(QStringLiteral("ui/sidebarExpanded"), expanded);
    emit sidebarExpandedChanged();
}

void AgentViewModel::toggleSidebar() {
    setIsSidebarExpanded(!m_isSidebarExpanded);
}

void AgentViewModel::setSidebarWidth(int width) {
    width = SettingsSchema::clampInt(QStringLiteral("ui/sidebarWidth"), width);
    if (m_sidebarWidth == width) return;
    m_sidebarWidth = width;
    emit sidebarWidthChanged();
    m_uiPersistTimer.start();
}

// ── Layout defaults ──────────────────────────────────────

void AgentViewModel::setDefaultSidebarWidth(int width) {
    width = SettingsSchema::clampInt(QStringLiteral("prefs/v1/ui.defaultSidebarWidth"), width);
    if (m_defaultSidebarWidth == width) return;
    m_defaultSidebarWidth = width;
    PreferenceStore::instance()->save(QStringLiteral("ui.defaultSidebarWidth"), width);
    emit defaultSidebarWidthChanged();
}

void AgentViewModel::setDefaultPanelWidth(int width) {
    width = SettingsSchema::clampInt(QStringLiteral("prefs/v1/ui.defaultPanelWidth"), width);
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
    if (!SettingsRepository::contains(QStringLiteral("ui/windowX"))) return {};

    const double x = SettingsRepository::value(QStringLiteral("ui/windowX")).toDouble();
    const double y = SettingsRepository::value(QStringLiteral("ui/windowY")).toDouble();

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
    geo.insert(QStringLiteral("width"), SettingsRepository::value(QStringLiteral("ui/windowWidth")).toDouble());
    geo.insert(QStringLiteral("height"), SettingsRepository::value(QStringLiteral("ui/windowHeight")).toDouble());
    geo.insert(QStringLiteral("maximized"), SettingsRepository::value(
        QStringLiteral("ui/windowMaximized"), false).toBool());
    return geo;
}

void AgentViewModel::saveWindowGeometry(double x, double y, double width, double height, bool maximized) {
    persistSettingsBatch({
        {QStringLiteral("ui/windowX"), x},
        {QStringLiteral("ui/windowY"), y},
        {QStringLiteral("ui/windowWidth"), width},
        {QStringLiteral("ui/windowHeight"), height},
        {QStringLiteral("ui/windowMaximized"), maximized}});
}

} // namespace NexusAI
