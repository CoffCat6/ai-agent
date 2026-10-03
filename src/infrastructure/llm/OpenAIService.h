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

    // 思考强度:归一化后的档位字符串("none"|"minimal"|"low"|"medium"|"high"
    // |"xhigh"|"max")。无效 QVariant = 未设置,请求体不出现 reasoning_effort,
    // 与 temperature 同一套"不发即默认"语义。
    void setReasoningEffort(const QVariant& effort);

    // 内部档位 -> 线上取值。内部用四档（off/low/medium/high）描述意图，OpenAI
    // 的枚举里没有 "off"（关闭是 "none"），原样发出去会被严格端点当非法取值拒
    // 掉。纯函数供测试断言；未知/空返回空串表示不发该字段。
    static QString reasoningEffortParam(const QString& level);

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

    // Batch 30 (F6): images-carrying user messages become multipart content
    // arrays; public static so the request format is directly testable.
    static QJsonArray convertMessagesForOpenAI(const QJsonArray& messages);

protected:
    // Tool call fragments accumulated across SSE deltas of one request
    struct StreamToolCall {
        QString id;
        QString name;
        QString arguments; // accumulated JSON string fragments
    };
    struct OpenAIRequestState : ProviderRequestState {
        QVector<StreamToolCall> toolCalls;
        // docs/16 §5.1: usage object from the include_usage final chunk
        // (empty "choices"), mapped onto LLMResponse in streamResult.
        QJsonObject usage;
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

    // Chat Completions 消费 profile 的输出上限与思考档位；ollama 的 numCtx 在
    // 这条协议里没有对应字段，所以不读。
    void applyProfileParams(const LlmProfileConfig& cfg) override
    {
        setSamplingParams(cfg.temperature, cfg.maxOutputTokens);
        setReasoningEffort(cfg.reasoningEffort);
    }

protected:
    // Protected (not private) so tests can assert the wire format directly,
    // mirroring convertMessagesForOpenAI.
    QJsonObject buildRequestBody(const QJsonArray& messages, const QJsonArray& tools);

    // Batch 15: unset = invalid QVariant → field omitted from the request.
    QVariant m_temperature;
    QVariant m_maxOutputTokens;
    QVariant m_reasoningEffort;
};

} // namespace NexusAI
