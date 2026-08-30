#pragma once

#include "IAIService.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>

namespace NexusAI {

// Anthropic Claude Messages API service (plain conversation, per MVP scope:
// tools are intentionally not exposed to Claude yet).
//
// Converts the agent's OpenAI-format history into Messages API form: role
// "system" messages are folded into the top-level "system" param, tool
// traffic is dropped, consecutive same-role messages are merged and leading
// assistant turns are removed (the API requires alternating roles that open
// with a user turn).
//
// Streamed text deltas are forwarded via streamingChunk(); the assembled
// response is emitted once via responseReady().
class AnthropicService : public IAIService {
    Q_OBJECT
public:
    explicit AnthropicService(QObject* parent = nullptr);
    ~AnthropicService() override;

    void chatComplete(const QJsonArray& messages, const QJsonArray& tools = {}) override;
    void cancel() override;

    void setApiKey(const QString& key) override { m_apiKey = key; }
    void setModel(const QString& model) override { m_model = model; }
    void setBaseUrl(const QString& url) override { m_baseUrl = url; }

    // Pure conversion / parsing helpers (no network state); exposed for tests.
    struct ConvertedHistory {
        QString system;
        QJsonArray messages;
    };
    static ConvertedHistory convertMessages(const QJsonArray& messages);
    static LLMResponse parseResponse(const QJsonObject& json);

private:
    void onReadyRead();
    void handleReplyFinished(QNetworkReply* reply);
    void processSSEBuffer();
    void emitStreamedResponse();
    void resetStreamState();

    QString m_apiKey;
    QString m_model{"claude-sonnet-4-5"};
    QString m_baseUrl{"https://api.anthropic.com"};
    int m_maxTokens{4096}; // Messages API requires an explicit output cap
    QNetworkAccessManager* m_nam;
    QNetworkReply* m_currentReply{nullptr};

    // Streaming state
    QByteArray m_sseBuffer;
    QByteArray m_rawBody;
    QString m_content;
    int m_tokensUsed{0};
    bool m_doneReceived{false};
    bool m_responseEmitted{false};
    bool m_errorEmitted{false};
};

} // namespace NexusAI
