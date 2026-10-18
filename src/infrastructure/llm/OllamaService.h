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

    // 思考强度:Ollama 只有一个顶层布尔 "think"(true 请求思考 / false 明确不要 /
    // 字段缺失=模型默认),没有强度分级,所以低/中/高三档都落到 true。
    void setReasoningEffort(const QVariant& effort);

    // 档位 -> think 字段值;invalid QVariant 表示不发该字段。纯函数供测试断言。
    static QVariant thinkFlagForLevel(const QString& level);

    // Pure parsers over response payloads (no network state); exposed for
    // unit tests.
    static LLMResponse parseResponse(const QJsonObject& json);
    static QVector<ToolCallInfo> parseToolCalls(const QJsonObject& message);

protected:
    struct OllamaRequestState : ProviderRequestState {
        QVector<ToolCallInfo> toolCalls;
        // docs/16 §5.1: eval counts from the done=true final line.
        int promptTokens{0};
        int completionTokens{0};
        // m-3: toolCalls is reassigned on done=true (so it already survives
        // a retry without leaking), but reset() keeps the contract uniform
        // across backends and protects against future accumulate-style edits.
        void reset() override { toolCalls.clear(); promptTokens = 0; completionTokens = 0; }
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

    // Ollama 的"上下文长度"就是 num_ctx，输出上限在这条协议上没有对应字段。
    void applyProfileParams(const LlmProfileConfig& cfg) override
    {
        setSamplingParams(cfg.temperature, cfg.numCtx);
        setReasoningEffort(cfg.reasoningEffort);
    }

private:
    QJsonObject buildRequestBody(const QJsonArray& messages, const QJsonArray& tools);

    // Batch 15: unset = invalid QVariant → options object omitted.
    QVariant m_temperature;
    QVariant m_numCtx;
    QVariant m_reasoningEffort; // 思考强度档位;unset = 不发 think 字段
};

} // namespace NexusAI
