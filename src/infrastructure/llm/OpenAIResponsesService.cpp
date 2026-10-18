#include "OpenAIResponsesService.h"

#include "OpenAIService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QDebug>

namespace NexusAI {

namespace {

// Responses 的输出上限含推理 token，低于下限的请求会直接 incomplete。
constexpr int kMinMaxOutputTokens = 16;

// usage 键名与 Chat 不同：input_tokens / output_tokens，缓存命中在
// input_tokens_details.cached_tokens。
//
// 只填 cachedTokens，不填 cacheReadTokens/cacheCreationTokens：AIAgent 把这几
// 个桶相加算输入，Anthropic 的 input_tokens 不含缓存所以另立两桶，而 Responses
// 的 input_tokens 已经把缓存算进去了，重复填会双计。
void applyUsage(LLMResponse& response, const QJsonObject& usage) {
    if (usage.isEmpty()) return;
    response.tokensUsed = usage.value("total_tokens").toInt();
    response.promptTokens = usage.value("input_tokens").toInt();
    response.completionTokens = usage.value("output_tokens").toInt();
    response.cachedTokens = usage.value("input_tokens_details").toObject()
                                .value("cached_tokens").toInt();
}

QString argumentsToString(const QJsonValue& arguments) {
    if (arguments.isString()) return arguments.toString();
    if (arguments.isObject()) {
        return QString::fromUtf8(QJsonDocument(arguments.toObject())
                                     .toJson(QJsonDocument::Compact));
    }
    return QStringLiteral("{}");
}

// Chat 的多模态 content 数组 → Responses 的输入内容块。两家的类型名不同
// （text/image_url 对 input_text/input_image），且图片地址从对象降成字符串。
QJsonArray toInputParts(const QJsonValue& content) {
    QJsonArray parts;
    for (const auto& entry : content.toArray()) {
        const QJsonObject part = entry.toObject();
        const QString type = part.value("type").toString();
        if (type == QLatin1String("text")) {
            parts.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("input_text")},
                                     {QStringLiteral("text"), part.value("text").toString()}});
        } else if (type == QLatin1String("image_url")) {
            parts.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("input_image")},
                                     {QStringLiteral("image_url"),
                                      part.value("image_url").toObject().value("url").toString()}});
        } else if (type == QLatin1String("input_text")
                || type == QLatin1String("input_image")) {
            parts.append(part);   // 已是 Responses 形态，原样透传
        }
    }
    return parts;
}

} // namespace

OpenAIResponsesService::OpenAIResponsesService(QObject* parent)
    : LLMServiceBase(parent)
{
    m_model = QStringLiteral("gpt-4o-mini");
    m_baseUrl = QStringLiteral("https://api.openai.com/v1");
}

void OpenAIResponsesService::setSamplingParams(const QVariant& temperature,
                                               const QVariant& maxOutputTokens) {
    m_temperature = temperature;
    m_maxOutputTokens = maxOutputTokens;
}

void OpenAIResponsesService::setReasoningEffort(const QVariant& effort) {
    m_reasoningEffort = effort;
}

QString OpenAIResponsesService::reasoningEffortParam(const QString& level) {
    // 内部四档 off/low/medium/high；线上枚举没有 off（关闭是 none）。
    if (level == QLatin1String("off")) return QStringLiteral("none");
    if (level == QLatin1String("minimal") || level == QLatin1String("low")
            || level == QLatin1String("medium") || level == QLatin1String("high")
            || level == QLatin1String("xhigh") || level == QLatin1String("max"))
        return level;
    return {};   // 未知 / 未设置：不发 reasoning 字段
}

QString OpenAIResponsesService::instructionsFromMessages(const QJsonArray& messages) {
    QStringList chunks;
    for (const auto& entry : messages) {
        const QJsonObject msg = entry.toObject();
        const QString role = msg.value("role").toString();
        if (role != QLatin1String("system") && role != QLatin1String("developer"))
            continue;
        const QString text = msg.value("content").toString();
        if (!text.isEmpty()) chunks.append(text);
    }
    return chunks.join(QStringLiteral("\n\n"));
}

