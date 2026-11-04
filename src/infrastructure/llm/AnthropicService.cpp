#include "AnthropicService.h"

#include "core/utils/DataUrlUtils.h"

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
            // Batch 30 (F6): a turn carrying images becomes content blocks
            // (images + text) and is never merged with plain-text neighbors.
            const QJsonArray images = msg.value("images").toArray();
            if (!images.isEmpty()) {
                QJsonArray blocks;
                for (const auto& image : images) {
                    const auto parsed = DataUrlUtils::parseDataUrl(image.toString());
                    if (!parsed.ok) continue;
                    blocks.append(QJsonObject{
                        {"type", "image"},
                        {"source", QJsonObject{
                            {"type", "base64"},
                            {"media_type", parsed.mimeType},
                            {"data", QString::fromUtf8(parsed.base64)}
                        }}
                    });
                }
                const QString text = msg.value("content").toString();
                if (!text.isEmpty())
                    blocks.append(QJsonObject{{"type", "text"}, {"text", text}});
                QJsonObject m;
                m["role"] = "user";
                m["content"] = blocks;
                out.messages.append(m);
                continue;
            }
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
    QString thinking;
    const QJsonArray content = json.value("content").toArray();
    for (const auto& block : content) {
        const QJsonObject b = block.toObject();
        const QString type = b.value("type").toString();
        if (type == QLatin1String("text")) {
            text += b.value("text").toString();
        } else if (type == QLatin1String("thinking")) {
            // Extended thinking block: deliberation text stays out of the
            // answer (the signature field is for the API's own integrity
            // check when echoing history back, which we never do).
            thinking += b.value("thinking").toString();
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
    response.reasoning = thinking;
    response.success = true;

    const QJsonObject usage = json.value("usage").toObject();
    response.tokensUsed = usage.value("input_tokens").toInt()
                        + usage.value("output_tokens").toInt();
    // docs/16 §5.1 / 附录 A: 分类 token 计数（来自 API usage 对象）
    response.promptTokens = usage.value("input_tokens").toInt();
    response.completionTokens = usage.value("output_tokens").toInt();
    response.cacheReadTokens = usage.value("cache_read_input_tokens").toInt();
    response.cacheCreationTokens = usage.value("cache_creation_input_tokens").toInt();
    return response;
}

// Batch 15 (FEAT-1502): max_tokens is required by the Messages API, so an
// unset profile value keeps the historical 4096; temperature stays optional.
void AnthropicService::setSamplingParams(const QVariant& temperature,
                                         const QVariant& maxOutputTokens) {
    m_maxTokens = maxOutputTokens.isValid() ? qMax(1, maxOutputTokens.toInt()) : 4096;
    m_temperature = temperature;
}

void AnthropicService::setReasoningEffort(const QVariant& effort) {
    m_reasoningEffort = effort;
}

int AnthropicService::thinkingBudgetTokens(const QString& level, int maxTokens) {
    int budget = 0;
    if (level == QLatin1String("low")) budget = 2048;
    else if (level == QLatin1String("medium")) budget = 8192;
    else if (level == QLatin1String("high")) budget = 16384;
    else return 0;                       // "off" / 未设置 / 未知档位

    // 预算与输出共享同一个 max_tokens 池:超出会让 API 直接 400。
    constexpr int kMinBudgetTokens = 1024;   // API 下限
    constexpr int kMinAnswerTokens = 1024;   // 留给正式回答的下限
    const int ceiling = maxTokens - kMinAnswerTokens;
    if (ceiling < kMinBudgetTokens) {
        qWarning() << "Anthropic: output cap" << maxTokens
                   << "too small for a thinking budget, sending without thinking";
        return 0;
    }
    return qMin(budget, ceiling);
}

void AnthropicService::chatComplete(const QJsonArray& messages, const QJsonArray& tools) {
    if (m_apiKey.isEmpty()) {
        emit errorOccurred("API key not set. Configure it in settings or via the "
                           "ANTHROPIC_API_KEY environment variable.");
        return;
    }

    const ConvertedHistory history = convertMessages(messages);
    if (history.messages.isEmpty()) {
        emit errorOccurred("No convertible messages for the Anthropic request");
        return;
    }

    QJsonObject body;
    body["model"] = m_model;
    body["max_tokens"] = m_maxTokens;
    const int thinkingBudget = m_reasoningEffort.isValid()
        ? thinkingBudgetTokens(m_reasoningEffort.toString(), m_maxTokens) : 0;
    if (thinkingBudget > 0) {
        body["thinking"] = QJsonObject{
            {"type", "enabled"},
            {"budget_tokens", thinkingBudget},
        };
        // 扩展思考下 temperature 只能是 1,配置值一律不发。
    } else if (m_temperature.isValid()) {
        body["temperature"] = m_temperature.toDouble();
    }
    body["stream"] = true;
    body["messages"] = history.messages;
    if (!history.system.isEmpty())
        body["system"] = history.system;
    if (!tools.isEmpty())
        body["tools"] = convertTools(tools);

    QNetworkRequest request;
    request.setUrl(QUrl(joinUrl(m_baseUrl, QStringLiteral("/v1/messages"))));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("x-api-key", m_apiKey.toUtf8());
    request.setRawHeader("anthropic-version", "2023-06-01");
    request.setRawHeader("Accept", "text/event-stream");
    request.setTransferTimeout(120000);

    startReply(request, QJsonDocument(body).toJson());
}

bool AnthropicService::streamComplete(const LLMRequestContext& ctx) const {
    const auto* state = static_cast<const AnthropicRequestState*>(ctx.provider.data());
    return ctx.doneReceived || !ctx.content.isEmpty() || !state->toolUses.isEmpty();
}

LLMResponse AnthropicService::streamResult(const LLMRequestContext& ctx) const {
    const auto* state = static_cast<const AnthropicRequestState*>(ctx.provider.data());
    LLMResponse response;
    response.success = true;
    response.content = ctx.content;
    response.reasoning = ctx.reasoning;
    response.tokensUsed = ctx.tokensUsed;
    // docs/16 §5.1: direction split captured from message_start/message_delta.
    // Zero when the stream ended early - the agent then estimates.
    response.promptTokens = state->promptTokens;
    response.completionTokens = state->completionTokens;
    response.cacheReadTokens = state->cacheReadTokens;
    response.cacheCreationTokens = state->cacheCreationTokens;
    for (const auto& tu : state->toolUses) {
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

QString AnthropicService::extractApiError(const LLMRequestContext& ctx,
                                          const QJsonObject& json) const {
    Q_UNUSED(ctx);
    if (json.value("type").toString() != QLatin1String("error")
            && !json.contains("error"))
        return {};
    return parseResponse(json).error;
}

bool AnthropicService::isCompleteBody(const QJsonObject& json) const {
    return json.contains("content");
}

void AnthropicService::processLine(LLMRequestContext& ctx, const QByteArray& line) {
    auto* state = static_cast<AnthropicRequestState*>(ctx.provider.data());

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
        ctx.errorEmitted = true;
        emit errorOccurred(QStringLiteral("Anthropic error: %1").arg(msg));
        return;
    }
    if (type == QLatin1String("message_start")) {
        // input tokens (+ cache split) arrive on the opening event
        const QJsonObject usage = json.value("message").toObject()
                                      .value("usage").toObject();
        ctx.tokensUsed += usage.value("input_tokens").toInt();
        // docs/16 §5.1: keep the direction split for AIAgent calibration
        // and billing instead of collapsing it into tokensUsed.
        state->promptTokens = usage.value("input_tokens").toInt();
        state->cacheReadTokens = usage.value("cache_read_input_tokens").toInt();
        state->cacheCreationTokens =
            usage.value("cache_creation_input_tokens").toInt();
        return;
    }
    if (type == QLatin1String("content_block_start")) {
        const QJsonObject block = json.value("content_block").toObject();
        if (block.value("type").toString() == QLatin1String("tool_use")) {
            StreamToolUse tu;
            tu.id = block.value("id").toString();
            tu.name = block.value("name").toString();
            state->toolUses.append(tu);
        }
        return;
    }
    if (type == QLatin1String("content_block_delta")) {
        const QJsonObject delta = json.value("delta").toObject();
        const QString deltaType = delta.value("type").toString();
        if (deltaType == QLatin1String("text_delta")) {
            const QString text = delta.value("text").toString();
            if (!text.isEmpty()) {
                ctx.content += text;
                emit streamingChunk(text);
            }
        } else if (deltaType == QLatin1String("thinking_delta")) {
            // Extended thinking streams on its own channel; "signature_delta"
            // (the cryptographic proof) is display-irrelevant and skipped.
            const QString thinking = delta.value("thinking").toString();
            if (!thinking.isEmpty()) {
                ctx.reasoning += thinking;
                emit reasoningChunk(thinking);
            }
        } else if (deltaType == QLatin1String("input_json_delta")) {
            if (!state->toolUses.isEmpty())
                state->toolUses.last().json += delta.value("partial_json").toString();
        }
        return;
    }
    if (type == QLatin1String("message_delta")) {
        // output_tokens arrives cumulatively - the final event carries the
        // full completion count.
        const int outputTokens =
            json.value("usage").toObject().value("output_tokens").toInt();
        ctx.tokensUsed += outputTokens;
        state->completionTokens = outputTokens;
        return;
    }
    if (type == QLatin1String("message_stop")) {
        ctx.doneReceived = true;
    }
}

} // namespace NexusAI
