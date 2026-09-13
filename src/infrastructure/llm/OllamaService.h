#pragma once

#include "LLMServiceBase.h"

#include <QString>
#include <QVariant>
#include <QVector>

namespace NexusAI {

// Ollama native /api/chat service with NDJSON streaming.
// Converts the agent's OpenAI-format message history into Ollama's format.
// Content chunks are forwarded via streamingChunk(); the assembled response
// (content + tool calls) is emitted once via responseReady().
// All request/stream state lives in the per-request LLMRequestContext; the
// service object itself only carries configuration.
class OllamaService : public LLMServiceBase {
    Q_OBJECT
public:
    // Batch 30 (F6): Ollama expects bare base64 payloads in "images";
    // public static so the conversion is directly testable.
    static QJsonArray convertMessagesForOllama(const QJsonArray& messages);

    explicit OllamaService(QObject* parent = nullptr);

    void chatComplete(const QJsonArray& messages, const QJsonArray& tools = {}) override;

    // Batch 15 (FEAT-1502): opt-in sampling params from the active profile.
    // Invalid/empty variants mean "not set" - the ollama "options" object is
    // omitted from the request JSON entirely when nothing is configured.
    void setSamplingParams(const QVariant& temperature, const QVariant& numCtx);

    // Pure parsers over response payloads (no network state); exposed for
    // unit tests.
    static LLMResponse parseResponse(const QJsonObject& json);
    static QVector<ToolCallInfo> parseToolCalls(const QJsonObject& message);

protected:
    struct OllamaRequestState : ProviderRequestState {
        QVector<ToolCallInfo> toolCalls;
    };

    ProviderRequestStatePtr createRequestState() const override
    { return ProviderRequestStatePtr(new OllamaRequestState()); }
    void processLine(LLMRequestContext& ctx, const QByteArray& line) override;
    bool streamComplete(const LLMRequestContext& ctx) const override;
    LLMResponse streamResult(const LLMRequestContext& ctx) const override;
    QString providerLabel() const override { return QStringLiteral("Ollama"); }
    QString extractApiError(const LLMRequestContext& ctx,
                            const QJsonObject& json) const override;
    bool isCompleteBody(const QJsonObject& json) const override;
    LLMResponse parseBody(const QJsonObject& json) const override
    { return parseResponse(json); }

private:
    QJsonObject buildRequestBody(const QJsonArray& messages, const QJsonArray& tools);

    // Batch 15: unset = invalid QVariant → options object omitted.
    QVariant m_temperature;
    QVariant m_numCtx;
};

} // namespace NexusAI