QJsonArray OpenAIResponsesService::convertMessagesToInput(const QJsonArray& messages) {
    QJsonArray input;
    for (const auto& entry : messages) {
        const QJsonObject msg = entry.toObject();
        const QString role = msg.value("role").toString();

        // system/developer 已经抽进顶层 instructions，不再进 input。
        if (role == QLatin1String("system") || role == QLatin1String("developer"))
            continue;

        if (role == QLatin1String("tool")) {
            QJsonObject item;
            item[QStringLiteral("type")] = QStringLiteral("function_call_output");
            item[QStringLiteral("call_id")] = msg.value("tool_call_id").toString();
            const QJsonValue content = msg.value("content");
            item[QStringLiteral("output")] = content.isString()
                ? QJsonValue(content.toString())
                : QJsonValue(toInputParts(content));
            input.append(item);
            continue;
        }

        if (role == QLatin1String("assistant")) {
            const QString text = msg.value("content").toString();
            // 顺序按模型产出的样子：先正文，再每个工具调用各一条 item。
            if (!text.isEmpty()) {
                input.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                                         {QStringLiteral("content"), text}});
            }
            for (const auto& call : msg.value("tool_calls").toArray()) {
                const QJsonObject tc = call.toObject();
                const QJsonObject fn = tc.value("function").toObject();
                QJsonObject item;
                item[QStringLiteral("type")] = QStringLiteral("function_call");
                // call_id 是回传 function_call_output 时唯一认得的关联键，
                // 必须原样带回去，不能重新生成。
                item[QStringLiteral("call_id")] = tc.value("id").toString();
                item[QStringLiteral("name")] = fn.value("name").toString();
                item[QStringLiteral("arguments")] = argumentsToString(fn.value("arguments"));
                input.append(item);
            }
            continue;
        }

        // user（以及陌生角色——内容不该被静默丢掉）按用户输入处理。
        const QString text = msg.value("content").toString();
        const QJsonArray images = msg.value("images").toArray();
        QJsonObject item;
        item[QStringLiteral("role")] = QStringLiteral("user");
        if (!images.isEmpty()) {
            QJsonArray parts;
            if (!text.isEmpty()) {
                parts.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("input_text")},
                                         {QStringLiteral("text"), text}});
            }
            for (const auto& image : images) {
                parts.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("input_image")},
                                         {QStringLiteral("image_url"), image.toString()}});
            }
            item[QStringLiteral("content")] = parts;
        } else if (msg.value("content").isArray()) {
            item[QStringLiteral("content")] = toInputParts(msg.value("content"));
        } else {
            item[QStringLiteral("content")] = text;
        }
        input.append(item);
    }
    return input;
}

// Chat 的 tools 有 function 包装层，Responses 是扁平的。
//
// strict 显式给 false：省略它时服务端会尝试把 schema 自动规范化成 strict，而
// 本项目由 ToolRegistry 生成的 schema 不保证满足 strict 的
// additionalProperties:false / 全字段 required 约束，规范化不下就直接 400。
QJsonArray OpenAIResponsesService::convertTools(const QJsonArray& tools) {
    QJsonArray out;
    for (const auto& entry : tools) {
        const QJsonObject tool = entry.toObject();
        const QJsonObject fn = tool.value("function").toObject();
        const QJsonObject source = fn.isEmpty() ? tool : fn;
        QJsonObject flat;
        flat[QStringLiteral("type")] = QStringLiteral("function");
        flat[QStringLiteral("name")] = source.value("name").toString();
        if (source.contains("description"))
            flat[QStringLiteral("description")] = source.value("description");
        if (source.contains("parameters"))
            flat[QStringLiteral("parameters")] = source.value("parameters");
        flat[QStringLiteral("strict")] = false;
        out.append(flat);
    }
    return out;
}

