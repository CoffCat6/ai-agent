#include "OllamaService.h"

#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QByteArray>
#include <QDebug>

namespace NexusAI {

OllamaService::OllamaService(QObject* parent)
    : IAIService(parent), m_nam(new QNetworkAccessManager(this)) {}

OllamaService::~OllamaService() {
    if (m_currentReply) {
        m_currentReply->abort();
    }
}

QJsonObject OllamaService::buildRequestBody(const QJsonArray& messages, const QJsonArray& tools) {
    QJsonObject body;
    body["model"] = m_model;
    body["messages"] = messages;
    body["stream"] = false;

    // Ollama supports tools in a slightly different format
    if (!tools.isEmpty()) {
        QJsonArray ollamaTools;
        for (const auto& tool : tools) {
            QJsonObject toolObj = tool.toObject();
            if (toolObj.contains("function")) {
                ollamaTools.append(toolObj.value("function"));
            } else {
                ollamaTools.append(toolObj);
            }
        }
        body["tools"] = ollamaTools;
    }

    return body;
}

void OllamaService::chatComplete(const QJsonArray& messages, const QJsonArray& tools) {
    QJsonObject body = buildRequestBody(messages, tools);
    QJsonDocument doc(body);

    QNetworkRequest request;
    request.setUrl(QUrl(m_baseUrl + "/api/chat"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    m_streamBuffer.clear();

    m_currentReply = m_nam->post(request, doc.toJson());

    connect(m_currentReply, &QNetworkReply::readyRead, this, [this]() {
        QByteArray data = m_currentReply->readAll();
        onStreamData(data);
    });

    connect(m_currentReply, &QNetworkReply::finished, this, [this]() {
        if (m_currentReply->error() != QNetworkReply::NoError) {
            emit errorOccurred(m_currentReply->errorString());
        }
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    });
}

void OllamaService::onStreamData(const QByteArray& data) {
    // Ollama returns NDJSON (newline-delimited JSON)
    m_streamBuffer.append(data);

    // Process complete lines
    QStringList lines = m_streamBuffer.split('\n');
    if (lines.isEmpty()) return;

    // Keep the last (possibly incomplete) line in buffer
    m_streamBuffer = lines.takeLast();

    QString fullContent;
    QVector<ToolCallInfo> toolCalls;
    bool success = false;

    for (const QString& rawLine : lines) {
        QString line = rawLine.trimmed();
        if (line.isEmpty()) continue;

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError) continue;

        QJsonObject json = doc.object();

        // Check if this is the final response
        if (json.value("done").toBool()) {
            success = true;
            QJsonObject message = json.value("message").toObject();
            fullContent = message.value("content").toString();

            // Parse tool calls
            if (message.contains("tool_calls")) {
                QJsonArray tcArray = message.value("tool_calls").toArray();
                for (const auto& tc : tcArray) {
                    QJsonObject tcObj = tc.toObject();
                    ToolCallInfo info;
                    info.name = tcObj.value("name").toString();
                    info.arguments = tcObj.value("arguments").toObject();
                    info.id = QString::number(toolCalls.size()); // Simple ID
                    toolCalls.append(info);
                }
            }
        } else {
            // Streaming chunk
            QJsonObject message = json.value("message").toObject();
            QString chunk = message.value("content").toString();
            if (!chunk.isEmpty()) {
                emit streamingChunk(chunk);
            }
        }
    }

    // If we got a complete response, emit it
    if (success) {
        LLMResponse response;
        response.content = fullContent;
        response.toolCalls = toolCalls;
        response.success = true;
        emit responseReady(response);
    }
}

LLMResponse OllamaService::parseResponse(const QJsonObject& json) {
    LLMResponse response;

    if (json.contains("error")) {
        response.success = false;
        response.error = json.value("error").toString();
        return response;
    }

    QJsonObject message = json.value("message").toObject();
    response.content = message.value("content").toString();
    response.success = true;

    if (message.contains("tool_calls")) {
        QJsonArray toolCalls = message.value("tool_calls").toArray();
        for (const auto& tc : toolCalls) {
            QJsonObject tcObj = tc.toObject();
            ToolCallInfo info;
            info.name = tcObj.value("name").toString();
            info.arguments = tcObj.value("arguments").toObject();
            info.id = QString::number(response.toolCalls.size());
            response.toolCalls.append(info);
        }
    }

    return response;
}

void OllamaService::cancel() {
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
    m_streamBuffer.clear();
}

} // namespace NexusAI
