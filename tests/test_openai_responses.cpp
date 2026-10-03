// OpenAI Responses 协议的报文转换与非流式解析（纯函数，不触网）。
// 流式事件解析在 test_provider_lifecycle 里用脚本化 SSE 服务器覆盖真实路径。
#include "infrastructure/llm/OpenAIResponsesService.h"

#include <QtTest>
#include <QJsonArray>
#include <QJsonObject>

using namespace NexusAI;

namespace {

// 报文体与错误判定都是 protected（与 OpenAIService 同一约定：格式只由这个类
// 知道，但要能被测试直接断言）。
class ExposedResponses : public OpenAIResponsesService {
public:
    using OpenAIResponsesService::buildRequestBody;
    using OpenAIResponsesService::extractApiError;
    using OpenAIResponsesService::isCompleteBody;
};

QJsonObject message(const QString& role, const QString& content) {
    return QJsonObject{{QStringLiteral("role"), role}, {QStringLiteral("content"), content}};
}

} // namespace

class TestOpenAIResponses : public QObject {
    Q_OBJECT

private slots:
    // system/developer 抽进顶层 instructions，不再作为 input item。
    void systemTurnsCollapseIntoInstructions() {
        QJsonArray messages;
        messages.append(message(QStringLiteral("system"), QStringLiteral("规则A")));
        messages.append(message(QStringLiteral("developer"), QStringLiteral("规则B")));
        messages.append(message(QStringLiteral("user"), QStringLiteral("你好")));

        QCOMPARE(OpenAIResponsesService::instructionsFromMessages(messages),
                 QStringLiteral("规则A\n\n规则B"));

        const QJsonArray input = OpenAIResponsesService::convertMessagesToInput(messages);
        QCOMPARE(input.size(), 1);
        QCOMPARE(input.at(0).toObject().value("role").toString(), QStringLiteral("user"));
        QCOMPARE(input.at(0).toObject().value("content").toString(), QStringLiteral("你好"));
    }

    // agent 侧用的是 Chat 报文：assistant.tool_calls 与 role:"tool" 结果要
    // 变成成对的 function_call / function_call_output item，靠 call_id 关联。
    void toolHistoryPairsByCallId() {
        QJsonObject call;
        call[QStringLiteral("id")] = QStringLiteral("call_9");
        call[QStringLiteral("type")] = QStringLiteral("function");
        call[QStringLiteral("function")] = QJsonObject{
            {QStringLiteral("name"), QStringLiteral("get_weather")},
            {QStringLiteral("arguments"), QStringLiteral("{\"city\":\"Paris\"}")}};

        QJsonArray messages;
        QJsonObject assistant = message(QStringLiteral("assistant"), QStringLiteral("先看一眼"));
        assistant[QStringLiteral("tool_calls")] = QJsonArray{call};
        messages.append(assistant);
        QJsonObject toolResult = message(QStringLiteral("tool"), QStringLiteral("晴"));
        toolResult[QStringLiteral("tool_call_id")] = QStringLiteral("call_9");
        messages.append(toolResult);

        const QJsonArray input = OpenAIResponsesService::convertMessagesToInput(messages);
        QCOMPARE(input.size(), 3);

        const QJsonObject text = input.at(0).toObject();
        QCOMPARE(text.value("role").toString(), QStringLiteral("assistant"));
        QCOMPARE(text.value("content").toString(), QStringLiteral("先看一眼"));

        const QJsonObject fc = input.at(1).toObject();
        QCOMPARE(fc.value("type").toString(), QStringLiteral("function_call"));
        QCOMPARE(fc.value("call_id").toString(), QStringLiteral("call_9"));
        QCOMPARE(fc.value("name").toString(), QStringLiteral("get_weather"));
        // arguments 必须是 JSON 字符串；给成对象会被 API 拒掉
        QVERIFY(fc.value("arguments").isString());
        QCOMPARE(fc.value("arguments").toString(), QStringLiteral("{\"city\":\"Paris\"}"));

        const QJsonObject out = input.at(2).toObject();
        QCOMPARE(out.value("type").toString(), QStringLiteral("function_call_output"));
        QCOMPARE(out.value("call_id").toString(), QStringLiteral("call_9"));
        QCOMPARE(out.value("output").toString(), QStringLiteral("晴"));
    }

