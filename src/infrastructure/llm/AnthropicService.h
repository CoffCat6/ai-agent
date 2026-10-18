#pragma once

#include "LLMServiceBase.h"

#include <QString>
#include <QVariant>
#include <QVector>

namespace NexusAI {

// Anthropic Claude Messages API service with full tool-use support.
//
// Converts between the agent's OpenAI wire format and the Messages API:
// - request "tools": {type,function:{name,description,parameters}} ->
//   {name,description,input_schema}
// - history: assistant tool_calls -> assistant content blocks
//   {type:"tool_use",id,name,input}; role:"tool" results -> a user message
//   with {type:"tool_result",tool_use_id,content} blocks (a run of tool
//   results merges into one user turn, as the API requires)
// - response: content blocks {type:"tool_use",...} -> ToolCallInfo; the
//   agent keeps using the OpenAI wire format unchanged
//
// Role "system" messages are folded into the top-level "system" param and
// the conversation is normalized to alternating user/assistant turns that
// open with a user turn.
//
// Streamed text deltas are forwarded via streamingChunk(); the assembled
// response (content + tool calls) is emitted once via responseReady().
// All request/stream state lives in the per-request LLMRequestContext; the
// service object itself only carries configuration.
class AnthropicService : public LLMServiceBase {
    Q_OBJECT
public:
    explicit AnthropicService(QObject* parent = nullptr);

    void chatComplete(const QJsonArray& messages, const QJsonArray& tools = {}) override;

    // Batch 15 (FEAT-1502): opt-in sampling params from the active profile.
    // max_tokens is REQUIRED by the Messages API: an unset override keeps
    // the historical 4096 fallback. temperature is omitted when unset.
    void setSamplingParams(const QVariant& temperature, const QVariant& maxOutputTokens);

    // 思考强度档位("low"|"medium"|"high";"off"/未设置 = 不启用)。Anthropic 的
    // 扩展思考用 budget_tokens 表达强度,而预算是从 max_tokens 那一池里扣的,
    // 所以档位要按当前输出上限折算。
    void setReasoningEffort(const QVariant& effort);

    // 档位 -> budget_tokens,0 表示不启用思考。纯函数,供测试直接断言:
    // API 要求 1024 <= budget_tokens < max_tokens,折算不下时返回 0
    // (宁可退回无思考,也不能让请求被 400 拒掉)。
    static int thinkingBudgetTokens(const QString& level, int maxTokens);

    // Pure conversion / parsing helpers (no network state); exposed for tests.
    struct ConvertedHistory {
        QString system;
        QJsonArray messages;
    };
    static ConvertedHistory convertMessages(const QJsonArray& messages);
    static LLMResponse parseResponse(const QJsonObject& json);

protected:
    // Tool_use content block accumulated from input_json_delta fragments
    // of one request
    struct StreamToolUse {
        QString id;
        QString name;
        QString json;
    };
    struct AnthropicRequestState : ProviderRequestState {
        QVector<StreamToolUse> toolUses;
        // docs/16 §5.1: usage split captured from message_start (input +
        // cache) and message_delta (output), mapped in streamResult.
        int promptTokens{0};
        int completionTokens{0};
        int cacheReadTokens{0};
        int cacheCreationTokens{0};
        // m-3: a retried attempt must not re-emit tool_use blocks accumulated
        // by the failed first attempt (token counts are overwritten on the
        // retry's message_start/message_delta anyway).
        void reset() override {
            toolUses.clear();
            promptTokens = 0;
            completionTokens = 0;
            cacheReadTokens = 0;
            cacheCreationTokens = 0;
        }
    };

    ProviderRequestStatePtr createRequestState() const override
    { return ProviderRequestStatePtr(new AnthropicRequestState()); }
    void processLine(LLMRequestContext& ctx, const QByteArray& line) override;
    bool streamComplete(const LLMRequestContext& ctx) const override;
    LLMResponse streamResult(const LLMRequestContext& ctx) const override;
    QString providerLabel() const override { return QStringLiteral("Anthropic"); }
    QString extractApiError(const LLMRequestContext& ctx,
                            const QJsonObject& json) const override;
    bool isCompleteBody(const QJsonObject& json) const override;
    LLMResponse parseBody(const QJsonObject& json) const override
    { return parseResponse(json); }

    // Messages API 用 max_tokens 而非上下文长度，故只读输出上限与思考档位。
    void applyProfileParams(const LlmProfileConfig& cfg) override
    {
        setSamplingParams(cfg.temperature, cfg.maxOutputTokens);
        setReasoningEffort(cfg.reasoningEffort);
    }

private:
    static QJsonArray convertTools(const QJsonArray& tools);

    int m_maxTokens{4096}; // Messages API requires an explicit output cap
    QVariant m_temperature; // Batch 15: unset → field omitted
    QVariant m_reasoningEffort; // 思考强度档位;unset = 不发 thinking 字段
};

} // namespace NexusAI
