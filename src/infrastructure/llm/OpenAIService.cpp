#include "OpenAIService.h"

#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QByteArray>
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
    body["stream"] = true; // SSE streaming

    if (!tools.isEmpty()) {
        body["tools"] = tools;
        body["tool_choice"] = "auto";
    }

    return body;
}

void OpenAIService::resetStreamState() {
    m_sseBuffer.clear();
    m_rawBody.clear();
    m_content.clear();
    m_toolCalls.clear();
    m_doneReceived = false;
    m_responseEmitted = false;
    m_errorEmitted = false;
}

void OpenAIService::chatComplete(const QJsonArray& messages, const QJsonArray& tools) {
    // Local / self-hosted OpenAI-compatible endpoints (Ollama, LM Studio,
    // llama.cpp) do not require an API key.
    const bool isLocal = m_baseUrl.contains("localhost", Qt::CaseInsensitive)
                      || m_baseUrl.contains("127.0.0.1");
    if (m_apiKey.isEmpty() && !isLocal) {
        emit errorOccurred("API key not set. Configure it via the OPENAI_API_KEY "
                           "environment variable or application settings.");
        return;
    }

    resetStreamState();

    QNetworkRequest request;
    request.setUrl(QUrl(m_baseUrl + "/chat/completions"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());
    request.setRawHeader("Accept", "text/event-stream");
    request.setTransferTimeout(120000); // 120s hard cap per request

    QNetworkReply* reply = m_nam->post(request,
        QJsonDocument(buildRequestBody(messages, tools)).toJson());
    m_currentReply = reply;

    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        if (m_currentReply != reply) return;
        onReadyRead();
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (m_currentReply == reply) m_currentReply = nullptr;
        handleReplyFinished(reply);
    });
}

void OpenAIService::onReadyRead() {
    const QByteArray data = m_currentReply->readAll();
    m_rawBody.append(data);
    m_sseBuffer.append(data);
    processSSEBuffer();
}

void OpenAIService::processSSEBuffer() {
    int newlineIdx;
    while ((newlineIdx = m_sseBuffer.indexOf('\n')) >= 0) {
        QByteArray line = m_sseBuffer.left(newlineIdx);
        m_sseBuffer.remove(0, newlineIdx + 1);
        line = line.trimmed();

        // SSE spec: only "data:" events carry payload
        if (!line.startsWith("data:")) continue;

        const QString payload = QString::fromUtf8(line.mid(5).trimmed());
        if (payload == QLatin1String("[DONE]")) {
            m_doneReceived = true;
            continue;
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(payload.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) continue;

        const QJsonObject json = doc.object();

        // Some providers stream error objects mid-flight
        if (json.contains("error")) {
            const QString msg = json.value("error").toObject().value("message").toString();
            m_errorEmitted = true;
            emit errorOccurred(QStringLiteral("API error: %1").arg(msg));
            continue;
        }

        const QJsonObject delta = json.value("choices").toArray().at(0)
                                      .toObject().value("delta").toObject();

        // Content delta
        const QString content = delta.value("content").toString();
        if (!content.isEmpty()) {
            m_content += content;
            emit streamingChunk(content);
        }

        // Tool call fragments: {index, id?, function: {name?, arguments?}}
        const QJsonArray toolCalls = delta.value("tool_calls").toArray();
        for (const auto& tc : toolCalls) {
            const QJsonObject tcObj = tc.toObject();
            const int index = tcObj.value("index").toInt(-1);
            if (index < 0) continue;
            while (m_toolCalls.size() <= index)
                m_toolCalls.append(StreamToolCall{});

            if (tcObj.contains("id"))
                m_toolCalls[index].id = tcObj.value("id").toString();
            const QJsonObject fn = tcObj.value("function").toObject();
            if (fn.contains("name"))
                m_toolCalls[index].name += fn.value("name").toString();
            if (fn.contains("arguments"))
                m_toolCalls[index].arguments += fn.value("arguments").toString();
        }
    }
}

void OpenAIService::emitStreamedResponse() {
    LLMResponse response;
    response.success = true;
    response.content = m_content;
    for (const auto& tc : m_toolCalls) {
        if (tc.name.isEmpty()) continue;
        ToolCallInfo info;
        info.id = tc.id;
        info.name = tc.name;
        info.arguments = QJsonDocument::fromJson(tc.arguments.toUtf8()).object();
        response.toolCalls.append(info);
    }
    // Token usage is not reported on streaming responses (would require
    // stream_options.include_usage); the agent estimates the context size.
    m_responseEmitted = true;
    emit responseReady(response);
}

void OpenAIService::handleReplyFinished(QNetworkReply* reply) {
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError err = reply->error();
    const QString errString = reply->errorString();

    const QByteArray rest = reply->readAll();
    reply->deleteLater();

    if (!rest.isEmpty()) {
        m_rawBody.append(rest);
        m_sseBuffer.append(rest);
        processSSEBuffer();
    }

    if (m_responseEmitted || m_errorEmitted) return;

    // Assemble the streamed response when data was received
    if (m_doneReceived || !m_content.isEmpty() || !m_toolCalls.isEmpty()) {
        emitStreamedResponse();
        return;
    }

    // No stream data: error body, plain non-stream JSON, or network failure
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(m_rawBody, &parseError);
    if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
        const QJsonObject json = doc.object();
        if (json.contains("error")) {
            const QJsonObject errObj = json.value("error").toObject();
            QString msg = errObj.value("message").toString();
            if (msg.isEmpty()) msg = QString::fromUtf8(m_rawBody);
            emit errorOccurred(QStringLiteral("API error (HTTP %1): %2").arg(status).arg(msg));
            return;
        }
        if (err == QNetworkReply::NoError && json.contains("choices")) {
            // Server ignored the stream flag - treat as regular response
            LLMResponse response = parseResponse(json);
            if (response.success) {
                m_responseEmitted = true;
                emit responseReady(response);
            } else {
                emit errorOccurred(response.error.isEmpty()
                    ? QStringLiteral("Empty response from API") : response.error);
            }
            return;
        }
    }

    if (err != QNetworkReply::NoError) {
        emit errorOccurred(QStringLiteral("Network error: %1").arg(errString));
        return;
    }

    emit errorOccurred("Empty or invalid response from API");
}

LLMResponse OpenAIService::parseResponse(const QJsonObject& json) {
    LLMResponse response;

    const QJsonArray choices = json.value("choices").toArray();
    if (choices.isEmpty()) {
        response.success = false;
        response.error = "No choices in API response";
        return response;
    }

    const QJsonObject message = choices.first().toObject().value("message").toObject();
    response.content = message.value("content").toString();
    response.success = true;

    const QJsonArray toolCalls = message.value("tool_calls").toArray();
    for (const auto& tc : toolCalls) {
        const QJsonObject tcObj = tc.toObject();
        const QJsonObject function = tcObj.value("function").toObject();

        ToolCallInfo info;
        info.id = tcObj.value("id").toString();
        info.name = function.value("name").toString();
        info.arguments = QJsonDocument::fromJson(
            function.value("arguments").toString().toUtf8()).object();
        if (info.name.isEmpty()) continue;
        response.toolCalls.append(info);
    }

    const QJsonObject usage = json.value("usage").toObject();
    response.tokensUsed = usage.value("total_tokens").toInt();

    return response;
}

void OpenAIService::cancel() {
    if (m_currentReply) {
        m_currentReply->abort(); // finished handler performs cleanup
    }
}

} // namespace NexusAI
