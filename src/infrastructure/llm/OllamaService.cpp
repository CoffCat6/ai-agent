#include "OllamaService.h"

#include "core/utils/DataUrlUtils.h"

#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QByteArray>
#include <QDebug>

namespace NexusAI {

OllamaService::OllamaService(QObject* parent)
    : LLMServiceBase(parent)
{
    m_model = QStringLiteral("qwen2.5:7b");
    m_baseUrl = QStringLiteral("http://localhost:11434");
}

QJsonObject OllamaService::buildRequestBody(const QJsonArray& messages, const QJsonArray& tools) {
    QJsonObject body;
    body["model"] = m_model;
    body["stream"] = true; // NDJSON streaming
    body["messages"] = convertMessagesForOllama(messages);

    // Ollama expects bare function schemas, not the OpenAI "type/function" wrapper
    if (!tools.isEmpty()) {
        QJsonArray ollamaTools;
        for (const auto& tool : tools) {
            const QJsonObject toolObj = tool.toObject();
            ollamaTools.append(toolObj.contains("function")
                ? toolObj.value("function") : toolObj);
        }
        body["tools"] = ollamaTools;
    }

    // 思考开关是顶层字段（不在 options 里）；档位未设置时整个字段不发。
    if (m_reasoningEffort.isValid()) {
        const QVariant think = thinkFlagForLevel(m_reasoningEffort.toString());
        if (think.isValid())
            body["think"] = think.toBool();
    }

    // Batch 15: sampling params are opt-in - the options object is only
    // present when the active profile set at least one of them.
    if (m_temperature.isValid() || m_numCtx.isValid()) {
        QJsonObject options;
        if (m_temperature.isValid())
            options["temperature"] = m_temperature.toDouble();
        if (m_numCtx.isValid())
            options["num_ctx"] = m_numCtx.toInt();
        body["options"] = options;
    }

    return body;
}

void OllamaService::setSamplingParams(const QVariant& temperature, const QVariant& numCtx) {
    m_temperature = temperature;
    m_numCtx = numCtx;
}

void OllamaService::setReasoningEffort(const QVariant& effort) {
    m_reasoningEffort = effort;
}

QVariant OllamaService::thinkFlagForLevel(const QString& level) {
    if (level == QLatin1String("off")) return false;
    if (level == QLatin1String("low") || level == QLatin1String("medium")
            || level == QLatin1String("high"))
        return true;
    return {};   // 未设置 / 未知档位：字段不发，退回模型默认
}

QJsonArray OllamaService::convertMessagesForOllama(const QJsonArray& messages) {
    QJsonArray out;
    for (const auto& entry : messages) {
        QJsonObject msg = entry.toObject();
        const QString role = msg.value("role").toString();

        // Batch 30 (F6): Ollama expects bare base64 payloads in "images".
        const QJsonArray images = msg.value("images").toArray();
        if (!images.isEmpty()) {
            QJsonArray bare;
            for (const auto& image : images) {
                const auto parsed = DataUrlUtils::parseDataUrl(image.toString());
                bare.append(QString::fromUtf8(parsed.base64));
            }
            msg["images"] = bare;
        }

        if (role == "assistant") {
            // Ollama rejects null content
            if (!msg.contains("content") || msg.value("content").isNull())
                msg["content"] = QStringLiteral("");

            // OpenAI: arguments as JSON string -> Ollama: arguments as object
            if (msg.contains("tool_calls")) {
                QJsonArray converted;
                for (const auto& tc : msg.value("tool_calls").toArray()) {
                    const QJsonObject fn = tc.toObject().value("function").toObject();
                    QJsonObject cFn;
                    cFn["name"] = fn.value("name");
                    cFn["arguments"] = QJsonDocument::fromJson(
                        fn.value("arguments").toString().toUtf8()).object();
                    QJsonObject c;
                    c["function"] = cFn;
                    converted.append(c);
                }
                msg["tool_calls"] = converted;
            }
        } else if (role == "tool") {
            // Ollama tool messages only carry content
            msg.remove("tool_call_id");
        }

        out.append(msg);
    }
    return out;
}

void OllamaService::chatComplete(const QJsonArray& messages, const QJsonArray& tools) {
    QNetworkRequest request;
    request.setUrl(QUrl(joinUrl(m_baseUrl, QStringLiteral("/api/chat"))));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(300000); // local models can be slow

    startReply(request, QJsonDocument(buildRequestBody(messages, tools)).toJson());
}

bool OllamaService::streamComplete(const LLMRequestContext& ctx) const {
    return ctx.doneReceived;
}

LLMResponse OllamaService::streamResult(const LLMRequestContext& ctx) const {
    const auto* state = static_cast<const OllamaRequestState*>(ctx.provider.data());
    LLMResponse response;
    response.success = true;
    response.content = ctx.content;
    response.reasoning = ctx.reasoning;
    response.toolCalls = state->toolCalls;
    response.tokensUsed = ctx.tokensUsed;
    // docs/16 §5.1: eval counts from the done=true line (0 on aborted
    // streams - the agent then estimates).
    response.promptTokens = state->promptTokens;
    response.completionTokens = state->completionTokens;
    return response;
}

QString OllamaService::extractApiError(const LLMRequestContext& ctx,
                                       const QJsonObject& json) const {
    Q_UNUSED(ctx);
    return json.contains("error") ? json.value("error").toString() : QString();
}

bool OllamaService::isCompleteBody(const QJsonObject& json) const {
    return json.contains("message");
}

void OllamaService::processLine(LLMRequestContext& ctx, const QByteArray& line) {
    auto* state = static_cast<OllamaRequestState*>(ctx.provider.data());
    if (line.isEmpty()) return;

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) return;