QJsonObject OpenAIResponsesService::buildRequestBody(const QJsonArray& messages,
                                                     const QJsonArray& tools) {
    QJsonObject body;
    body[QStringLiteral("model")] = m_model;
    body[QStringLiteral("stream")] = true;
    // 无状态用法：每轮全量重发 input，服务端不留档。
    body[QStringLiteral("store")] = false;
    // 上下文超限时让服务端裁旧内容，而不是整条请求 400。
    body[QStringLiteral("truncation")] = QStringLiteral("auto");

    const QString instructions = instructionsFromMessages(messages);
    if (!instructions.isEmpty())
        body[QStringLiteral("instructions")] = instructions;
    body[QStringLiteral("input")] = convertMessagesToInput(messages);

    // 与 Chat Completions 同一套"未设置就不发字段"的 opt-in 语义。
    if (m_temperature.isValid())
        body[QStringLiteral("temperature")] = m_temperature.toDouble();
    if (m_maxOutputTokens.isValid())
        body[QStringLiteral("max_output_tokens")] =
            qMax(kMinMaxOutputTokens, m_maxOutputTokens.toInt());
    if (m_reasoningEffort.isValid()) {
        const QString effort = reasoningEffortParam(m_reasoningEffort.toString());
        if (!effort.isEmpty()) {
            body[QStringLiteral("reasoning")] = QJsonObject{{QStringLiteral("effort"), effort}};
        }
    }

    if (!tools.isEmpty()) {
        body[QStringLiteral("tools")] = convertTools(tools);
        body[QStringLiteral("tool_choice")] = QStringLiteral("auto");
    }
    return body;
}

