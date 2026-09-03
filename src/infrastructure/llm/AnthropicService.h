#pragma once

#include "LLMServiceBase.h"

#include <QString>
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
// Request lifecycle / streaming accumulation / finished-reply fallback
// ladder live in LLMServiceBase.
class AnthropicService : public LLMServiceBase {
    Q_OBJECT
public:
    explicit AnthropicService(QObject* parent = nullptr);

    void chatComplete(const QJsonArray& messages, const QJsonArray& tools = {}) override;

    // Pure conversion / parsing helpers (no network state); exposed for tests.
    struct ConvertedHistory {
        QString system;
        QJsonArray messages;
    };
    static ConvertedHistory convertMessages(const QJsonArray& messages);
    static LLMResponse parseResponse(const QJsonObject& json);

protected:
    void processLine(const QByteArray& line) override;
    bool streamComplete() const override;
    LLMResponse streamResult() const override;
    QString providerLabel() const override { return QStringLiteral("Anthropic"); }
    QString extractApiError(const QJsonObject& json) const override;
    bool isCompleteBody(const QJsonObject& json) const override;
    LLMResponse parseBody(const QJsonObject& json) const override
    { return parseResponse(json); }

private:
    // Tool_use content block accumulated from input_json_delta fragments
    struct StreamToolUse {
        QString id;
        QString name;
        QString json;
    };

    static QJsonArray convertTools(const QJsonArray& tools);

    int m_maxTokens{4096}; // Messages API requires an explicit output cap
    QVector<StreamToolUse> m_toolUses;
};

} // namespace NexusAI
