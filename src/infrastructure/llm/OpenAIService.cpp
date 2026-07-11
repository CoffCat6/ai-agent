#include "OpenAIService.h"

#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QByteArray>
#include <QEventLoop>
#include <QDebug>

namespace NexusAI {

OpenAIService::OpenAIService(QObject* parent)
    : IAIService(parent), m_nam(new QNetworkAccessManager(this)) {}

OpenAIService::~OpenAIService() {
    if (m_currentReply) {
        m_currentReply->abort();
    }
}

QJsonObject OpenAIService::buildRequestBody(const QJsonArray& messages, const QJsonArray& tools) {
    QJsonObject body;
    body["model"] = m_model;
    body["messages"] = messages;
    body["stream"] = false;  // MVP: no streaming for now

    if (!tools.isEmpty()) {
        body["tools"] = tools;
        body["tool_choice"] = "auto";
    }

    return body;
}

void OpenAIService::chatComplete(const QJsonArray& messages, const QJsonArray& tools) {
    if (m_apiKey.isEmpty()) {
        emit errorOccurred("API key not set");
        return;
    }

    QJsonObject body = buildRequestBody(messages, tools);
    QJsonDocument doc(body);

    QNetworkRequest request;
    request.setUrl(QUrl(m_baseUrl + "/chat/completions"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());

    m_streamBuffer.clear();

    m_currentReply = m_nam->post(request, doc.toJson());

    connect(m_currentReply, &QNetworkReply::readyRead, this, [this]() {
        QByteArray data = m_currentReply->readAll();
        onStreamData(data);
    });

    connect(m_currentReply, &QNetworkReply::finished, this, [this]() {
        if (m_currentReply->error() != QNetworkReply::NoError) {
            emit errorOccurred(m_currentReply->errorString());
            m_currentReply->deleteLater();
            m_currentReply = nullptr;
            return;
        }
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    });
}

void OpenAIService::onStreamData(const QByteArray& data) {
    m_streamBuffer.append(data);

    // Try to parse complete JSON response
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(m_streamBuffer.toUtf8(), &parseError);

    if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
        LLMResponse response = parseResponse(doc.object());
        emit responseReady(response);
        m_streamBuffer.clear();
    }
}

void OpenAIService::onReplyReady() {
    // Not used in current implementation
}

LLMResponse OpenAIService::parseResponse(const QJsonObject& json) {
    LLMResponse response;

    if (json.contains("error")) {
        response.success = false;
        response.error = json.value("error").toObject().value("message").toString();
        return response;
    }

    QJsonObject choices = json.value("choices").toArray().first().toObject();
    QJsonObject message = choices.value("message").toObject();

    response.content = message.value("content").toString();
    response.success = true;

    // Parse tool calls
    if (message.contains("tool_calls")) {
        QJsonArray toolCalls = message.value("tool_calls").toArray();
        for (const auto& tc : toolCalls) {
            QJsonObject tcObj = tc.toObject();
            ToolCallInfo info;
            info.id = tcObj.value("id").toString();
            QJsonObject function = tcObj.value("function").toObject();
            info.name = function.value("name").toString();
            info.arguments = QJsonDocument::fromJson(
                function.value("arguments").toString().toUtf8()).object();
            response.toolCalls.append(info);
        }
    }

    // Token usage
    QJsonObject usage = json.value("usage").toObject();
    response.tokensUsed = usage.value("total_tokens").toInt();

    return response;
}

void OpenAIService::cancel() {
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
    m_streamBuffer.clear();
}

} // namespace NexusAI
