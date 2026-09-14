#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

namespace NexusAI {

// Transport abstraction for the MCP client (Batch 31a, F9): a bidirectional
// JSON-RPC 2.0 message pipe to one MCP server. Concrete transports:
// StdioTransport (child process, newline-delimited JSON over stdin/stdout)
// now; HttpTransport (Streamable HTTP) in 31b.
//
// Threading: transports live on the GUI thread (owned via QObject parentage
// by McpClient); McpToolAdapter::execute runs on a pool thread and reaches
// the client only through QMetaObject::invokeMethod(QueuedConnection).
class McpTransport : public QObject {
    Q_OBJECT

public:
    enum class State { Disconnected, Connecting, Connected, Failed };
    Q_ENUM(State)

    explicit McpTransport(QObject* parent = nullptr);
    ~McpTransport() override = default;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void sendMessage(const QJsonObject& message) = 0;

    State state() const { return m_state; }

signals:
    void messageReceived(const QJsonObject& message);
    void stateChanged(NexusAI::McpTransport::State state);
    void errorOccurred(const QString& error);

protected:
    void setState(State state);
    // Frame one raw protocol line/message into a parsed object; malformed
    // input is reported through errorOccurred and dropped.
    void dispatchPayload(const QByteArray& payload);

    State m_state{State::Disconnected};
};

} // namespace NexusAI
