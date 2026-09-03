#pragma once

#include "LLMServiceBase.h"

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
// response is emitted once via responseReady(). Request lifecycle /
// streaming accumulation / finished-reply fallback ladder live in
// LLMServiceBase.
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
    int m_maxTokens{4096}; // Messages API requires an explicit output cap
};

} // namespace NexusAI
