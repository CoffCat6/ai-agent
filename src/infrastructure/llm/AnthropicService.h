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

private:
    static QJsonArray convertTools(const QJsonArray& tools);

    int m_maxTokens{4096}; // Messages API requires an explicit output cap
    QVariant m_temperature; // Batch 15: unset → field omitted
};

} // namespace NexusAI
