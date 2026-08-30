#pragma once

#include "IAIService.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>
#include <QVector>

namespace NexusAI {

// Ollama native /api/chat service with NDJSON streaming.
// Converts the agent's OpenAI-format message history into Ollama's format.
// Content chunks are forwarded via streamingChunk(); the assembled response
// (content + tool calls) is emitted once via responseReady().
class OllamaService : public IAIService {
    Q_OBJECT
public:
    explicit OllamaService(QObject* parent = nullptr);
    ~OllamaService() override;

    void chatComplete(const QJsonArray& messages, const QJsonArray& tools = {}) override;
    void cancel() override;

    void setApiKey(const QString& key) override { Q_UNUSED(key) } // Ollama doesn't need an API key
    void setModel(const QString& model) override { m_model = model; }
    void setBaseUrl(const QString& url) override { m_baseUrl = url; }

    // Pure parsers over response payloads (no network state); exposed for
    // unit tests.
    static LLMResponse parseResponse(const QJsonObject& json);
    static QVector<ToolCallInfo> parseToolCalls(const QJsonObject& message);

private:
    void onReadyRead();
    void handleReplyFinished(QNetworkReply* reply);
    void processLineBuffer();
    void emitStreamedResponse();
    void resetStreamState();

    static QJsonArray convertMessagesForOllama(const QJsonArray& messages);

    QString m_model{"qwen2.5:7b"};
    QString m_baseUrl{"http://localhost:11434"};
    QNetworkAccessManager* m_nam;
    QNetworkReply* m_currentReply{nullptr};

    // Streaming state
    QByteArray m_lineBuffer; // trailing incomplete line
    QByteArray m_rawBody;    // full raw response (for error diagnostics)
    QString m_content;
    QVector<ToolCallInfo> m_toolCalls;
    int m_tokensUsed{0};
    bool m_doneReceived{false};
    bool m_responseEmitted{false};
    bool m_errorEmitted{false};

    QJsonObject buildRequestBody(const QJsonArray& messages, const QJsonArray& tools);
};

} // namespace NexusAI
