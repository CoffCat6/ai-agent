#pragma once

#include "IAIService.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>

namespace NexusAI {

// Ollama local API service
class OllamaService : public IAIService {
    Q_OBJECT
public:
    explicit OllamaService(QObject* parent = nullptr);
    ~OllamaService() override;

    void chatComplete(const QJsonArray& messages, const QJsonArray& tools = {}) override;
    void cancel() override;

    void setApiKey(const QString& key) override { Q_UNUSED(key) } // Ollama doesn't need API key
    void setModel(const QString& model) override { m_model = model; }
    void setBaseUrl(const QString& url) override { m_baseUrl = url; }

private slots:
    void onStreamData(const QByteArray& data);

private:
    QString m_model{"llama3"};
    QString m_baseUrl{"http://localhost:11434"};
    QNetworkAccessManager* m_nam;
    QNetworkReply* m_currentReply{nullptr};
    QString m_streamBuffer;

    LLMResponse parseResponse(const QJsonObject& json);
    QJsonObject buildRequestBody(const QJsonArray& messages, const QJsonArray& tools);
};

} // namespace NexusAI
