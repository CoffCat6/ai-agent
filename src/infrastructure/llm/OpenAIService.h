#pragma once

#include "LLMServiceBase.h"

#include <QString>
#include <QVector>

namespace NexusAI {

// OpenAI-compatible chat completions service with SSE streaming.
// Works with any endpoint implementing the /chat/completions contract
// (OpenAI, DeepSeek, Moonshot, LocalAI, llama.cpp server, ...).
//
// Content deltas are forwarded via streamingChunk(); the fully assembled
// response (content + tool calls) is emitted once via responseReady().
// Request lifecycle / streaming accumulation / finished-reply fallback
// ladder live in LLMServiceBase.
class OpenAIService : public LLMServiceBase {
    Q_OBJECT
public:
    explicit OpenAIService(QObject* parent = nullptr);

    void chatComplete(const QJsonArray& messages, const QJsonArray& tools = {}) override;

    // Pure parser over a non-streamed response body (no network state);
    // exposed for unit tests.
    static LLMResponse parseResponse(const QJsonObject& json);

protected:
    void processLine(const QByteArray& line) override;
    bool streamComplete() const override;
    LLMResponse streamResult() const override;
    QString providerLabel() const override { return QStringLiteral("API"); }
    QString extractApiError(const QJsonObject& json) const override;
    bool isCompleteBody(const QJsonObject& json) const override;
    LLMResponse parseBody(const QJsonObject& json) const override
    { return parseResponse(json); }

private:
    // Tool call fragments accumulated across SSE deltas
    struct StreamToolCall {
        QString id;
        QString name;
        QString arguments; // accumulated JSON string fragments
    };

    QJsonObject buildRequestBody(const QJsonArray& messages, const QJsonArray& tools);

    QVector<StreamToolCall> m_toolCalls;
};

} // namespace NexusAI
