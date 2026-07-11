#pragma once

#include "IAIService.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>

namespace NexusAI {

// OpenAI-compatible API service (supports streaming SSE)
class OpenAIService : public IAIService {
    Q_OBJECT
public:
    explicit OpenAIService(QObject* parent = nullptr);
    ~OpenAIService() override;

    void chatComplete(const QJsonArray& messages, const QJsonArray& tools = {}) override;
    void cancel() override;

    void setApiKey(const QString& key) override { m_apiKey = key; }
    void setModel(const QString& model) override { m_model = model; }
    void setBaseUrl(const QString& url) override { m_baseUrl = url; }

private slots:
    void onReplyReady();
    void onStreamData(const QByteArray& data);

private:
    QString m_apiKey;
    QString m_model{"gpt-4"};
    QString m_baseUrl{"https://api.openai.com/v1"};
    QNetworkAccessManager* m_nam;
    QNetworkReply* m_currentReply{nullptr};
    QString m_streamBuffer;

    LLMResponse parseResponse(const QJsonObject& json);
    QJsonObject buildRequestBody(const QJsonArray& messages, const QJsonArray& tools);
};

} // namespace NexusAI