    // 没有正文只有工具调用的 assistant 轮次不该产出空 message item（空 content
    // 的 assistant item 会被 API 拒）。
    void emptyAssistantTurnEmitsOnlyCalls() {
        QJsonObject call;
        call[QStringLiteral("id")] = QStringLiteral("c1");
        call[QStringLiteral("function")] = QJsonObject{
            {QStringLiteral("name"), QStringLiteral("f")},
            {QStringLiteral("arguments"), QStringLiteral("{}")}};
        QJsonObject assistant;
        assistant[QStringLiteral("role")] = QStringLiteral("assistant");
        assistant[QStringLiteral("content")] = QStringLiteral("");
        assistant[QStringLiteral("tool_calls")] = QJsonArray{call};

        const QJsonArray input = OpenAIResponsesService::convertMessagesToInput(
            QJsonArray{assistant});
        QCOMPARE(input.size(), 1);
        QCOMPARE(input.at(0).toObject().value("type").toString(),
                 QStringLiteral("function_call"));
    }

    // Chat 的 arguments 偶尔以对象形态进入历史（自建网关），要序列化成字符串。
    void objectArgumentsAreStringified() {
        QJsonObject call;
        call[QStringLiteral("id")] = QStringLiteral("c2");
        call[QStringLiteral("function")] = QJsonObject{
            {QStringLiteral("name"), QStringLiteral("f")},
            {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("k"), 1}}}};
        QJsonObject assistant;
        assistant[QStringLiteral("role")] = QStringLiteral("assistant");
        assistant[QStringLiteral("tool_calls")] = QJsonArray{call};

        const QJsonArray input = OpenAIResponsesService::convertMessagesToInput(
            QJsonArray{assistant});
        QCOMPARE(input.at(0).toObject().value("arguments").toString(),
                 QStringLiteral("{\"k\":1}"));
    }

    void imagesBecomeInputImageParts() {
        QJsonObject user = message(QStringLiteral("user"), QStringLiteral("看这张"));
        user[QStringLiteral("images")] = QJsonArray{QStringLiteral("data:image/png;base64,AAA")};

        const QJsonArray input = OpenAIResponsesService::convertMessagesToInput(
            QJsonArray{user});
        const QJsonObject item = input.at(0).toObject();
        QVERIFY(!item.contains(QStringLiteral("images")));
        const QJsonArray parts = item.value("content").toArray();
        QCOMPARE(parts.size(), 2);
        QCOMPARE(parts.at(0).toObject().value("type").toString(), QStringLiteral("input_text"));
        const QJsonObject image = parts.at(1).toObject();
        QCOMPARE(image.value("type").toString(), QStringLiteral("input_image"));
        // Responses 里 image_url 是字符串，不是 Chat 的 {url:...} 对象
        QCOMPARE(image.value("image_url").toString(),
                 QStringLiteral("data:image/png;base64,AAA"));
    }

    void toolsLoseTheChatFunctionWrapper() {
        QJsonObject parameters;
        parameters[QStringLiteral("type")] = QStringLiteral("object");

        QJsonObject fn;
        fn[QStringLiteral("name")] = QStringLiteral("search");
        fn[QStringLiteral("description")] = QStringLiteral("搜");
        fn[QStringLiteral("parameters")] = parameters;

        QJsonObject wrapped;
        wrapped[QStringLiteral("type")] = QStringLiteral("function");
        wrapped[QStringLiteral("function")] = fn;

        const QJsonArray flat = OpenAIResponsesService::convertTools(
            QJsonArray{wrapped});
        QCOMPARE(flat.size(), 1);
        const QJsonObject tool = flat.at(0).toObject();
        QVERIFY(!tool.contains(QStringLiteral("function")));
        QCOMPARE(tool.value("type").toString(), QStringLiteral("function"));
        QCOMPARE(tool.value("name").toString(), QStringLiteral("search"));
        QCOMPARE(tool.value("description").toString(), QStringLiteral("搜"));
        // 显式 non-strict：本项目的 schema 不满足 strict 的 required/
        // additionalProperties 约束，省略该字段会被服务端自动规范化并 400。
        QVERIFY(tool.contains(QStringLiteral("strict")));
        QCOMPARE(tool.value("strict").toBool(), false);
    }

    void reasoningEffortMapsOffToNone() {
        QCOMPARE(OpenAIResponsesService::reasoningEffortParam(QStringLiteral("off")),
                 QStringLiteral("none"));
        QCOMPARE(OpenAIResponsesService::reasoningEffortParam(QStringLiteral("high")),
                 QStringLiteral("high"));
        QCOMPARE(OpenAIResponsesService::reasoningEffortParam(QStringLiteral("bogus")),
                 QString());
    }

    // 无状态契约 + "未设置就不发字段"语义 + 输出上限下限钳制。
    void requestBodyCarriesTheStatelessContract() {
        ExposedResponses service;
        service.setModel(QStringLiteral("gpt-4o-mini"));
        const QJsonArray messages{message(QStringLiteral("system"), QStringLiteral("规则")),
                                  message(QStringLiteral("user"), QStringLiteral("hi"))};

        QJsonObject body = service.buildRequestBody(messages, QJsonArray());
        QCOMPARE(body.value("model").toString(), QStringLiteral("gpt-4o-mini"));
        QCOMPARE(body.value("stream").toBool(), true);
        QCOMPARE(body.value("store").toBool(), false);
        QCOMPARE(body.value("truncation").toString(), QStringLiteral("auto"));
        QCOMPARE(body.value("instructions").toString(), QStringLiteral("规则"));
        QVERIFY(body.contains("input"));
        // 未设置的采样参数不能出现在请求里（严格端点会拒空字段）
        QVERIFY(!body.contains("temperature"));
        QVERIFY(!body.contains("max_output_tokens"));
        QVERIFY(!body.contains("reasoning"));
        QVERIFY(!body.contains("tools"));

        service.setSamplingParams(QVariant(0.3), QVariant(5));
        body = service.buildRequestBody(messages, QJsonArray());
        QCOMPARE(body.value("temperature").toDouble(), 0.3);
        // max_output_tokens 含推理 token，低于 API 下限会直接 incomplete
        QCOMPARE(body.value("max_output_tokens").toInt(), 16);

        service.setSamplingParams(QVariant(), QVariant(8192));
        body = service.buildRequestBody(messages, QJsonArray());
        QVERIFY(!body.contains("temperature"));
        QCOMPARE(body.value("max_output_tokens").toInt(), 8192);

        service.setReasoningEffort(QStringLiteral("off"));
        body = service.buildRequestBody(messages, QJsonArray());
        QCOMPARE(body.value("reasoning").toObject().value("effort").toString(),
                 QStringLiteral("none"));
    }

    void nonStreamBodyParsesTextCallsReasoningAndUsage() {
        const QJsonObject json{
            {QStringLiteral("id"), QStringLiteral("resp_1")},
            {QStringLiteral("object"), QStringLiteral("response")},
            {QStringLiteral("status"), QStringLiteral("completed")},
            {QStringLiteral("output"), QJsonArray{
                QJsonObject{{QStringLiteral("type"), QStringLiteral("reasoning")},
                            {QStringLiteral("summary"), QJsonArray{QJsonObject{
                                {QStringLiteral("type"), QStringLiteral("summary_text")},
                                {QStringLiteral("text"), QStringLiteral("先想一下")}}}}},
                QJsonObject{{QStringLiteral("type"), QStringLiteral("message")},
                            {QStringLiteral("content"), QJsonArray{QJsonObject{
                                {QStringLiteral("type"), QStringLiteral("output_text")},
                                {QStringLiteral("text"), QStringLiteral("答案")}}}}},
                QJsonObject{{QStringLiteral("type"), QStringLiteral("function_call")},
                            {QStringLiteral("id"), QStringLiteral("fc_1")},
                            {QStringLiteral("call_id"), QStringLiteral("call_7")},
                            {QStringLiteral("name"), QStringLiteral("lookup")},
                            {QStringLiteral("arguments"), QStringLiteral("{\"q\":\"x\"}")}}}},
            {QStringLiteral("usage"), QJsonObject{
                {QStringLiteral("input_tokens"), 120},
                {QStringLiteral("input_tokens_details"), QJsonObject{{QStringLiteral("cached_tokens"), 40}}},
                {QStringLiteral("output_tokens"), 30},
                {QStringLiteral("output_tokens_details"), QJsonObject{{QStringLiteral("reasoning_tokens"), 10}}},
                {QStringLiteral("total_tokens"), 150}}}};

        const LLMResponse parsed = OpenAIResponsesService::parseResponse(json);
        QVERIFY(parsed.success);
        QCOMPARE(parsed.content, QStringLiteral("答案"));
        QCOMPARE(parsed.reasoning, QStringLiteral("先想一下"));
        QCOMPARE(parsed.toolCalls.size(), 1);
        // id 必须是 call_id：下一轮要按它回传 function_call_output
        QCOMPARE(parsed.toolCalls.at(0).id, QStringLiteral("call_7"));
        QCOMPARE(parsed.toolCalls.at(0).name, QStringLiteral("lookup"));
        QCOMPARE(parsed.toolCalls.at(0).arguments.value("q").toString(), QStringLiteral("x"));
        QCOMPARE(parsed.promptTokens, 120);
        QCOMPARE(parsed.completionTokens, 30);
        QCOMPARE(parsed.cachedTokens, 40);
        QCOMPARE(parsed.tokensUsed, 150);
        // input_tokens 已经把缓存算进去了，Anthropic 那两桶必须留空，否则
        // AIAgent 相加会双计。
        QCOMPARE(parsed.cacheReadTokens, 0);
        QCOMPARE(parsed.cacheCreationTokens, 0);
    }

    // 成功的响应体里 error 恒为 null，不能被当成错误上抛（非流式回退梯子会先
    // 过 extractApiError）。
    void nullErrorFieldIsNotAnError() {
        ExposedResponses service;
        const LLMRequestContext ctx;
        const QJsonObject json{
            {QStringLiteral("object"), QStringLiteral("response")},
            {QStringLiteral("status"), QStringLiteral("completed")},
            {QStringLiteral("error"), QJsonValue::Null},
            {QStringLiteral("output"), QJsonArray{QJsonObject{
                {QStringLiteral("type"), QStringLiteral("message")},
                {QStringLiteral("content"), QJsonArray{QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("output_text")},
                    {QStringLiteral("text"), QStringLiteral("ok")}}}}}}}};
        QVERIFY(service.extractApiError(ctx, json).isEmpty());
        QVERIFY(service.isCompleteBody(json));
    }

    void failedBodyReportsAnError() {
        const QJsonObject json{
            {QStringLiteral("object"), QStringLiteral("response")},
            {QStringLiteral("status"), QStringLiteral("failed")},
            {QStringLiteral("error"), QJsonObject{
                {QStringLiteral("type"), QStringLiteral("invalid_request_error")},
                {QStringLiteral("message"), QStringLiteral("bad input")}}},
            {QStringLiteral("output"), QJsonArray{}}};
        const LLMResponse parsed = OpenAIResponsesService::parseResponse(json);
        QVERIFY(!parsed.success);
        QVERIFY(parsed.error.contains(QStringLiteral("failed")));
    }
};

QTEST_GUILESS_MAIN(TestOpenAIResponses)
#include "test_openai_responses.moc"
