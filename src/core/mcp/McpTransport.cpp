#include "core/mcp/McpTransport.h"

#include <QJsonDocument>
#include <QJsonParseError>

namespace NexusAI {

McpTransport::McpTransport(QObject* parent) : QObject(parent) {}

void McpTransport::setState(NexusAI::McpTransport::State state) {
    if (m_state == state) return;
    m_state = state;
    emit stateChanged(m_state);
}

void McpTransport::dispatchPayload(const QByteArray& payload) {
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        emit errorOccurred(QStringLiteral("Malformed MCP message: ")
            + error.errorString());
        return;
    }
    emit messageReceived(doc.object());
}

} // namespace NexusAI
