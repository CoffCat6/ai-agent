#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVariant>

#include "core/agent/LLMTypes.h"

namespace NexusAI {

// 当前活动 model profile 的归一化配置快照，由装配层（main.cpp）从 profile map
// 组装，再交给 LLMServiceBase::configure。这里刻意只放"已清洗过的值"：字段
// 缺失/清空就是无效 QVariant，语义是"请求体里不发该字段"，各协议自己决定
// 消费哪几个（输出上限 / 上下文长度 / 思考档位在不同协议里不是同一个东西）。
struct LlmProfileConfig {
    QString baseUrl;
    QString apiKey;
    QString model;
    QVariant temperature;
    QVariant maxOutputTokens;   // openai / anthropic 的输出上限
    QVariant numCtx;            // ollama 的上下文长度
    QVariant reasoningEffort;   // 思考强度档位
};

// Abstract AI service interface for LLM providers
class IAIService : public QObject {
    Q_OBJECT
public:
    explicit IAIService(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~IAIService() = default;

    // Send chat completion request with optional tool definitions.
    // Messages use the OpenAI wire format (assistant tool_calls carry
    // JSON-string arguments; tool results use role "tool" + tool_call_id).
    // Provider-specific conversion happens inside each implementation.
    virtual void chatComplete(const QJsonArray& messages,
                              const QJsonArray& tools = {}) = 0;
    virtual void cancel() = 0;

    // Configuration
    virtual void setApiKey(const QString& key) = 0;
    virtual void setModel(const QString& model) = 0;
    virtual void setBaseUrl(const QString& url) = 0;

    // Apply the active profile in one shot. Default no-op: test doubles and
    // any service that takes no profile configuration stay as they are.
    virtual void configure(const LlmProfileConfig&) {}

    // Identity of the configured model (observability: audit rows carry it;
    // empty for fakes that don't track a model).
    virtual QString model() const { return {}; }

signals:
    // Emitted when a request is dispatched; responses echo the id so the
    // agent can drop replies from superseded requests.
    void requestStarted(quint64 requestId);
    void responseReady(const NexusAI::LLMResponse& response);
    void streamingChunk(const QString& chunk);
    // Chain-of-thought deltas on a channel of their own: reusing
    // streamingChunk would land the model's private reasoning inside the
    // answer node the agent is still building.
    void reasoningChunk(const QString& chunk);
    void errorOccurred(const QString& error);
};

} // namespace NexusAI
