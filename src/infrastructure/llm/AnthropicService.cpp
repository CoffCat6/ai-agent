#include "AnthropicService.h"

#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>

namespace NexusAI {

AnthropicService::AnthropicService(QObject* parent)
    : LLMServiceBase(parent)
{
    m_model = QStringLiteral("claude-sonnet-4-5");
    m_baseUrl = QStringLiteral("https://api.anthropic.com");
}

// OpenAI wire format {type:"function",function:{name,description,parameters}}
// -> Messages API {name,description,input_schema}
QJsonArray AnthropicService::convertTools(const QJsonArray& tools) {
    QJsonArray out;
    for (const auto& tool : tools) {
        const QJsonObject obj = tool.toObject();
        const QJsonObject fn = obj.contains("function")
            ? obj.value("function").toObject() : obj;
        QJsonObject t;
        t["name"] = fn.value("name").toString();
        t["description"] = fn.value("description").toString();
        t["input_schema"] = fn.value("parameters").toObject();
        out.append(t);
    }
    return out;
}

AnthropicService::ConvertedHistory
AnthropicService::convertMessages(const QJsonArray& messages) {
    ConvertedHistory out;

    // Tool results travel as user turns; a run of role:"tool" messages
    // merges into one user message holding several tool_result blocks.
    QJsonArray pendingResults;
    const auto flushResults = [&]() {
        if (pendingResults.isEmpty()) return;
        QJsonObject m;
        m["role"] = "user";
        m["content"] = pendingResults;
        out.messages.append(m);
        pendingResults = QJsonArray{};
    };

    // Merge consecutive plain-text turns of the same role
    const auto appendMerged = [&](const QString& role, const QString& content) {
        if (!out.messages.isEmpty()
                && out.messages.last().toObject().value("role").toString() == role) {
            QJsonObject last = out.messages.last().toObject();
            if (last.value("content").isString()) {
                last["content"] = last.value("content").toString()
                    + QStringLiteral("\n\n") + content;
                out.messages.replace(out.messages.size() - 1, last);
                return;
            }
        }
        QJsonObject m;
        m["role"] = role;
        m["content"] = content;
        out.messages.append(m);
    };

    for (const auto& entry : messages) {
        const QJsonObject msg = entry.toObject();
        const QString role = msg.value("role").toString();

        if (role == "system") {
            const QString content = msg.value("content").toString();
            if (!content.isEmpty()) {
                if (!out.system.isEmpty()) out.system += QStringLiteral("\n\n");
                out.system += content;
            }
            continue;
        }

        if (role == "tool") {
            QJsonObject result;
            result["type"] = "tool_result";
            result["tool_use_id"] = msg.value("tool_call_id").toString();
            result["content"] = msg.value("content").toString();
            pendingResults.append(result);
            continue;
        }

        // Any non-tool message ends the tool-result run
        flushResults();

        if (role == "assistant") {
            const QJsonArray toolCalls = msg.value("tool_calls").toArray();
            if (toolCalls.isEmpty()) {
                const QString content = msg.value("content").toString();
                if (!content.isEmpty()) appendMerged("assistant", content);
                continue;
            }

            // Assistant turn requesting tools: content blocks with tool_use
            QJsonArray blocks;
            const QString text = msg.value("content").toString();
            if (!text.isEmpty())
                blocks.append(QJsonObject{{"type", "text"}, {"text", text}});
            for (const auto& tc : toolCalls) {
                const QJsonObject tcObj = tc.toObject();
                const QJsonObject fn = tcObj.value("function").toObject();
                blocks.append(QJsonObject{
                    {"type", "tool_use"},
                    {"id", tcObj.value("id").toString()},
                    {"name", fn.value("name").toString()},
                    // OpenAI carries arguments as a JSON string; the Messages
                    // API wants the parsed object
                    {"input", QJsonDocument::fromJson(
                        fn.value("arguments").toString().toUtf8()).object()},
                });
            }
            QJsonObject m;
            m["role"] = "assistant";
            m["content"] = blocks;
            out.messages.append(m);
            continue;
        }

        if (role == "user") {
            const QString content = msg.value("content").toString();
            if (!content.isEmpty()) appendMerged("user", content);
        }
    }
    flushResults();

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
        const QString type = b.value("type").toString();
        if (type == QLatin1String("text")) {
            text += b.value("text").toString();
        } else if (type == QLatin1String("tool_use")) {
            ToolCallInfo info;
            info.id = b.value("id").toString();
            info.name = b.value("name").toString();
            info.arguments = b.value("input").toObject();
            if (!info.name.isEmpty())
                response.toolCalls.append(info);
        }
    }
    response.content = text;
    response.success = true;

    const QJsonObject usage = json.value("usage").toObject();
    response.tokensUsed = usage.value("input_tokens").toInt()
                        + usage.value("output_tokens").toInt();
    return response;
}

void AnthropicService::chatComplete(const QJsonArray& messages, const QJsonArray& tools) {
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
    if (!tools.isEmpty())
        body["tools"] = convertTools(tools);

    QNetworkRequest request;
    request.setUrl(QUrl(m_baseUrl + "/v1/messages"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("x-api-key", m_apiKey.toUtf8());
    request.setRawHeader("anthropic-version", "2023-06-01");
    request.setRawHeader("Accept", "text/event-stream");
    request.setTransferTimeout(120000);

    startReply(request, QJsonDocument(body).toJson());
}

// Request-scoped state: tool_use blocks must not survive into the next
// request, or the previous response's calls are re-emitted with every
// subsequent response on this instance (deterministic knowledge-tool
// cross-talk).
void AnthropicService::resetStreamState() {
    m_toolUses.clear();
    LLMServiceBase::resetStreamState();
}

bool AnthropicService::streamComplete() const {
    return m_doneReceived || !m_content.isEmpty() || !m_toolUses.isEmpty();
}

LLMResponse AnthropicService::streamResult() const {
    LLMResponse response;
    response.success = true;
    response.content = m_content;
    response.tokensUsed = m_tokensUsed;
    for (const auto& tu : m_toolUses) {
        if (tu.name.isEmpty()) continue;
        ToolCallInfo info;
        info.id = tu.id;
        info.name = tu.name;
        // Same contract as the OpenAI path: malformed accumulated JSON keeps
        // the call alive with an empty object, but is logged.
        QJsonParseError parseError;
        const QJsonDocument argsDoc =
            QJsonDocument::fromJson(tu.json.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError)
            qWarning() << "Malformed tool call arguments from model:"
                       << parseError.errorString() << tu.json;
        info.arguments = argsDoc.object();
        response.toolCalls.append(info);
    }
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
    if (type == QLatin1String("content_block_start")) {
        const QJsonObject block = json.value("content_block").toObject();
        if (block.value("type").toString() == QLatin1String("tool_use")) {
            StreamToolUse tu;
            tu.id = block.value("id").toString();
            tu.name = block.value("name").toString();
            m_toolUses.append(tu);
        }
        return;
    }
    if (type == QLatin1String("content_block_delta")) {
        const QJsonObject delta = json.value("delta").toObject();
        const QString deltaType = delta.value("type").toString();
        if (deltaType == QLatin1String("text_delta")) {
            const QString text = delta.value("text").toString();
            if (!text.isEmpty()) {
                m_content += text;
                emit streamingChunk(text);
            }
        } else if (deltaType == QLatin1String("input_json_delta")) {
            if (!m_toolUses.isEmpty())
                m_toolUses.last().json += delta.value("partial_json").toString();
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
