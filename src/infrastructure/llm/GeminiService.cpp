#include "GeminiService.h"

#include "core/utils/DataUrlUtils.h"

#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QDebug>

namespace NexusAI {

namespace {

// m-4：Gemini Schema 子集不支持的关键字。MCP 的 inputSchema 是完整 JSON
// Schema，一旦这些关键字进了 functionDeclarations.parameters，整条工具声明
// 会直接 400。纯约束性关键字这里直接裁掉（丢约束保结构）；组合/引用类
// （anyOf/oneOf/allOf/$ref）在 sanitizeSchemaNode 里先收敛再裁剪。
bool isGeminiUnsupportedKeyword(const QString& key) {
    static const QSet<QString> unsupported = {
        QStringLiteral("$ref"), QStringLiteral("$schema"), QStringLiteral("$defs"),
        QStringLiteral("$id"), QStringLiteral("definitions"),
        QStringLiteral("oneOf"), QStringLiteral("anyOf"), QStringLiteral("allOf"),
        QStringLiteral("not"), QStringLiteral("patternProperties"),
        QStringLiteral("additionalProperties"), QStringLiteral("const"),
        QStringLiteral("contains"), QStringLiteral("if"), QStringLiteral("then"),
        QStringLiteral("else"), QStringLiteral("minItems"), QStringLiteral("maxItems"),
        QStringLiteral("uniqueItems"), QStringLiteral("minLength"),
        QStringLiteral("maxLength"), QStringLiteral("pattern"),
        QStringLiteral("minProperties"), QStringLiteral("maxProperties"),
        QStringLiteral("minimum"), QStringLiteral("maximum"),
        QStringLiteral("exclusiveMinimum"), QStringLiteral("exclusiveMaximum"),
        QStringLiteral("multipleOf"), QStringLiteral("default"),
        QStringLiteral("examples"), QStringLiteral("contentMediaType"),
        QStringLiteral("contentEncoding"), QStringLiteral("readOnly"),
        QStringLiteral("writeOnly"),
    };
    return unsupported.contains(key);
}

// 递归把一段 schema 节点降到 Gemini Schema 子集（原地修改，返回 false 表示
// 该节点无法安全降级）。组合器（anyOf/oneOf/allOf）收敛到第一个能降级的
// 分支并并入其约束；$ref 无法解析直接丢弃；properties/items 递归下探。
// 处理后节点为空（所有约束都被裁掉）时返回 false，表示无法安全降级。
bool sanitizeSchemaNode(QJsonObject& node) {
    const char* combinators[] = {"anyOf", "oneOf", "allOf"};
    for (const char* key : combinators) {
        if (!node.contains(key)) continue;
        const QJsonArray branches = node.value(key).toArray();
        node.remove(key);
        for (const auto& b : branches) {
            if (!b.isObject()) continue;
            QJsonObject candidate = b.toObject();
            if (!sanitizeSchemaNode(candidate)) continue;
            for (auto it = candidate.begin(); it != candidate.end(); ++it) {
                if (!node.contains(it.key())) node.insert(it.key(), it.value());
            }
            break;
        }
    }

    for (auto it = node.begin(); it != node.end();) {
        if (isGeminiUnsupportedKeyword(it.key())) it = node.erase(it);
        else ++it;
    }

    if (node.contains(QStringLiteral("properties"))) {
        const QJsonValue pv = node.value(QStringLiteral("properties"));
        if (pv.isObject()) {
            QJsonObject props = pv.toObject();
            for (auto it = props.begin(); it != props.end();) {
                if (it.value().isObject()) {
                    QJsonObject sub = it.value().toObject();
                    if (sanitizeSchemaNode(sub)) { it.value() = sub; ++it; continue; }
                }
                it = props.erase(it);
            }
            node[QStringLiteral("properties")] = props;
        } else {
            node.remove(QStringLiteral("properties"));
        }
    }

    if (node.contains(QStringLiteral("items"))) {
        const QJsonValue iv = node.value(QStringLiteral("items"));
        if (iv.isObject()) {
            QJsonObject items = iv.toObject();
            if (sanitizeSchemaNode(items)) node[QStringLiteral("items")] = items;
            else node.remove(QStringLiteral("items"));
        } else {
            node.remove(QStringLiteral("items"));
        }
    }

    // 节点为空表示所有约束都被裁掉，无法安全降级
    return !node.isEmpty();
}

} // namespace

GeminiService::GeminiService(QObject* parent)
    : LLMServiceBase(parent)
{
    m_model = QStringLiteral("gemini-2.5-flash");
    m_baseUrl = QStringLiteral("https://generativelanguage.googleapis.com");
}

// OpenAI wire format {type:"function",function:{name,description,parameters}}
// -> Gemini [{"functionDeclarations":[{name,description,parameters}]}]。
// parameters 就是 JSON Schema 对象，两个协议同形；但 Gemini 只吃它的一个子
// 集，超出子集（$ref/anyOf/oneOf 等）会整体 400 —— 这里经 sanitizeSchema 做
// 子集校验/降级，无法安全降级的工具整条跳过 + qWarning，保住其余工具可用。
QJsonArray GeminiService::convertTools(const QJsonArray& tools) {
    QJsonArray declarations;
    for (const auto& tool : tools) {
        const QJsonObject obj = tool.toObject();
        const QJsonObject fn = obj.contains("function")
            ? obj.value("function").toObject() : obj;
        const QString name = fn.value("name").toString();

        bool schemaOk = true;
        const QJsonObject parameters =
            sanitizeSchema(fn.value("parameters").toObject(), &schemaOk);
        if (!schemaOk) {
            qWarning() << "Gemini: skipping tool" << name
                       << "- inputSchema not representable in the Gemini Schema subset";
            continue;
        }

        QJsonObject d;
        d["name"] = name;
        d["description"] = fn.value("description").toString();
        d["parameters"] = parameters;
        declarations.append(d);
    }
    return QJsonArray{ QJsonObject{{"functionDeclarations", declarations}} };
}

// m-4：把 MCP inputSchema（JSON Schema）降到 Gemini Schema 子集。顶层强制收
// 敛为 object+properties（functionDeclarations.parameters 期待对象）；明确是
// 非对象标量/数组且无可收敛 properties 的顶层，无法安全表达成命名参数对象，
// 返回空并置 *ok=false（调用方跳过该工具）。type 强制写小写 object：与本项
// 目 inputSchema 的 JSON Schema 形态一致（Gemini 侧按小写可正常接收）。
QJsonObject GeminiService::sanitizeSchema(const QJsonObject& inputSchema, bool* ok) {
    QJsonObject schema = inputSchema;
    if (!sanitizeSchemaNode(schema)) {
        // 顶层节点无法安全降级，跳过该工具
        if (ok) *ok = false;
        return {};
    }

    const QString type = schema.value(QStringLiteral("type")).toString().toLower();
    const bool structurallyScalarOrArray =
        type == QLatin1String("array") || type == QLatin1String("string")
        || type == QLatin1String("number") || type == QLatin1String("integer")
        || type == QLatin1String("boolean");
    if (structurallyScalarOrArray && !schema.contains(QStringLiteral("properties"))) {
        if (ok) *ok = false;
        return {};
    }

    schema[QStringLiteral("type")] = QStringLiteral("object");
    if (!schema.contains(QStringLiteral("properties")))
        schema[QStringLiteral("properties")] = QJsonObject{};
    if (ok) *ok = true;
    return schema;
}

GeminiService::ConvertedRequest
GeminiService::convertMessages(const QJsonArray& messages) {
    ConvertedRequest out;

    // tool 消息只带 tool_call_id，而 functionResponse 必须给函数名——从
    // 历史 assistant tool_calls 里按 id 反查（wire format 由 Agent 层构
    // 造，tool 消息总是跟在带 tool_calls 的 assistant 轮后，配对成立；
    // 反查不到时退回 id 本身，至少保住报文结构可发）。
    QHash<QString, QString> callIdToName;

    // 连续多条 tool 结果合并进一个 user content 的多个 functionResponse
    // parts（Gemini 要求结果与调用轮相邻，多个结果同轮送达；参照
    // Anthropic 的 tool_result run-merge 模式）。
    QJsonArray pendingResponses;
    const auto flushResponses = [&]() {
        if (pendingResponses.isEmpty()) return;
        out.contents.append(QJsonObject{
            {"role", "user"},
            {"parts", pendingResponses},
        });
        pendingResponses = QJsonArray{};
    };

    // 同角色连续纯文本轮合并：只并入"末轮恰为单个 text part"的情况，
    // 绝不与 inlineData/functionCall 结构混拼。
    const auto appendMerged = [&](const QString& role, const QString& text) {
        if (!out.contents.isEmpty()
                && out.contents.last().toObject().value("role").toString() == role) {
            QJsonObject last = out.contents.last().toObject();
            const QJsonArray parts = last.value("parts").toArray();
            if (parts.size() == 1 && parts.at(0).toObject().contains("text")) {
                QJsonObject mergedText = parts.at(0).toObject();
                mergedText["text"] = mergedText.value("text").toString()
                    + QStringLiteral("\n\n") + text;
                last["parts"] = QJsonArray{mergedText};
                out.contents.replace(out.contents.size() - 1, last);
                return;
            }
        }
        out.contents.append(QJsonObject{
            {"role", role},
            {"parts", QJsonArray{QJsonObject{{"text", text}}}},
        });
    };

    for (const auto& entry : messages) {
        const QJsonObject msg = entry.toObject();
        const QString role = msg.value("role").toString();

        if (role == "system") {
            const QString content = msg.value("content").toString();
            if (!content.isEmpty()) {
                if (!out.systemInstruction.isEmpty())
                    out.systemInstruction += QStringLiteral("\n\n");
                out.systemInstruction += content;
            }
            continue;
        }

        if (role == "tool") {
            const QString callId = msg.value("tool_call_id").toString();
            const QString content = msg.value("content").toString();
            // content 通常是工具结果的 JSON 字符串，response 要对象；但
            // Agent 层有多条真实路径发纯文本（用户拒绝授权 AIAgent.cpp:
            // 1749 / 重复调用跳过 :1174 / 封口 note :1436），解析失败或
            // 非对象时包一层 {"result": 原文}——静默变空 {} 会让模型看
            // 不到"用户拒绝"语义而重复调用被拒工具（M-2，任务 #27）。
            const QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8());
            const QJsonObject responseObj = doc.isObject()
                ? doc.object() : QJsonObject{{"result", content}};
            pendingResponses.append(QJsonObject{
                {"functionResponse", QJsonObject{
                    {"name", callIdToName.value(callId, callId)},
                    {"response", responseObj},
                }},
            });
            continue;
        }

        // 任何非 tool 消息结束 tool 结果 run
        flushResponses();

        if (role == "assistant") {
            const QJsonArray toolCalls = msg.value("tool_calls").toArray();
            const QString text = msg.value("content").toString();
            if (toolCalls.isEmpty()) {
                if (!text.isEmpty()) appendMerged("model", text);
                continue;
            }

            // model 轮请求工具：text part（可选）+ functionCall parts
            QJsonArray parts;
            if (!text.isEmpty())
                parts.append(QJsonObject{{"text", text}});
            for (const auto& tc : toolCalls) {
                const QJsonObject tcObj = tc.toObject();
                const QJsonObject fn = tcObj.value("function").toObject();
                const QString name = fn.value("name").toString();
                // 登记 id → name：后续 tool 消息的 functionResponse 反查用
                if (!name.isEmpty())
                    callIdToName.insert(tcObj.value("id").toString(), name);
                parts.append(QJsonObject{
                    {"functionCall", QJsonObject{
                        {"name", name},
                        // OpenAI 的 arguments 是 JSON 字符串；Gemini 的
                        // args 要解析后的对象
                        {"args", QJsonDocument::fromJson(
                            fn.value("arguments").toString().toUtf8()).object()},
                    }},
                });
            }
            out.contents.append(QJsonObject{
                {"role", "model"},
                {"parts", parts},
            });
            continue;
        }

        if (role == "user") {
            // 带 images 的轮转成 [text(可选), inlineData...] parts，且不
            // 与纯文本邻轮合并（结构轮不可混拼）。
            const QJsonArray images = msg.value("images").toArray();
            if (!images.isEmpty()) {
                QJsonArray parts;
                const QString text = msg.value("content").toString();
                if (!text.isEmpty())
                    parts.append(QJsonObject{{"text", text}});
                for (const auto& image : images) {
                    const auto parsed = DataUrlUtils::parseDataUrl(image.toString());
                    if (!parsed.ok) continue;
                    parts.append(QJsonObject{
                        {"inlineData", QJsonObject{
                            {"mimeType", parsed.mimeType},
                            {"data", QString::fromUtf8(parsed.base64)},
                        }},
                    });
                }
                out.contents.append(QJsonObject{
                    {"role", "user"},
                    {"parts", parts},
                });
                continue;
            }
            const QString content = msg.value("content").toString();
            if (!content.isEmpty()) appendMerged("user", content);
        }
    }
    flushResponses();

