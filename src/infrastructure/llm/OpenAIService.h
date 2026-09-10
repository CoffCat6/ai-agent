#pragma once

#include "LLMServiceBase.h"

#include <QString>
#include <QVariant>
#include <QVector>

namespace NexusAI {

// OpenAI-compatible chat completions service with SSE streaming.
// Works with any endpoint implementing the /chat/completions contract
// (OpenAI, DeepSeek, Moonshot, LocalAI, llama.cpp server, ...).
//
// Content deltas are forwarded via streamingChunk(); the fully assembled
// response (content + tool calls) is emitted once via responseReady().
// All request/stream state lives in the per-request LLMRequestContext; the
// service object itself only carries configuration.
class OpenAIService : public LLMServiceBase {
    Q_OBJECT
public:
    explicit OpenAIService(QObject* parent = nullptr);

    void chatComplete(const QJsonArray& messages, const QJsonArray& tools = {}) override;

    // Batch 15 (FEAT-1502): opt-in sampling params from the active profile.
    // Invalid/empty variants mean "not set" - the field is omitted from the
    // request JSON (strict proxies reject empty fields).
    void setSamplingParams(const QVariant& temperature, const QVariant& maxOutputTokens);

    // Pure parser over a non-streamed response body (no network state);
    // exposed for unit tests.
    static LLMResponse parseResponse(const QJsonObject& json);

    // Streaming tool-call accumulation helpers (pure, exposed for tests).
    // OpenAI sends the tool name once (complete) and arguments as split
    // delta fragments, but some OpenAI-compatible proxies RESEND the
    // complete name/arguments on every chunk - naive concatenation then
    // yields "search_knowledge_basesearch_knowledge_base..." names and
    // welded-together argument objects that never parse.
    static QString accumulateName(const QString& acc, const QString& incoming);
    static QString accumulateArguments(const QString& acc, const QString& incoming);

protected:
    // Tool call fragments accumulated across SSE deltas of one request
    struct StreamToolCall {
        QString id;
        QString name;
        QString arguments; // accumulated JSON string fragments
    };
    struct OpenAIRequestState : ProviderRequestState {
        QVector<StreamToolCall> toolCalls;
    };

    ProviderRequestStatePtr createRequestState() const override
    { return ProviderRequestStatePtr(new OpenAIRequestState()); }
    void processLine(LLMRequestContext& ctx, const QByteArray& line) override;
    bool streamComplete(const LLMRequestContext& ctx) const override;
    LLMResponse streamResult(const LLMRequestContext& ctx) const override;
    QString providerLabel() const override { return QStringLiteral("API"); }
    QString extractApiError(const LLMRequestContext& ctx,
                            const QJsonObject& json) const override;
    bool isCompleteBody(const QJsonObject& json) const override;
    LLMResponse parseBody(const QJsonObject& json) const override
    { return parseResponse(json); }

private:
    QJsonObject buildRequestBody(const QJsonArray& messages, const QJsonArray& tools);

    // Batch 15: unset = invalid QVariant → field omitted from the request.
    QVariant m_temperature;
    QVariant m_maxOutputTokens;
};

} // namespace NexusAI
