#pragma once

#include "core/mcp/McpTransport.h"

#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

namespace NexusAI {

// stdio transport: spawns the MCP server as a child process and exchanges
// newline-delimited JSON-RPC messages over stdin/stdout (MCP 2025-03-26
// stdio transport). stderr is captured as diagnostics only.
//
// Crash handling: an abnormal child exit schedules up to MaxRestarts
// reconnects with exponential backoff (1s/2s/4s); the client re-runs the
// initialize handshake on top of the new process. A clean stop() (user
// action / shutdown) never restarts.
class StdioTransport : public McpTransport {
    Q_OBJECT

public:
    struct Config {
        QString command;
        QStringList args;
        QVariantMap env;   // extra environment variables
    };

    explicit StdioTransport(const Config& config, QObject* parent = nullptr);
    ~StdioTransport() override;

    void start() override;
    void stop() override;
    void sendMessage(const QJsonObject& message) override;

private:
    void spawnProcess();
    void onReadyRead();
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void scheduleRestart();
    void appendStderr(const QByteArray& chunk);
    // Append the retained stderr tail to a transport error so users can see
    // *why* a server exited (e.g. missing API key, package install failure)
    // without having to rerun the command by hand.
    QString formatError(const QString& reason) const;

    Config m_config;
    QProcess m_process;
    QByteArray m_lineBuffer;
    QByteArray m_stderrBuffer;
    QTimer m_restartTimer;
    int m_restarts{0};
    bool m_stopping{false};

    static constexpr int MaxRestarts = 3;
};

} // namespace NexusAI
