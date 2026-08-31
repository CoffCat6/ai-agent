#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace NexusAI {

class IMemory {
public:
    virtual ~IMemory() = default;

    virtual void saveMessage(const QString& sessionId, const QJsonObject& msg) = 0;
    virtual QJsonArray loadMessages(const QString& sessionId, int limit = 50) = 0;

    virtual void saveEpisodic(const QString& sessionId, const QJsonObject& episode) = 0;
    virtual QJsonArray searchEpisodic(const QString& query, int limit = 10) = 0;

    virtual void clear(const QString& sessionId) = 0;

    // Session bookkeeping (optional capability; default no-ops)
    virtual void beginSession(const QString& sessionId, const QString& title) {
        Q_UNUSED(sessionId); Q_UNUSED(title);
    }
    virtual void touchSession(const QString& sessionId) {
        Q_UNUSED(sessionId);
    }
    virtual QJsonArray listSessions(int limit = 50) {
        Q_UNUSED(limit);
        return {};
    }
    // Remove a session together with its messages and episodes
    virtual void deleteSession(const QString& sessionId) {
        Q_UNUSED(sessionId);
    }
    virtual void renameSession(const QString& sessionId, const QString& title) {
        Q_UNUSED(sessionId); Q_UNUSED(title);
    }

    // Audit trail for tool activity (optional capability; default no-ops).
    // events: tool_execute / tool_approved / tool_declined / tool_blocked /
    // unknown_tool
    virtual void logAudit(const QString& sessionId, const QString& event,
                          const QString& toolName, const QString& arguments,
                          bool success, const QString& detail) {
        Q_UNUSED(sessionId); Q_UNUSED(event); Q_UNUSED(toolName);
        Q_UNUSED(arguments); Q_UNUSED(success); Q_UNUSED(detail);
    }
    virtual QJsonArray auditLog(int limit = 100) {
        Q_UNUSED(limit);
        return {};
    }

    // Trace view persistence (optional capability; default no-ops). Trace
    // nodes rebuild the visual timeline (thinking / tool calls / results)
    // when a session is resumed - plain message history only keeps the
    // user/assistant text. `order` fixes the timeline position on first
    // insert and is ignored on upsert (status/title updates).
    virtual void saveTraceNode(const QString& sessionId, const QJsonObject& node, int order) {
        Q_UNUSED(sessionId); Q_UNUSED(node); Q_UNUSED(order);
    }
    virtual QJsonArray loadTraceNodes(const QString& sessionId) {
        Q_UNUSED(sessionId);
        return {};
    }
};

} // namespace NexusAI