    // 会话以 user 开头（对齐 Anthropic 模板）：历史残留的 model 开头轮
    // 对 Gemini 无意义，删掉直到第一个 user 轮。
    while (!out.contents.isEmpty()
           && out.contents.first().toObject().value("role").toString() != "user") {
        out.contents.removeAt(0);
    }
    return out;
}

LLMResponse GeminiService::parseResponse(const QJsonObject& json) {
    LLMResponse response;

    // 错误体 {"error":{"code":400,"message":"...","status":"INVALID_ARGUMENT"}}
    if (json.contains("error")) {
        const QJsonObject err = json.value("error").toObject();
        QString msg = err.value("message").toString();
        if (msg.isEmpty()) msg = QStringLiteral("Gemini API error");
        const QString status = err.value("status").toString();
        response.error = status.isEmpty()
            ? msg : msg + QStringLiteral(" (") + status + QLatin1Char(')');
        return response;   // success 保持 false
    }

    const QJsonArray candidates = json.value("candidates").toArray();
    if (candidates.isEmpty()) {
        // m-1（任务 #27）：请求侧安全拦截——无 candidates 无 error，只有
        // promptFeedback.blockReason。报明确的拦截原因，而不是误导性的
        // "No candidates"。
        const QString blockReason = json.value("promptFeedback").toObject()
                                        .value("blockReason").toString();
        if (!blockReason.isEmpty()) {
            response.error = QStringLiteral(
                "Request blocked by safety filters (")
                + blockReason + QLatin1Char(')');
            return response;
        }
        response.error = QStringLiteral("No candidates in Gemini response");
        return response;
    }

    QString finishReason;
    const QJsonObject candidate = candidates.at(0).toObject();
    finishReason = candidate.value("finishReason").toString();
    const QJsonArray parts = candidate.value("content").toObject()
                                  .value("parts").toArray();
    QString text;
    QString thinking;
    int callIndex = 0;
    for (const auto& part : parts) {
        const QJsonObject p = part.toObject();
        if (p.contains("functionCall")) {
            const QJsonObject call = p.value("functionCall").toObject();
            if (!call.value("name").toString().isEmpty()) {
                ToolCallInfo info;
                // Gemini 不返回调用 id，Agent 层按 id 关联工具结果——
                // 合成稳定 id 保住 wire format 兼容
                info.id = QStringLiteral("gemini-call-%1").arg(callIndex++);
                info.name = call.value("name").toString();
                info.arguments = call.value("args").toObject();
                response.toolCalls.append(info);
            }
        } else if (p.value("thought").toBool()) {
            // 思考增量走独立通道，永不混进 content
            thinking += p.value("text").toString();
        } else if (p.contains("text")) {
            text += p.value("text").toString();
        }
    }
    response.content = text;
    response.reasoning = thinking;

    // usageMetadata: promptTokenCount -> promptTokens,
    // candidatesTokenCount -> completionTokens；总消耗再计入思考 token
    const QJsonObject usage = json.value("usageMetadata").toObject();
    response.promptTokens = usage.value("promptTokenCount").toInt();
    response.completionTokens = usage.value("candidatesTokenCount").toInt();
    response.tokensUsed = response.promptTokens + response.completionTokens
                       + usage.value("thoughtsTokenCount").toInt();

    // 拦截型 finishReason（SAFETY/RECITATION/PROHIBITED/BLOCKLIST/
    // IMAGE_SAFETY/SPII，见 isBlockedFinishReason）且无任何可用输出：
    // 升级为错误——静默空响应会让用户以为模型"没说话"，其实是被安全
    // 过滤了。有部分内容时保留内容正常返回（截断的内容仍有价值）。
    if (isBlockedFinishReason(finishReason)
            && text.isEmpty() && response.toolCalls.isEmpty()) {
        response.error = QStringLiteral("Response blocked by safety filters (")
                       + finishReason + QLatin1Char(')');
        return response;
    }

    response.success = true;
    return response;
}

