#pragma once

#include "core/mcp/McpTransport.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStringList>
#include <QVariantMap>

namespace NexusAI {

// Streamable HTTP transport (MCP 2025-03-26, POST subset — Batch 31b):
// every message is POSTed to the single endpoint; the response body is
// either application/json (one message) or text/event-stream (several,
// "data: <json>" lines). The server-issued Mcp-Session-Id response header
// is stored and echoed on subsequent requests.
//
// Deliberately NOT implemented (documented in docs/10 §3): the standalone
// GET SSE push channel and OAuth authorization (static extra headers only).
class HttpTransport : public McpTransport {
    Q_OBJECT

public:
    struct Config {
        QString url;
        QVariantMap headers;   // static extra headers (e.g. Authorization)
    };

    explicit HttpTransport(const Config& config, QObject* parent = nullptr);
    ~HttpTransport() override;

    void start() override;      // state → Connected; failures surface per POST
    void stop() override;
    void sendMessage(const QJsonObject& message) override;

private:
    void post(const QJsonObject& message);
    void onReplyFinished(QNetworkReply* reply, const QByteArray& buffer);
    void onSseChunk(QNetworkReply* reply, const QByteArray& chunk);
    void dispatchSseEvents(const QByteArray& payload);

    Config m_config;
    QNetworkAccessManager* m_nam;
    QString m_sessionId;
    // Per-reply SSE line buffers (a POST may return an event stream with
    // several JSON-RPC messages).
    QHash<const QNetworkReply*, QByteArray> m_sseBuffers;
};

} // namespace NexusAI