void OpenAIResponsesService::chatComplete(const QJsonArray& messages,
                                          const QJsonArray& tools) {
    // 自建网关（localhost / 127.0.0.1）不要求 key，与 Chat Completions 一致。
    const bool isLocal = m_baseUrl.contains(QStringLiteral("localhost"), Qt::CaseInsensitive)
                      || m_baseUrl.contains(QStringLiteral("127.0.0.1"));
    if (m_apiKey.isEmpty() && !isLocal) {
        emit errorOccurred("API key not set. Configure it via the OPENAI_API_KEY "
                           "environment variable or application settings.");
        return;
    }

    QNetworkRequest request;
    request.setUrl(QUrl(m_baseUrl + "/responses"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());
    request.setRawHeader("Accept", "text/event-stream");
    request.setTransferTimeout(120000);

    startReply(request, QJsonDocument(buildRequestBody(messages, tools)).toJson());
}

QString OpenAIResponsesService::callKey(const QJsonObject& json, const QJsonObject& item) {
    const QString id = !json.value("item_id").toString().isEmpty()
        ? json.value("item_id").toString()
        : item.value("id").toString();
    if (!id.isEmpty()) return id;
    return QStringLiteral("output:%1").arg(json.value("output_index").toInt(-1));
}

OpenAIResponsesService::PendingCall&
OpenAIResponsesService::slotFor(ResponsesRequestState& state, const QString& key) {
    auto it = state.indexByKey.find(key);
    if (it == state.indexByKey.end()) {
        it = state.indexByKey.insert(key, state.calls.size());
        state.calls.append(PendingCall{key, {}, {}, {}});
    }
    return state.calls[it.value()];
}

void OpenAIResponsesService::processLine(LLMRequestContext& ctx, const QByteArray& line) {
    auto* state = static_cast<ResponsesRequestState*>(ctx.provider.data());

    if (!line.startsWith("data:")) return;   // event: / id: / 空行都不带载荷
    const QString payload = QString::fromUtf8(line.mid(5).trimmed());
    if (payload == QLatin1String("[DONE]")) {
        ctx.doneReceived = true;
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payload.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) return;
    const QJsonObject json = doc.object();

    // 载荷自带 type，所以不依赖 event: 行
    const QString type = json.value("type").toString();

    if (type == QLatin1String("response.output_text.delta")) {
        const QString delta = json.value("delta").toString();
        if (!delta.isEmpty()) {
            ctx.content += delta;
            emit streamingChunk(delta);
        }
        return;
    }

    if (type == QLatin1String("response.reasoning_summary_text.delta")
            || type == QLatin1String("response.reasoning_text.delta")) {
        const QString delta = json.value("delta").toString();
        if (!delta.isEmpty()) {
            ctx.reasoning += delta;
            emit reasoningChunk(delta);
        }
        return;
    }

    if (type == QLatin1String("error")) {
        // Responses 的 error 事件是顶层 code/message，不套 error 对象。
        QString msg = json.value("message").toString();
        if (msg.isEmpty()) msg = json.value("code").toString();
        if (msg.isEmpty()) msg = QStringLiteral("unknown stream error");
        ctx.errorEmitted = true;
        emit errorOccurred(QStringLiteral("Responses error: %1").arg(msg));
        return;
    }

    if (type == QLatin1String("response.output_item.added")
            || type == QLatin1String("response.output_item.done")) {
        const QJsonObject item = json.value("item").toObject();
        if (item.value("type").toString() != QLatin1String("function_call")) return;
        PendingCall& slot = slotFor(*state, callKey(json, item));
        // added 事件带 name，done 事件带最终 name/call_id/arguments。
        const QString name = item.value("name").toString();
        if (!name.isEmpty()) slot.name = name;
        const QString callId = item.value("call_id").toString();
        if (!callId.isEmpty()) slot.callId = callId;
        if (item.contains("arguments"))
            slot.arguments = OpenAIService::accumulateArguments(
                slot.arguments, argumentsToString(item.value("arguments")));
        return;
    }

    // 部分第三方网关不走 output_item.added/done 包装，直接吐扁平的
    // {"type":"function_call", item_id, call_id, name, arguments} 事件。
    // 累积语义与标准事件一致：item_id 归位（callKey 从载荷顶层取 item_id
    // 就是为此类事件准备的）、arguments 增量靠 accumulateArguments 折叠。
    if (type == QLatin1String("function_call")) {
        PendingCall& slot = slotFor(*state, callKey(json, QJsonObject()));
        const QString name = json.value("name").toString();
        if (!name.isEmpty()) slot.name = name;
        const QString callId = json.value("call_id").toString();
        if (!callId.isEmpty()) slot.callId = callId;
        if (json.contains("arguments"))
            slot.arguments = OpenAIService::accumulateArguments(
                slot.arguments, argumentsToString(json.value("arguments")));
        return;
    }

    if (type == QLatin1String("response.function_call_arguments.delta")
            || type == QLatin1String("response.function_call_arguments.done")) {
        // .delta 带增量、.done 带完整值（不含 name）；两者都按 item_id 归位，
        // done 的完整值要能折叠掉已累积的增量片段。
        PendingCall& slot = slotFor(*state, callKey(json, QJsonObject()));
        const QString incoming = type.endsWith(QStringLiteral(".delta"))
            ? json.value("delta").toString()
            : json.value("arguments").toString();
        slot.arguments = OpenAIService::accumulateArguments(slot.arguments, incoming);
        return;
    }

    if (type == QLatin1String("response.completed")
            || type == QLatin1String("response.incomplete")) {
        const QJsonObject response = json.value("response").toObject();
        const QJsonObject usage = response.value("usage").toObject();
        if (!usage.isEmpty()) state->usage = usage;
        if (type.endsWith(QStringLiteral(".incomplete"))) {
            qWarning() << "Responses stream incomplete:"
                       << response.value("incomplete_details").toObject()
                              .value("reason").toString();
        }
        ctx.doneReceived = true;
        return;
    }

    if (type == QLatin1String("response.failed")) {
        const QJsonObject error = json.value("response").toObject()
                                     .value("error").toObject();
        const QString msg = error.isEmpty() ? QStringLiteral("response failed")
                                            : error.value("message").toString();
        ctx.errorEmitted = true;
        emit errorOccurred(QStringLiteral("Responses error: %1").arg(msg));
        return;
    }
}

bool OpenAIResponsesService::streamComplete(const LLMRequestContext& ctx) const {
    const auto* state = static_cast<const ResponsesRequestState*>(ctx.provider.data());
    return ctx.doneReceived || !ctx.content.isEmpty() || !state->calls.isEmpty();
}

QVector<ToolCallInfo>
OpenAIResponsesService::finalizeCalls(const QVector<PendingCall>& calls) {
    QVector<ToolCallInfo> out;
    for (const auto& call : calls) {
        // 没有 name 的半截 item（网关异常 / 流被切）留不住一次调用。
        if (call.name.isEmpty()) continue;
        ToolCallInfo info;
        // id 用 call_id：下一轮要按它回传 function_call_output。
        info.id = call.callId.isEmpty() ? call.key : call.callId;
        info.name = call.name;
        QJsonParseError parseError;
        const QJsonDocument argsDoc =
            QJsonDocument::fromJson(call.arguments.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError)
            qWarning() << "Malformed tool call arguments from model:"
                       << parseError.errorString() << call.arguments;
        info.arguments = argsDoc.object();
        out.append(info);
    }
    return out;
}

LLMResponse OpenAIResponsesService::streamResult(const LLMRequestContext& ctx) const {
    const auto* state = static_cast<const ResponsesRequestState*>(ctx.provider.data());
    LLMResponse response;
    response.success = true;
    response.content = ctx.content;
    response.reasoning = ctx.reasoning;
    response.toolCalls = finalizeCalls(state->calls);
    applyUsage(response, state->usage);
    return response;
}

QString OpenAIResponsesService::extractApiError(const LLMRequestContext&,
                                                const QJsonObject& json) const {
    if (!json.contains("error")) return {};
    const QJsonValue error = json.value("error");
    QString msg;
    if (error.isString()) msg = error.toString();
    else if (error.isObject()) {
        const QJsonObject obj = error.toObject();
        msg = obj.value("message").toString();
        if (msg.isEmpty()) msg = obj.value("code").toString();
    }
    // 成功的 response 对象里 error 恒为 null，不能当错误上抛。
    return msg;
}

bool OpenAIResponsesService::isCompleteBody(const QJsonObject& json) const {
    return json.value("object").toString() == QLatin1String("response")
        || json.contains("output");
}

LLMResponse OpenAIResponsesService::parseResponse(const QJsonObject& json) {
    LLMResponse response;
    QString content;
    QString reasoning;
    QVector<PendingCall> pending;

    for (const auto& entry : json.value("output").toArray()) {
        const QJsonObject item = entry.toObject();
        const QString type = item.value("type").toString();
        if (type == QLatin1String("message")) {
            for (const auto& part : item.value("content").toArray()) {
                const QJsonObject block = part.toObject();
                const QString blockType = block.value("type").toString();
                if (blockType == QLatin1String("output_text")
                        || blockType == QLatin1String("refusal"))
                    content += block.value("text").toString();
            }
        } else if (type == QLatin1String("function_call")) {
            // 与非流式路径同一套累积键：call_id 缺失时退回 item.id。
            const QString key = item.value("call_id").toString().isEmpty()
                ? item.value("id").toString()
                : item.value("call_id").toString();
            pending.append(PendingCall{key,
                                       item.value("call_id").toString(),
                                       item.value("name").toString(),
                                       argumentsToString(item.value("arguments"))});
        } else if (type == QLatin1String("reasoning")) {
            for (const auto& part : item.value("summary").toArray())
                reasoning += part.toObject().value("text").toString();
        }
    }

    const QString status = json.value("status").toString();
    if (content.isEmpty() && pending.isEmpty() && reasoning.isEmpty()
            && status != QLatin1String("completed")) {
        response.success = false;
        response.error = QStringLiteral("No output in Responses API response (status %1)")
                             .arg(status.isEmpty() ? QStringLiteral("unknown") : status);
        return response;
    }

    response.content = content;
    response.reasoning = reasoning;
    response.toolCalls = finalizeCalls(pending);
    response.success = true;
    applyUsage(response, json.value("usage").toObject());
    return response;
}

} // namespace NexusAI