void GeminiService::setSamplingParams(const QVariant& temperature,
                                      const QVariant& maxOutputTokens) {
    m_maxOutputTokens = maxOutputTokens.isValid()
        ? qMax(1, maxOutputTokens.toInt()) : 4096;
    m_temperature = temperature;
}

void GeminiService::setReasoningEffort(const QVariant& effort) {
    m_reasoningEffort = effort;
}

int GeminiService::thinkingBudgetFor(const QString& level) {
    if (level == QLatin1String("low")) return 2048;
    if (level == QLatin1String("medium")) return 8192;
    if (level == QLatin1String("high")) return 16384;
    // s-1 现状登记：off / 未设置 / 未知档位返回 0 = 不指定 thinkingConfig，
    // 交由服务端默认动态思考——而非"彻底关闭思考"。Gemini 2.5/3 系列本身即
    // thinking 模型，API 没有真正的"关思考"开关，只能做到"不指定"。
    return 0;
}

// M-1（任务 #27）：思考与正文共享 maxOutputTokens 这一池（Gemini 的输
// 出上限包含思考 token），预算超过上限会被 API 直接 400。仿
// AnthropicService 的折算降级防护：钳到 maxOutputTokens - kMinAnswerTokens，
// 留不出最低预算时降级为 0（不发 thinkingConfig，宁可少思考也不发必
// 失败的报文）。
int GeminiService::clampedThinkingBudget(const QString& level,
                                         int maxOutputTokens) {
    const int budget = thinkingBudgetFor(level);
    if (budget <= 0) return 0;   // "off" / 未设置 / 未知档位

    constexpr int kMinBudgetTokens = 1024;   // 钳制后的合理下限
    constexpr int kMinAnswerTokens = 1024;   // 留给正式回答的预留
    const int ceiling = maxOutputTokens - kMinAnswerTokens;
    if (ceiling < kMinBudgetTokens) {
        qWarning() << "Gemini: output cap" << maxOutputTokens
                   << "too small for a thinking budget, sending without thinking";
        return 0;
    }
    return qMin(budget, ceiling);
}

