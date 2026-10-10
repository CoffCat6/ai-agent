#include "viewmodels/ChatSessionController.h"

#include "core/agent/AIAgent.h"
#include "core/memory/IMemory.h"
#include "core/utils/Activity.h"

namespace NexusAI {

ChatSessionController::ChatSessionController(QObject* parent)
    : QObject(parent) {}

ChatSessionController::~ChatSessionController() {
    if (m_agent && m_agent->isActive())
        m_agent->stop();
    // Factory-built agents belong to this controller; the shared template
    // agent (AgentViewModel::setAgent) is owned by the caller.
    if (m_agent && m_ownedAgent)
        m_agent->deleteLater();
}

void ChatSessionController::attachAgent(AIAgent* agent, bool owned) {
    if (m_agent == agent && m_ownedAgent == owned) return;
    // Replacing or detaching an active agent terminates its task cleanly.
    if (m_agent && m_agent->isActive())
        m_agent->stop();
    if (m_agent) {
        disconnect(m_agent, nullptr, this, nullptr);
        if (m_ownedAgent)
            m_agent->deleteLater();
    }
    m_agent = agent;
    m_ownedAgent = owned;
    if (m_agent) {
        connectAgent(m_agent);
    } else {
        // Detached state is idle; the caller must attach a configured agent
        // before accepting tasks.
        setState(AgentState::Idle);
        m_trace.clearNodes();
        emit traceChanged();
        m_currentSessionId.clear();
        emit currentSessionIdChanged();
    }
    if (m_agent) {
        // The controller mirror is authoritative for UX continuity; push it
        // into the newly attached agent.
        m_agent->setPermissionMode(static_cast<PermissionMode>(m_permissionMode));
    }
}

void ChatSessionController::setPermissionMode(int mode) {
    if (mode < 0 || mode > 2 || mode == m_permissionMode) return;
    m_permissionMode = mode;
    m_permissionModeOverridden = true;
    if (m_agent)
        m_agent->setPermissionMode(static_cast<PermissionMode>(mode));
    emit permissionModeChanged();
}

void ChatSessionController::applyDefaultPermissionMode(int mode) {
    if (mode < 0 || mode > 2 || m_permissionModeOverridden
            || mode == m_permissionMode)
        return;
    m_permissionMode = mode;
    if (m_agent)
        m_agent->setPermissionMode(static_cast<PermissionMode>(mode));
    emit permissionModeChanged();
}

void ChatSessionController::connectAgent(AIAgent* agent) {
    connect(agent, &AIAgent::stateChanged, this, [this](AgentState state) {
        setState(state);
    });

    connect(agent, &AIAgent::traceChanged, this, [this, agent]() {
        m_trace.applyDelta(agent->traceModel().toVariantList());
        emit traceChanged();
    });

    connect(agent, &AIAgent::responseReady, this, [this](const QString& response) {
        m_finalResponse = response;
        emit responseReady();
    });

    connect(agent, &AIAgent::toolConfirmRequired, this,
            [this](const QString& id, const QString& name, const QJsonObject& args) {
        emit toolConfirmRequired(id, name, args.toVariantMap());
    });

    connect(agent, &AIAgent::errorOccurred, this, &ChatSessionController::errorOccurred);

    connect(agent, &AIAgent::tokensChanged, this, [this](int usedTokens) {
        m_usedTokens = usedTokens;
        emit tokenUsageChanged();
    });

    // docs/16 §8.2: 会话累计 token（跨任务累加，区别于窗口占用）。
    connect(agent, &AIAgent::sessionTokensChanged, this, [this](qint64 tokens) {
        m_sessionTokensUsed = tokens;
        emit sessionTokensChanged();
    });

    connect(agent, &AIAgent::contextLimitChanged, this, [this]() {
        emit contextLimitChanged();
        emit tokenUsageChanged();
    });

    // docs/16 §6/§7: forward cost/quota signals to the UI.
    connect(agent, &AIAgent::costChanged, this, &ChatSessionController::costChanged);
    connect(agent, &AIAgent::quotaWarning, this, &ChatSessionController::quotaWarning);

    // Session history is global (same SQLite file for every tab); the facade
    // refreshes the shared list whenever any tab reports a change.
    connect(agent, &AIAgent::sessionsChanged, this,
            &ChatSessionController::sessionHistoryChanged);

    connect(agent, &AIAgent::sessionChanged, this, [this](const QString& sessionId) {
        if (m_currentSessionId != sessionId) {
            m_currentSessionId = sessionId;
            emit currentSessionIdChanged();
        }
        emit sessionHistoryChanged();
    });

    // Initial sync with the agent's state.
    m_state = agent->state();
    emit stateChanged();
    m_trace.applyDelta(agent->traceModel().toVariantList());
    emit traceChanged();
    m_finalResponse = agent->lastResponse();
    emit responseReady();
    // 与 AIAgent::notifyTokens / contextComposition 同口径（含 tools schema），
    // 保证初始同步值也和后续 tokensChanged 一致。
    m_usedTokens = agent->contextComposition().value("totalTokens").toInt();
    emit tokenUsageChanged();
    m_currentSessionId = agent->currentSessionId();
    emit currentSessionIdChanged();
    emit sessionHistoryChanged();
}

QString ChatSessionController::stateText() const {
    switch (m_state) {
    case AgentState::Idle:               return "Ready";
    case AgentState::Thinking:           return "Thinking...";
    case AgentState::AwaitingToolConfirm:return "Awaiting Confirmation";
    case AgentState::ExecutingTool:      return "Executing Tool";
    case AgentState::Summarizing:        return "Summarizing";
    case AgentState::Completed:          return "Completed";
    case AgentState::Error:              return "Error";
    }
    return "Unknown";
}

bool ChatSessionController::isBusy() const {
    return m_state == AgentState::Thinking
        || m_state == AgentState::ExecutingTool
        || m_state == AgentState::AwaitingToolConfirm
        || m_state == AgentState::Summarizing;
}

int ChatSessionController::maxTokens() const {
    return m_agent ? m_agent->contextLimit() : AIAgent::kDefaultContextTokens;
}

void ChatSessionController::setDraftText(const QString& text) {
    if (m_draftText == text) return;
    m_draftText = text;
    emit draftTextChanged();
}

void ChatSessionController::setDraftAttachments(const QVariantList& attachments) {
    if (m_draftAttachments == attachments) return;
    m_draftAttachments = attachments;
    emit draftAttachmentsChanged();
}

// docs/20 §3.3：输入历史回溯。游标从 size() 开始（未浏览）；↑ 前移一位并
// 返回该条目，已到最旧时保持在最旧（返回首条，幂等）；无历史返回空串。
// ↓ 后移，回到 size() 时返回空串（表示回到最新/草稿态）。
QString ChatSessionController::historyPrev() {
    if (m_inputHistory.isEmpty()) return QString();
    if (m_historyCursor > m_inputHistory.size())
        m_historyCursor = m_inputHistory.size();
    if (m_historyCursor > 0)
        --m_historyCursor;
    return m_inputHistory.at(m_historyCursor);
}

QString ChatSessionController::historyNext() {
    if (m_inputHistory.isEmpty()) return QString();
    if (m_historyCursor >= m_inputHistory.size()) return QString();   // 已最新
    ++m_historyCursor;
    if (m_historyCursor >= m_inputHistory.size()) return QString();   // 回到草稿态
    return m_inputHistory.at(m_historyCursor);
}

void ChatSessionController::resetHistoryCursor() {
    m_historyCursor = m_inputHistory.size();
}

void ChatSessionController::setState(AgentState state) {
    if (m_state == state) return;
    m_state = state;
    emit stateChanged();
}


// ── QML actions ──────────────────────────────────────────

bool ChatSessionController::startTask(const QString& input, const QStringList& images,
                                      const QJsonArray& files) {
    if (isBusy()) return false;
    // docs/20 §3.3：成功发起的非空输入入历史环（相邻重复不入队），并复位
    // 浏览游标。仅本标签内存态，不持久化。
    const QString trimmedInput = input.trimmed();
    if (!trimmedInput.isEmpty()
            && (m_inputHistory.isEmpty() || m_inputHistory.last() != trimmedInput)) {
        m_inputHistory.append(trimmedInput);
        if (m_inputHistory.size() > kMaxHistory)
            m_inputHistory.removeFirst();
    }
    m_historyCursor = m_inputHistory.size();
    Activity::usage(Activity::Kind::User, QStringLiteral("task.start"),
                    input.left(80));
    if (!m_agent) {
        emit errorOccurred(tr("No model agent is configured. Configure a model and try again."));
        return false;
    }

    m_finalResponse.clear();
    emit responseReady();

    return m_agent->startTask(input, images, files);
}

void ChatSessionController::approveToolCall(const QString& id, bool approved) {
    Activity::usage(Activity::Kind::User,
                    approved ? QStringLiteral("tool.approve")
                             : QStringLiteral("tool.decline"),
                    id);
    if (m_agent) m_agent->approveToolCall(id, approved);
}

void ChatSessionController::stop() {
    Activity::usage(Activity::Kind::User, QStringLiteral("task.stop"));
    if (m_agent) m_agent->stop();
}

// docs/20 §3.1（FORK-701）：重新生成。busy 时拒绝（与 stop 的前置守卫
// 一致），有 agent 时转发给真实管线。
void ChatSessionController::regenerate() {
    if (isBusy()) return;
    Activity::usage(Activity::Kind::User, QStringLiteral("task.regenerate"));

    if (m_agent) m_agent->regenerateLast();
}

void ChatSessionController::newChat() {
    Activity::usage(Activity::Kind::User, QStringLiteral("session.new"));

    m_finalResponse.clear();
    emit responseReady();

    if (m_agent) {
        m_agent->resetSession();
        return;
    }

    setState(AgentState::Idle);
    m_trace.clearNodes();
    emit traceChanged();
    m_currentSessionId.clear();
    emit currentSessionIdChanged();
}

// docs/18: 归属按标签持有——直接读本标签的 agent，不再经过全局状态。
QString ChatSessionController::activeProjectId() const {
    return m_agent ? m_agent->activeProjectId() : QString();
}

QString ChatSessionController::activeProjectRoot() const {
    return m_agent ? m_agent->activeProjectRoot() : QString();
}

void ChatSessionController::loadSession(const QString& sessionId) {
    if (!m_agent || isBusy()) return;
    Activity::usage(Activity::Kind::User, QStringLiteral("session.resume"),
                    sessionId);
    m_agent->resumeSession(sessionId);
    m_finalResponse = m_agent->lastResponse();
    emit responseReady();
}

void ChatSessionController::deleteSession(const QString& sessionId) {
    Activity::usage(Activity::Kind::User, QStringLiteral("session.delete"),
                    sessionId);
    if (m_agent) m_agent->deleteSession(sessionId);
}

void ChatSessionController::renameSession(const QString& sessionId, const QString& title) {
    Activity::usage(Activity::Kind::User, QStringLiteral("session.rename"),
                    sessionId);
    if (m_agent) m_agent->renameSession(sessionId, title);
}

QVariantList ChatSessionController::auditLog(int limit) const {
    return m_agent ? m_agent->auditLog(limit).toVariantList() : QVariantList{};
}

// docs/16 §8.3: live per-tab context composition. Empty when no agent is
// attached yet (fresh tab before first task) - the UI shows the empty state.
QVariantMap ChatSessionController::contextComposition() const {
    return m_agent ? m_agent->contextComposition() : QVariantMap{};
}

// docs/16 §8: QML-facing wrappers. Delegate to the agent's IMemory backend
// (SqliteMemory) which implements the aggregate queries. Return empty when
// no agent is attached yet so the UI falls back to zeros.
QVariantMap ChatSessionController::tokenStatsDetailed(int recentDays) const {
    if (!m_agent || !m_agent->memory()) return {};
    return m_agent->memory()->tokenStatsDetailed(recentDays);
}

QVariantList ChatSessionController::tokenStatsBySource(int recentDays) const {
    if (!m_agent || !m_agent->memory()) return {};
    return m_agent->memory()->tokenStatsBySource(recentDays);
}

QVariantMap ChatSessionController::costStats(int recentDays) const {
    if (!m_agent || !m_agent->memory()) return {};
    return m_agent->memory()->costStats(recentDays);
}

QVariantMap ChatSessionController::quotaStatus() const {
    if (!m_agent || !m_agent->memory()) return {};
    return m_agent->memory()->quotaStatus();
}


} // namespace NexusAI
