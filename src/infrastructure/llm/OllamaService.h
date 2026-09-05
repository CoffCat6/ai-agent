#pragma once

#include "LLMServiceBase.h"

#include <QString>
#include <QVector>

namespace NexusAI {

// Ollama native /api/chat service with NDJSON streaming.
// Converts the agent's OpenAI-format message history into Ollama's format.
// Content chunks are forwarded via streamingChunk(); the assembled response
// (content + tool calls) is emitted once via responseReady(). Request
// lifecycle / streaming accumulation / finished-reply fallback ladder live
// in LLMServiceBase.
class OllamaService : public LLMServiceBase {
    Q_OBJECT
public:
    explicit OllamaService(QObject* parent = nullptr);

    void chatComplete(const QJsonArray& messages, const QJsonArray& tools = {}) override;

    // Pure parsers over response payloads (no network state); exposed for
    // unit tests.
    static LLMResponse parseResponse(const QJsonObject& json);
    static QVector<ToolCallInfo> parseToolCalls(const QJsonObject& message);

protected:
    void resetStreamState() override;
    void processLine(const QByteArray& line) override;
    bool streamComplete() const override;
    LLMResponse streamResult() const override;
    QString providerLabel() const override { return QStringLiteral("Ollama"); }
    QString extractApiError(const QJsonObject& json) const override;
    bool isCompleteBody(const QJsonObject& json) const override;
    LLMResponse parseBody(const QJsonObject& json) const override
    { return parseResponse(json); }

private:
    static QJsonArray convertMessagesForOllama(const QJsonArray& messages);

    QJsonObject buildRequestBody(const QJsonArray& messages, const QJsonArray& tools);

    QVector<ToolCallInfo> m_toolCalls;
};

} // namespace NexusAI
