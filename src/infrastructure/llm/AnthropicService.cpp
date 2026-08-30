#include "AnthropicService.h"

#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>

namespace NexusAI {

AnthropicService::AnthropicService(QObject* parent)
    : IAIService(parent), m_nam(new QNetworkAccessManager(this)) {}

AnthropicService::~AnthropicService() {
    if (m_currentReply) {
        m_currentReply->abort();
    }
}

AnthropicService::ConvertedHistory
AnthropicService::convertMessages(const QJsonArray& messages) {
    ConvertedHistory out;
    for (const auto& entry : messages) {
        const QJsonObject msg = entry.toObject();
        const QString role = msg.value("role").toString();
        const QString content = msg.value("content").toString();

        if (role == "system") {
            if (!content.isEmpty()) {
                if (!out.system.isEmpty()) out.system += QStringLiteral("\n\n");
                out.system += content;
            }
            continue;
        }
        if (role != "user" && role != "assistant") continue; // tool turns not representable
        if (content.isEmpty()) continue;                     // tool-call-only assistant turns

        // The Messages API expects alternating roles; merge run-ons.
        if (!out.messages.isEmpty()
                && out.messages.last().toObject().value("role").toString() == role) {
            QJsonObject last = out.messages.last().toObject();
            last["content"] = last.value("content").toString()
                + QStringLiteral("\n\n") + content;
            out.messages.replace(out.messages.size() - 1, last);
        } else {
            QJsonObject m;
            m["role"] = role;
            m["content"] = content;
            out.messages.append(m);
        }
    }

    // The conversation must open with a user turn
    while (!out.messages.isEmpty()
           && out.messages.first().toObject().value("role").toString() != "user") {
        out.messages.removeAt(0);
    }
    return out;
}

LLMResponse AnthropicService::parseResponse(const QJsonObject& json) {
    LLMResponse response;

    if (json.value("type").toString() == QLatin1String("error")) {
        QString msg = json.value("error").toObject().value("message").toString();
        if (msg.isEmpty()) msg = QStringLiteral("Anthropic API error");
        response.success = false;
        response.error = msg;
        return response;
    }

    QString text;
    const QJsonArray content = json.value("content").toArray();
    for (const auto& block : content) {
        const QJsonObject b = block.toObject();
        if (b.value("type").toString() == QLatin1String("text"))
            text += b.value("text").toString();
    }
    response.content = text;
    response.success = true;

    const QJsonObject usage = json.value("usage").toObject();
    response.tokensUsed = usage.value("input_tokens").toInt()
                        + usage.value("output_tokens").toInt();
    return response;
}

void AnthropicService::resetStreamState() {
    m_sseBuffer.clear();
    m_rawBody.clear();
    m_content.clear();
    m_tokensUsed = 0;
    m_doneReceived = false;
    m_responseEmitted = false;
    m_errorEmitted = false;
}

void AnthropicService::chatComplete(const QJsonArray& messages, const QJsonArray& tools) {
    Q_UNUSED(tools) // plain conversation: tools are not exposed to Claude (MVP scope)

    if (m_apiKey.isEmpty()) {
        emit errorOccurred("API key not set. Configure it in settings or via the "
                           "ANTHROPIC_API_KEY environment variable.");
        return;
    }

    resetStreamState();

    const ConvertedHistory history = convertMessages(messages);
    if (history.messages.isEmpty()) {
        emit errorOccurred("No convertible messages for the Anthropic request");
        return;
    }

    QJsonObject body;
    body["model"] = m_model;
    body["max_tokens"] = m_maxTokens;
    body["stream"] = true;
    body["messages"] = history.messages;
    if (!history.system.isEmpty())
        body["system"] = history.system;

    QNetworkRequest request;
    request.setUrl(QUrl(m_baseUrl + "/v1/messages"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("x-api-key", m_apiKey.toUtf8());
    request.setRawHeader("anthropic-version", "2023-06-01");
    request.setRawHeader("Accept", "text/event-stream");
    request.setTransferTimeout(120000);

    QNetworkReply* reply = m_nam->post(request, QJsonDocument(body).toJson());
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

void AnthropicService::onReadyRead() {
    const QByteArray data = m_currentReply->readAll();
    m_rawBody.append(data);
    m_sseBuffer.append(data);
    processSSEBuffer();
}

void AnthropicService::processSSEBuffer() {
    int newlineIdx;
    while ((newlineIdx = m_sseBuffer.indexOf('\n')) >= 0) {
        QByteArray line = m_sseBuffer.left(newlineIdx);
        m_sseBuffer.remove(0, newlineIdx + 1);
        line = line.trimmed();

        // Only "data:" events carry payload; "event:" lines are redundant here
        if (!line.startsWith("data:")) continue;

        const QString payload = QString::fromUtf8(line.mid(5).trimmed());
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(payload.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) continue;

        const QJsonObject json = doc.object();
        const QString type = json.value("type").toString();

        if (type == QLatin1String("error")) {
            const QString msg = json.value("error").toObject().value("message").toString();
            m_errorEmitted = true;
            emit errorOccurred(QStringLiteral("Anthropic error: %1").arg(msg));
            continue;
        }
        if (type == QLatin1String("message_start")) {
            // input tokens arrive on the opening event
            m_tokensUsed += json.value("message").toObject().value("usage")
                                .toObject().value("input_tokens").toInt();
            continue;
        }
        if (type == QLatin1String("content_block_delta")) {
            const QJsonObject delta = json.value("delta").toObject();
            if (delta.value("type").toString() == QLatin1String("text_delta")) {
                const QString text = delta.value("text").toString();
                if (!text.isEmpty()) {
                    m_content += text;
                    emit streamingChunk(text);
                }
            }
            continue;
        }
        if (type == QLatin1String("message_delta")) {
            m_tokensUsed += json.value("usage").toObject().value("output_tokens").toInt();
            continue;
        }
        if (type == QLatin1String("message_stop")) {
            m_doneReceived = true;
        }
    }
}

void AnthropicService::emitStreamedResponse() {
    LLMResponse response;
    response.success = true;
    response.content = m_content;
    response.tokensUsed = m_tokensUsed;
    m_responseEmitted = true;
    emit responseReady(response);
}

void AnthropicService::handleReplyFinished(QNetworkReply* reply) {
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

    if (m_doneReceived || !m_content.isEmpty()) {
        emitStreamedResponse();
        return;
    }

    // No stream data: error body, plain non-stream JSON, or network failure
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(m_rawBody, &parseError);
    if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
        const QJsonObject json = doc.object();
        if (json.value("type").toString() == QLatin1String("error")
                || json.contains("error")) {
            emit errorOccurred(QStringLiteral("Anthropic error (HTTP %1): %2")
                .arg(status).arg(parseResponse(json).error));
            return;
        }
        if (err == QNetworkReply::NoError && json.contains("content")) {
            // Server ignored the stream flag - treat as regular response
            LLMResponse response = parseResponse(json);
            if (response.success) {
                m_responseEmitted = true;
                emit responseReady(response);
            } else {
                emit errorOccurred(response.error.isEmpty()
                    ? QStringLiteral("Empty response from Anthropic") : response.error);
            }
            return;
        }
    }

    if (err != QNetworkReply::NoError) {
        emit errorOccurred(QStringLiteral("Network error: %1").arg(errString));
        return;
    }

    emit errorOccurred("Empty or invalid response from Anthropic");
}

void AnthropicService::cancel() {
    if (m_currentReply) {
        m_currentReply->abort(); // finished handler performs cleanup
    }
}

} // namespace NexusAI