// s-3（任务 #27）：拦截语义的 finishReason 家族——内容被安全/合规过
// 滤，零有效输出，与 SAFETY/RECITATION 同语义。
bool GeminiService::isBlockedFinishReason(const QString& reason) {
    return reason == QLatin1String("SAFETY")
        || reason == QLatin1String("RECITATION")
        || reason == QLatin1String("PROHIBITED")
        || reason == QLatin1String("BLOCKLIST")
        || reason == QLatin1String("IMAGE_SAFETY")
        || reason == QLatin1String("SPII");
}

// m-2（任务 #27）：模型名拼进 URL path，只放行不会破坏 URL 结构的
// 字符；先 trim（profileConfigOf 原样透传，首尾空白常见）。
QString GeminiService::sanitizeModelName(const QString& model) {
    const QString trimmed = model.trimmed();
    static const QRegularExpression valid(
        QStringLiteral("^[A-Za-z0-9._:~-]+$"));
    return valid.match(trimmed).hasMatch() ? trimmed : QString();
}

// m-2（任务 #27）：拼接流式端点 URL；baseUrl 末尾多余的 "/" 去重
// （避免 "//v1beta"）。
QString GeminiService::buildStreamUrl(const QString& baseUrl,
                                      const QString& model) {
    QString base = baseUrl;
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    return base + QStringLiteral("/v1beta/models/") + model
                + QStringLiteral(":streamGenerateContent?alt=sse");
}

