#include "AnthropicService.h"

#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>

namespace NexusAI {

AnthropicService::AnthropicService(QObject* parent)
    : LLMServiceBase(parent)
{
    m_model = QStringLiteral("claude-sonnet-4-5");
    m_baseUrl = QStringLiteral("https://api.anthropic.com");
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

    startReply(request, QJsonDocument(body).toJson());
}

bool AnthropicService::streamComplete() const {
    return m_doneReceived || !m_content.isEmpty();
}

LLMResponse AnthropicService::streamResult() const {
    LLMResponse response;
    response.success = true;
    response.content = m_content;
    response.tokensUsed = m_tokensUsed;
    return response;
}

QString AnthropicService::extractApiError(const QJsonObject& json) const {
    if (json.value("type").toString() != QLatin1String("error")
            && !json.contains("error"))
        return {};
    return parseResponse(json).error;
}

bool AnthropicService::isCompleteBody(const QJsonObject& json) const {
    return json.contains("content");
}

void AnthropicService::processLine(const QByteArray& line) {
    // Only "data:" events carry payload; "event:" lines are redundant here
    if (!line.startsWith("data:")) return;

    const QString payload = QString::fromUtf8(line.mid(5).trimmed());
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payload.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) return;

    const QJsonObject json = doc.object();
    const QString type = json.value("type").toString();

    if (type == QLatin1String("error")) {
        const QString msg = json.value("error").toObject().value("message").toString();
        m_errorEmitted = true;
        emit errorOccurred(QStringLiteral("Anthropic error: %1").arg(msg));
        return;
    }
    if (type == QLatin1String("message_start")) {
        // input tokens arrive on the opening event
        m_tokensUsed += json.value("message").toObject().value("usage")
                            .toObject().value("input_tokens").toInt();
        return;
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
        return;
    }
    if (type == QLatin1String("message_delta")) {
        m_tokensUsed += json.value("usage").toObject().value("output_tokens").toInt();
        return;
    }
    if (type == QLatin1String("message_stop")) {
        m_doneReceived = true;
    }
}

} // namespace NexusAI
