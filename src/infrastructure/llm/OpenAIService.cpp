#include "OpenAIService.h"

#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QByteArray>
#include <QDebug>

namespace NexusAI {

OpenAIService::OpenAIService(QObject* parent)
    : LLMServiceBase(parent)
{
    m_model = QStringLiteral("gpt-4o-mini");
    m_baseUrl = QStringLiteral("https://api.openai.com/v1");
}

// Batch 30 (F6): user messages carrying images become multipart content
// arrays ([text, image_url, ...]); messages without images keep the plain
// string content — strict OpenAI-compatible gateways must not see arrays
// where they expect strings. The non-standard "images" field is stripped.
QJsonArray OpenAIService::convertMessagesForOpenAI(const QJsonArray& messages) {
    QJsonArray out;
    for (const auto& entry : messages) {
        QJsonObject msg = entry.toObject();
        const QJsonArray images = msg.value("images").toArray();
        if (msg.value("role").toString() != "user" || images.isEmpty()) {
            msg.remove("images");
            out.append(msg);
            continue;
        }

        QJsonArray content;
        const QString text = msg.value("content").toString();
        if (!text.isEmpty())
            content.append(QJsonObject{{"type", "text"}, {"text", text}});
        for (const auto& image : images) {
            content.append(QJsonObject{
                {"type", "image_url"},
                {"image_url", QJsonObject{{"url", image.toString()}}}
            });
        }
        msg["content"] = content;
        msg.remove("images");
        out.append(msg);
    }
    return out;
}

QJsonObject OpenAIService::buildRequestBody(const QJsonArray& messages, const QJsonArray& tools) {
    QJsonObject body;
    body["model"] = m_model;
    body["messages"] = convertMessagesForOpenAI(messages);
    body["stream"] = true; // SSE streaming

    // Batch 15: sampling params are opt-in - absent from the JSON unless the
    // active profile sets them (strict proxies reject empty fields).
    if (m_temperature.isValid())
        body["temperature"] = m_temperature.toDouble();
    if (m_maxOutputTokens.isValid())
        body["max_tokens"] = m_maxOutputTokens.toInt();

    if (!tools.isEmpty()) {
        body["tools"] = tools;
        body["tool_choice"] = "auto";
    }

    return body;
}

void OpenAIService::setSamplingParams(const QVariant& temperature,
                                      const QVariant& maxOutputTokens) {
    m_temperature = temperature;
    m_maxOutputTokens = maxOutputTokens;
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

    QNetworkRequest request;
    request.setUrl(QUrl(m_baseUrl + "/chat/completions"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());
    request.setRawHeader("Accept", "text/event-stream");
    request.setTransferTimeout(120000); // 120s hard cap per request

    startReply(request, QJsonDocument(buildRequestBody(messages, tools)).toJson());
}

bool OpenAIService::streamComplete(const LLMRequestContext& ctx) const {
    const auto* state = static_cast<const OpenAIRequestState*>(ctx.provider.data());
    return ctx.doneReceived || !ctx.content.isEmpty() || !state->toolCalls.isEmpty();
}

LLMResponse OpenAIService::streamResult(const LLMRequestContext& ctx) const {
    const auto* state = static_cast<const OpenAIRequestState*>(ctx.provider.data());
    LLMResponse response;
    response.success = true;
    response.content = ctx.content;
    for (const auto& tc : state->toolCalls) {
        if (tc.name.isEmpty()) continue;
        ToolCallInfo info;
        info.id = tc.id;
        info.name = tc.name;
        // Malformed arguments keep the call alive with an empty object (the
        // tool rejects it and the model can retry) - but never silently.
        QJsonParseError parseError;
        const QJsonDocument argsDoc =
            QJsonDocument::fromJson(tc.arguments.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError)
            qWarning() << "Malformed tool call arguments from model:"
                       << parseError.errorString() << tc.arguments;
        info.arguments = argsDoc.object();
        response.toolCalls.append(info);
    }
    // Token usage is not reported on streaming responses (would require
    // stream_options.include_usage); the agent estimates the context size.
    return response;
}

QString OpenAIService::extractApiError(const LLMRequestContext& ctx,
                                       const QJsonObject& json) const {
    if (!json.contains("error")) return {};
    QString msg = json.value("error").toObject().value("message").toString();
    if (msg.isEmpty()) msg = QString::fromUtf8(ctx.rawBody);
    return msg;
}

bool OpenAIService::isCompleteBody(const QJsonObject& json) const {
    return json.contains("choices");
}

// Streaming accumulation tolerant of both wire styles:
// - genuine delta fragments are concatenated ("search_kn" + "owledge_base")
// - complete values resent by the proxy on every chunk are collapsed
//   ("search_knowledge_base" + "search_knowledge_base" stays one name)
QString OpenAIService::accumulateName(const QString& acc, const QString& incoming) {
    if (incoming.isEmpty()) return acc;
    if (acc.isEmpty()) return incoming;
    if (incoming.startsWith(acc)) return incoming;   // resent, grown, or identical
    if (!acc.contains(incoming)) return acc + incoming;
    return acc;                                       // repeat of what we have
}

QString OpenAIService::accumulateArguments(const QString& acc, const QString& incoming) {
    if (incoming.isEmpty()) return acc;
    if (acc.isEmpty()) return incoming;
    if (incoming.startsWith(acc)) return incoming;   // resent, grown, or identical

    // A genuine delta fragment is appended - unless both sides are already
    // complete JSON objects. Then the proxy resent diverging full payloads
    // and concatenating would weld them into unparseable garbage; the first
    // complete document wins.
    QJsonParseError accErr{}, inErr{};
    const bool accComplete = QJsonDocument::fromJson(acc.toUtf8(), &accErr).isObject()
        && accErr.error == QJsonParseError::NoError;
    const bool incomingComplete =
        QJsonDocument::fromJson(incoming.toUtf8(), &inErr).isObject()
        && inErr.error == QJsonParseError::NoError;
    if (accComplete && incomingComplete) return acc;
    return acc + incoming;
}

void OpenAIService::processLine(LLMRequestContext& ctx, const QByteArray& line) {
    auto* state = static_cast<OpenAIRequestState*>(ctx.provider.data());

    // SSE spec: only "data:" events carry payload
    if (!line.startsWith("data:")) return;

    const QString payload = QString::fromUtf8(line.mid(5).trimmed());
    if (payload == QLatin1String("[DONE]")) {
        ctx.doneReceived = true;
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payload.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) return;

    const QJsonObject json = doc.object();

    // Some providers stream error objects mid-flight
    if (json.contains("error")) {
        const QString msg = json.value("error").toObject().value("message").toString();
        ctx.errorEmitted = true;
        emit errorOccurred(QStringLiteral("API error: %1").arg(msg));
        return;
    }

    // Some providers send keep-alive / empty choices frames; indexing into
    // an empty array would be undefined behavior.
    const QJsonArray choices = json.value("choices").toArray();
    if (choices.isEmpty()) return;

    const QJsonObject delta = choices.at(0).toObject().value("delta").toObject();

    // Content delta
    const QString content = delta.value("content").toString();
    if (!content.isEmpty()) {
        ctx.content += content;
        emit streamingChunk(content);
    }

    // Tool call fragments: {index, id?, function: {name?, arguments?}}
    const QJsonArray toolCalls = delta.value("tool_calls").toArray();
    for (const auto& tc : toolCalls) {
        const QJsonObject tcObj = tc.toObject();
        const int index = tcObj.value("index").toInt(-1);
        if (index < 0) continue;
        while (state->toolCalls.size() <= index)
            state->toolCalls.append(StreamToolCall{});

        if (tcObj.contains("id"))
            state->toolCalls[index].id = tcObj.value("id").toString();
        const QJsonObject fn = tcObj.value("function").toObject();
        if (fn.contains("name"))
            state->toolCalls[index].name =
                accumulateName(state->toolCalls[index].name, fn.value("name").toString());
        if (fn.contains("arguments"))
            state->toolCalls[index].arguments =
                accumulateArguments(state->toolCalls[index].arguments,
                                    fn.value("arguments").toString());
    }
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
        // Same contract as the streaming path: malformed arguments keep the
        // call alive with an empty object, but are logged.
        QJsonParseError parseError;
        const QJsonDocument argsDoc = QJsonDocument::fromJson(
            function.value("arguments").toString().toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError)
            qWarning() << "Malformed tool call arguments from model:"
                       << parseError.errorString();
        info.arguments = argsDoc.object();
        if (info.name.isEmpty()) continue;
        response.toolCalls.append(info);
    }

    const QJsonObject usage = json.value("usage").toObject();
    response.tokensUsed = usage.value("total_tokens").toInt();

    // docs/16 §5.1 / 附录 A: 分类 token 计数（来自 API usage 对象）
    response.promptTokens = usage.value("prompt_tokens").toInt();
    response.completionTokens = usage.value("completion_tokens").toInt();
    response.cachedTokens = usage.value("prompt_tokens_details").toObject()
                                .value("cached_tokens").toInt();

    return response;
}

} // namespace NexusAI