void GeminiService::chatComplete(const QJsonArray& messages, const QJsonArray& tools) {
    if (m_apiKey.isEmpty()) {
        emit errorOccurred("API key not set. Configure it in settings or via the "
                           "GEMINI_API_KEY environment variable.");
        return;
    }

    // m-2（任务 #27）：模型名拼进 URL path，先 trim 再过白名单——异常
    // 配置宁可报清楚的配置错误，也不发一个结构被破坏的 URL（? 截断
    // alt=sse、空白被 percent-encode 成 %20、/ 改写 path）。
    const QString model = sanitizeModelName(m_model);
    if (model.isEmpty()) {
        emit errorOccurred(QStringLiteral(
            "Invalid Gemini model name '%1': only letters, digits and "
            "'.', '_', ':', '~', '-' are allowed").arg(m_model));
        return;
    }

    const ConvertedRequest req = convertMessages(messages);
    if (req.contents.isEmpty()) {
        emit errorOccurred("No convertible messages for the Gemini request");
        return;
    }

    QJsonObject body;
    body["contents"] = req.contents;
    if (!req.systemInstruction.isEmpty()) {
        body["systemInstruction"] = QJsonObject{
            {"parts", QJsonArray{QJsonObject{{"text", req.systemInstruction}}}},
        };
    }

    // 采样参数与思考档位都收在 generationConfig 里；未设置的字段不发
    //（严格端点会拒空字段，与 OpenAI 路径同一纪律）。
    QJsonObject generationConfig;
    if (m_temperature.isValid())
        generationConfig["temperature"] = m_temperature.toDouble();
    generationConfig["maxOutputTokens"] = m_maxOutputTokens;
    // M-1（任务 #27）：档位按输出上限钳制/降级（budget 与正文共享
    // maxOutputTokens 池，防 400）。
    const int thinkingBudget = m_reasoningEffort.isValid()
        ? clampedThinkingBudget(m_reasoningEffort.toString(), m_maxOutputTokens)
        : 0;
    if (thinkingBudget > 0) {
        // Gemini 的 thinking 与 temperature 可以共存（不像 Anthropic 扩展
        // 思考强制 temperature=1），两个配置各自独立生效。
        generationConfig["thinkingConfig"] = QJsonObject{
            {"thinkingBudget", thinkingBudget},
        };
    }
    body["generationConfig"] = generationConfig;

    if (!tools.isEmpty())
        body["tools"] = convertTools(tools);

    QNetworkRequest request;
    request.setUrl(QUrl(buildStreamUrl(m_baseUrl, model)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    // 鉴权走请求头而不是 ?key= 查询参数：URL 会整串进日志与代理，key
    // 挂在 URL 上等于主动泄露。
    request.setRawHeader("x-goog-api-key", m_apiKey.toUtf8());
    request.setRawHeader("Accept", "text/event-stream");
    request.setTransferTimeout(120000);

    startReply(request, QJsonDocument(body).toJson());
}

bool GeminiService::streamComplete(const LLMRequestContext& ctx) const {
    const auto* state = static_cast<const GeminiRequestState*>(ctx.provider.data());
    return ctx.doneReceived || !ctx.content.isEmpty() || !state->toolCalls.isEmpty();
}

LLMResponse GeminiService::streamResult(const LLMRequestContext& ctx) const {
    const auto* state = static_cast<const GeminiRequestState*>(ctx.provider.data());
    LLMResponse response;
    response.success = true;
    response.content = ctx.content;
    response.reasoning = ctx.reasoning;
    response.tokensUsed = ctx.tokensUsed;
    response.promptTokens = state->promptTokens;
    response.completionTokens = state->completionTokens;
    response.toolCalls = state->toolCalls;
    // 拦截型 finishReason（isBlockedFinishReason 家族）且无任何输出：
    // 升级为错误（同 parseResponse）
    if (isBlockedFinishReason(state->finishReason)
            && response.content.isEmpty() && response.toolCalls.isEmpty()) {
        response.success = false;
        response.error = QStringLiteral("Response blocked by safety filters (")
                       + state->finishReason + QLatin1Char(')');
    }
    return response;
}

QString GeminiService::extractApiError(const LLMRequestContext& ctx,
                                       const QJsonObject& json) const {
    Q_UNUSED(ctx);
    if (!json.contains("error")) return {};
    return parseResponse(json).error;
}

bool GeminiService::isCompleteBody(const QJsonObject& json) const {
    // 完整（非流式）响应带 candidates；错误体只有 error。
    return json.contains("candidates");
}

void GeminiService::processLine(LLMRequestContext& ctx, const QByteArray& line) {
    auto* state = static_cast<GeminiRequestState*>(ctx.provider.data());

    // SSE 只有 "data:" 行带载荷
    if (!line.startsWith("data:")) return;

    const QString payload = QString::fromUtf8(line.mid(5).trimmed());
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payload.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) return;

    const QJsonObject json = doc.object();

    // 错误可能作为流内事件出现（或整个 4xx/5xx body 走到这里）；
    // errorEmitted 已置位时不再重复上报（基类回退梯子也据此短路，
    // 同一流内的多个 error 事件只报第一个）
    if (json.contains("error")) {
        if (!ctx.errorEmitted) {
            ctx.errorEmitted = true;
            emit errorOccurred(QStringLiteral("Gemini error: %1")
                                   .arg(parseResponse(json).error));
        }
        return;
    }

    // m-1（任务 #27）：请求侧安全拦截的流式形态——chunk 只有
    // promptFeedback.blockReason（无 error 无 candidates）。置
    // errorEmitted 并报明确的拦截原因，而不是流结束后落进基类
    // "Empty or invalid response" 的误导性回退。
    const QString blockReason = json.value("promptFeedback").toObject()
                                    .value("blockReason").toString();
    if (!blockReason.isEmpty() && !json.contains("candidates")) {
        if (!ctx.errorEmitted) {
            ctx.errorEmitted = true;
            emit errorOccurred(QStringLiteral(
                "Request blocked by safety filters (%1)").arg(blockReason));
        }
        return;
    }

    // 多候选场景只取第一个（本应用不配 candidateCount，服务端默认 1）
    const QJsonObject candidate =
        json.value("candidates").toArray().at(0).toObject();
    const QJsonArray parts =
        candidate.value("content").toObject().value("parts").toArray();
    for (const auto& part : parts) {
        const QJsonObject p = part.toObject();
        if (p.contains("functionCall")) {
            const QJsonObject call = p.value("functionCall").toObject();
            if (!call.value("name").toString().isEmpty()) {
                ToolCallInfo info;
                // id 合成规则与 parseResponse 一致（Gemini 无调用 id）
                info.id = QStringLiteral("gemini-call-%1")
                              .arg(state->toolCalls.size());
                info.name = call.value("name").toString();
                info.arguments = call.value("args").toObject();
                state->toolCalls.append(info);
            }
        } else if (p.value("thought").toBool()) {
            const QString text = p.value("text").toString();
            if (!text.isEmpty()) {
                ctx.reasoning += text;
                emit reasoningChunk(text);
            }
        } else if (p.contains("text")) {
            const QString text = p.value("text").toString();
            if (!text.isEmpty()) {
                ctx.content += text;
                emit streamingChunk(text);
            }
        }
    }

    // 最后一个 chunk 带 finishReason（STOP/MAX_TOKENS/SAFETY/...）——
    // 这是流结束信号（对齐 Anthropic 的 message_stop 语义）
    const QString finishReason = candidate.value("finishReason").toString();
    if (!finishReason.isEmpty()) {
        state->finishReason = finishReason;
        ctx.doneReceived = true;
    }

    // usageMetadata 在每个 chunk 上都是"迄今为止"的累积值：覆盖赋值，
    // 最后一个 chunk 的就是最终计数。
    const QJsonObject usage = json.value("usageMetadata").toObject();
    if (!usage.isEmpty()) {
        state->promptTokens = usage.value("promptTokenCount").toInt();
        state->completionTokens = usage.value("candidatesTokenCount").toInt();
        state->thoughtsTokens = usage.value("thoughtsTokenCount").toInt();
        ctx.tokensUsed = state->promptTokens + state->completionTokens
                       + state->thoughtsTokens;
    }
}

} // namespace NexusAI