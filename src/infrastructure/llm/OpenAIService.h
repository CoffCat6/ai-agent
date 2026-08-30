#pragma once

#include "IAIService.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>
#include <QVector>

namespace NexusAI {

// OpenAI-compatible chat completions service with SSE streaming.
// Works with any endpoint implementing the /chat/completions contract
// (OpenAI, DeepSeek, Moonshot, LocalAI, llama.cpp server, ...).
//
// Content deltas are forwarded via streamingChunk(); the fully assembled
// response (content + tool calls) is emitted once via responseReady().
class OpenAIService : public IAIService {
    Q_OBJECT
public:
    explicit OpenAIService(QObject* parent = nullptr);
    ~OpenAIService() override;

    void chatComplete(const QJsonArray& messages, const QJsonArray& tools = {}) override;
    void cancel() override;

    void setApiKey(const QString& key) override { m_apiKey = key; }
    void setModel(const QString& model) override { m_model = model; }
    void setBaseUrl(const QString& url) override { m_baseUrl = url; }

    // Pure parser over a non-streamed response body (no network state);
    // exposed for unit tests.
    static LLMResponse parseResponse(const QJsonObject& json);

private:
    // Tool call fragments accumulated across SSE deltas
    struct StreamToolCall {
        QString id;
        QString name;
        QString arguments; // accumulated JSON string fragments
    };

    void onReadyRead();
    void handleReplyFinished(QNetworkReply* reply);
    void processSSEBuffer();
    void emitStreamedResponse();
    void resetStreamState();

    QString m_apiKey;
    QString m_model{"gpt-4o-mini"};
    QString m_baseUrl{"https://api.openai.com/v1"};
    QNetworkAccessManager* m_nam;
    QNetworkReply* m_currentReply{nullptr};

    // Streaming state
    QByteArray m_sseBuffer;   // trailing incomplete bytes
    QByteArray m_rawBody;     // full raw response (for error diagnostics)
    QString m_content;
    QVector<StreamToolCall> m_toolCalls;
    bool m_doneReceived{false};
    bool m_responseEmitted{false};
    bool m_errorEmitted{false};

    QJsonObject buildRequestBody(const QJsonArray& messages, const QJsonArray& tools);
};

} // namespace NexusAI
