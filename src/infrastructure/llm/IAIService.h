#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include "core/tools/ITool.h"

namespace NexusAI {

// LLM response structure
struct LLMResponse {
    QString content;
    QVector<ToolCallInfo> toolCalls;
    int tokensUsed{0};
    bool success{false};
    QString error;
};

// Abstract AI service interface for LLM providers
class IAIService : public QObject {
    Q_OBJECT
public:
    explicit IAIService(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~IAIService() = default;

    // Send chat completion request with optional tool definitions
    virtual void chatComplete(const QJsonArray& messages,
                              const QJsonArray& tools = {}) = 0;
    virtual void cancel() = 0;

    // Configuration
    virtual void setApiKey(const QString& key) = 0;
    virtual void setModel(const QString& model) = 0;
    virtual void setBaseUrl(const QString& url) = 0;

signals:
    void responseReady(const NexusAI::LLMResponse& response);
    void streamingChunk(const QString& chunk);
    void errorOccurred(const QString& error);
};

} // namespace NexusAI

Q_DECLARE_METATYPE(NexusAI::LLMResponse)