    const QJsonObject json = doc.object();

    // Ollama error payload (model not found, etc.)
    if (json.contains("error")) {
        ctx.errorEmitted = true;
        emit errorOccurred(QStringLiteral("Ollama error: %1")
            .arg(json.value("error").toString()));
        return;
    }

    const QJsonObject message = json.value("message").toObject();

    // 思考正文走 message.thinking，与 content 完全分开（流式与整块同构）
    const QString thinking = message.value("thinking").toString();
    if (!thinking.isEmpty()) {
        ctx.reasoning += thinking;
        emit reasoningChunk(thinking);
    }

    // Streamed content chunk
    const QString chunk = message.value("content").toString();
    if (!chunk.isEmpty()) {
        ctx.content += chunk;
        emit streamingChunk(chunk);
    }

    // Final line of a stream carries done=true (+ tool calls + usage)
    if (json.value("done").toBool()) {
        ctx.doneReceived = true;
        state->toolCalls = parseToolCalls(message);
        // docs/16 §5.1: keep the direction split for AIAgent calibration
        // and billing instead of collapsing it into tokensUsed.
        state->promptTokens = json.value("prompt_eval_count").toInt();
        state->completionTokens = json.value("eval_count").toInt();
        ctx.tokensUsed = state->completionTokens + state->promptTokens;
    }
}

QVector<ToolCallInfo> OllamaService::parseToolCalls(const QJsonObject& message) {
    // Ollama tool_calls: {"function": {"name", "arguments": {...}}}
    // (older builds used flat {"name", "arguments"})
    QVector<ToolCallInfo> calls;
    const QJsonArray toolCalls = message.value("tool_calls").toArray();
    int index = 0;
    for (const auto& tc : toolCalls) {
        const QJsonObject tcObj = tc.toObject();
        QString name;
        QJsonObject arguments;
        if (tcObj.contains("function")) {
            const QJsonObject fn = tcObj.value("function").toObject();
            name = fn.value("name").toString();
            arguments = fn.value("arguments").toObject();
        } else {
            name = tcObj.value("name").toString();
            arguments = tcObj.value("arguments").toObject();
        }
        if (name.isEmpty()) continue;

        ToolCallInfo info;
        info.id = tcObj.value("id").toString();
        if (info.id.isEmpty()) info.id = QStringLiteral("call_%1").arg(index);
        info.name = name;
        info.arguments = arguments;
        calls.append(info);
        ++index;
    }
    return calls;
}

LLMResponse OllamaService::parseResponse(const QJsonObject& json) {
    LLMResponse response;

    const QJsonObject message = json.value("message").toObject();
    response.content = message.value("content").toString();
    response.reasoning = message.value("thinking").toString();
    response.success = true;
    response.toolCalls = parseToolCalls(message);

    // Token usage reported on the final non-streamed message
    response.tokensUsed = json.value("eval_count").toInt()
                        + json.value("prompt_eval_count").toInt();
    // docs/16 §5.1 / 附录 A: 分类 token 计数（Ollama 不支持缓存）
    response.promptTokens = json.value("prompt_eval_count").toInt();
    response.completionTokens = json.value("eval_count").toInt();

    return response;
}

} // namespace NexusAI
