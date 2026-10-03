// Provider response parsers: plain text, tool calls, error bodies and
// protocol conversion for the Anthropic Messages API.
#include "infrastructure/llm/OpenAIService.h"
#include "infrastructure/llm/OllamaService.h"
#include "infrastructure/llm/AnthropicService.h"
#include "core/utils/DataUrlUtils.h"

#include <QtTest>
#include <QJsonDocument>

using namespace NexusAI;

namespace {

QJsonObject jsonOf(const char* raw) {
    return QJsonDocument::fromJson(QByteArray(raw)).object();
}
QJsonArray arrayOf(const char* raw) {
    return QJsonDocument::fromJson(QByteArray(raw)).array();
}

// Expose the protected streaming entry points so tests can drive one
// request's processLine sequence by hand and inspect streamResult.
class TestableOpenAI : public OpenAIService {
public:
    using OpenAIService::OpenAIService;
    void feed(LLMRequestContext& ctx, const QByteArray& line) { processLine(ctx, line); }
    LLMResponse result(const LLMRequestContext& ctx) const { return streamResult(ctx); }
    ProviderRequestStatePtr newState() const { return createRequestState(); }
    QJsonObject bodyFor(const QJsonArray& messages) { return buildRequestBody(messages, {}); }
};

class TestableOllama : public OllamaService {
public:
    using OllamaService::OllamaService;
    void feed(LLMRequestContext& ctx, const QByteArray& line) { processLine(ctx, line); }
    LLMResponse result(const LLMRequestContext& ctx) const { return streamResult(ctx); }
    ProviderRequestStatePtr newState() const { return createRequestState(); }
};

class TestableAnthropic : public AnthropicService {
public:
    using AnthropicService::AnthropicService;
    void feed(LLMRequestContext& ctx, const QByteArray& line) { processLine(ctx, line); }
    LLMResponse result(const LLMRequestContext& ctx) const { return streamResult(ctx); }
    ProviderRequestStatePtr newState() const { return createRequestState(); }
};

} // namespace

class TestLlmParsers : public QObject {
    Q_OBJECT

private slots:
    // Batch 30 (F6): images-carrying user messages convert to the provider
    // vision formats; messages without images stay untouched.
    void multimodalMessageConversion() {
        const QJsonObject user{
            {"role", "user"},
            {"content", "what is in this picture?"},
            {"images", QJsonArray{
                QStringLiteral("data:image/jpeg;base64,QUJD")}
            }};

        // OpenAI: multipart content array, non-standard field stripped.
        const QJsonArray openai = OpenAIService::convertMessagesForOpenAI({user});
        QCOMPARE(openai.size(), 1);
        const QJsonObject oMsg = openai.at(0).toObject();
        QVERIFY(!oMsg.contains("images"));
        const QJsonArray oContent = oMsg.value("content").toArray();
        QCOMPARE(oContent.size(), 2);
        QCOMPARE(oContent.at(0).toObject().value("type").toString(),
                 QStringLiteral("text"));
        QCOMPARE(oContent.at(1).toObject().value("type").toString(),
                 QStringLiteral("image_url"));
        QCOMPARE(oContent.at(1).toObject().value("image_url").toObject()
                     .value("url").toString(),
                 QStringLiteral("data:image/jpeg;base64,QUJD"));

        // Plain messages keep the string content form.
        const QJsonObject plain{{"role", "user"}, {"content", "hi"}};
        const QJsonArray openaiPlain =
            OpenAIService::convertMessagesForOpenAI({plain});
        QVERIFY(openaiPlain.at(0).toObject().value("content").isString());

        // Anthropic: image block with base64 source + text block.
        const auto anthropic = AnthropicService::convertMessages({user});
        QCOMPARE(anthropic.messages.size(), 1);
        const QJsonArray aContent =
            anthropic.messages.at(0).toObject().value("content").toArray();
        QCOMPARE(aContent.size(), 2);
        QCOMPARE(aContent.at(0).toObject().value("type").toString(),
                 QStringLiteral("image"));
        QCOMPARE(aContent.at(0).toObject().value("source").toObject()
                     .value("media_type").toString(),
                 QStringLiteral("image/jpeg"));
        QCOMPARE(aContent.at(0).toObject().value("source").toObject()
                     .value("data").toString(),
                 QStringLiteral("QUJD"));
        QCOMPARE(aContent.at(1).toObject().value("type").toString(),
                 QStringLiteral("text"));

        // Ollama: bare base64 payloads in "images".
        const QJsonArray ollama = OllamaService::convertMessagesForOllama({user});
        const QJsonArray bare = ollama.at(0).toObject().value("images").toArray();
        QCOMPARE(bare.size(), 1);
        QCOMPARE(bare.at(0).toString(), QStringLiteral("QUJD"));
        QCOMPARE(ollama.at(0).toObject().value("content").toString(),
                 QStringLiteral("what is in this picture?"));
    }

    // ── OpenAI-compatible ─────────────────────────────
    void openaiPlainText();
    void openaiToolCalls();
    void openaiMalformedToolArguments();
    void openaiStreamingAccumulation();
    void openaiNoChoices();
    void openaiErrorBodyHasNoChoices();
    // docs/16 §5.1: usage captured on the streaming path (include_usage)
    void openaiStreamingUsageCapture();
    void openaiRequestBodyIncludesStreamOptions();

    // ── Ollama ────────────────────────────────────────
    void ollamaPlainText();
    void ollamaNestedToolCalls();
    void ollamaLegacyToolCalls();
    void ollamaToolCallIdFallback();
    void ollamaStreamingUsageCapture();

    // ── Anthropic ─────────────────────────────────────
    void anthropicPlainText();
    void anthropicErrorPayload();
    void anthropicConvertMessages();
    void anthropicConvertToolCalls();
    void anthropicConvertDropsLeadingAssistant();
    void anthropicParseToolUse();
    void anthropicStreamingUsageCapture();

    // ── 思考强度：请求字段映射与思考正文分离 ──────────────
    void openaiReasoningEffortOmittedWhenUnset();
    void openaiReasoningEffortSentPerLevel();
    void openaiStreamingKeepsReasoningOutOfContent();
    void anthropicThinkingBudgetMapping();
    void anthropicStreamingThinkingBlockStaysOutOfContent();
    void ollamaThinkFlagMapping();
    void ollamaStreamingKeepsThinkingOutOfContent();
};

void TestLlmParsers::openaiPlainText() {
    const LLMResponse r = OpenAIService::parseResponse(jsonOf(R"({
        "choices": [{"message": {"role": "assistant", "content": "Hello!"}}],
        "usage": {"total_tokens": 42}
    })"));
    QVERIFY(r.success);
    QCOMPARE(r.content, QStringLiteral("Hello!"));
    QCOMPARE(r.tokensUsed, 42);
    QVERIFY(r.toolCalls.isEmpty());
}

void TestLlmParsers::openaiToolCalls() {
    const LLMResponse r = OpenAIService::parseResponse(jsonOf(R"({
        "choices": [{"message": {
            "role": "assistant",
            "content": null,
            "tool_calls": [{"id": "call_1", "type": "function", "function": {
                "name": "read_file",
                "arguments": "{\"path\": \"/tmp/a.txt\"}"
            }}]
        }}]
    })"));
    QVERIFY(r.success);
    QCOMPARE(r.toolCalls.size(), 1);
    QCOMPARE(r.toolCalls.first().id, QStringLiteral("call_1"));
    QCOMPARE(r.toolCalls.first().name, QStringLiteral("read_file"));
    QCOMPARE(r.toolCalls.first().arguments.value("path").toString(), QStringLiteral("/tmp/a.txt"));
}

void TestLlmParsers::openaiMalformedToolArguments() {
    // Malformed JSON arguments must not crash; the call survives with no args
    const LLMResponse r = OpenAIService::parseResponse(jsonOf(R"({
        "choices": [{"message": {"role": "assistant", "tool_calls": [{
            "id": "call_2", "type": "function",
            "function": {"name": "read_file", "arguments": "not-json"}
        }]}}]
    })"));
    QVERIFY(r.success);
    QCOMPARE(r.toolCalls.size(), 1);
    QVERIFY(r.toolCalls.first().arguments.isEmpty());
}

void TestLlmParsers::openaiStreamingAccumulation() {
    // Official wire style: complete name once, arguments as split fragments
    QString name = OpenAIService::accumulateName(QString(), QStringLiteral("search_knowledge_base"));
    QCOMPARE(name, QStringLiteral("search_knowledge_base"));
    name = OpenAIService::accumulateName(name, QStringLiteral(""));      // no name delta -> unchanged
    QCOMPARE(name, QStringLiteral("search_knowledge_base"));

    // Proxy bug seen in the wild: the complete name resent on every chunk
    name = OpenAIService::accumulateName(name, QStringLiteral("search_knowledge_base"));
    QCOMPARE(name, QStringLiteral("search_knowledge_base"));
    name = OpenAIService::accumulateName(name, QStringLiteral("search_knowledge_base"));
    QCOMPARE(name, QStringLiteral("search_knowledge_base"));

    // Genuine name split fragments still concatenate
    name = OpenAIService::accumulateName(QString(), QStringLiteral("search_kn"));
    name = OpenAIService::accumulateName(name, QStringLiteral("owledge_base"));
    QCOMPARE(name, QStringLiteral("search_knowledge_base"));

    // Arguments: genuine split fragments concatenate into one valid object
    QString args = OpenAIService::accumulateArguments(QString(), QStringLiteral("{\"query\": \"白"));
    args = OpenAIService::accumulateArguments(args, QStringLiteral("艳波\"}"));
    QCOMPARE(args, QStringLiteral("{\"query\": \"白艳波\"}"));

    // Proxy bug: complete arguments resent on every chunk (observed as
    // "{\"query\": ...}{\"query\": ...}" welded payloads)
    args = OpenAIService::accumulateArguments(QString(), QStringLiteral("{}"));
    args = OpenAIService::accumulateArguments(args, QStringLiteral("{}"));
    QCOMPARE(args, QStringLiteral("{}"));

    args = OpenAIService::accumulateArguments(QString(), QStringLiteral("{\"query\": \"a\"}"));
    args = OpenAIService::accumulateArguments(args, QStringLiteral("{\"query\": \"a\"}"));
    QCOMPARE(args, QStringLiteral("{\"query\": \"a\"}"));

    // Diverging complete payloads: the first complete document wins
    args = OpenAIService::accumulateArguments(QString(), QStringLiteral("{\"query\": \"白艳波\"}"));
    args = OpenAIService::accumulateArguments(args, QStringLiteral("{\"query\": \"教师唯一ID 739\"}"));
    QCOMPARE(args, QStringLiteral("{\"query\": \"白艳波\"}"));
}

void TestLlmParsers::openaiNoChoices() {
    const LLMResponse r = OpenAIService::parseResponse(jsonOf("{}"));
    QVERIFY(!r.success);
    QVERIFY(!r.error.isEmpty());
}

void TestLlmParsers::openaiErrorBodyHasNoChoices() {
    const LLMResponse r = OpenAIService::parseResponse(jsonOf(
        R"({"error": {"message": "invalid api key", "type": "auth_error"}})"));
    QVERIFY(!r.success);
}

// docs/16 §5.1: with stream_options.include_usage the final chunk (empty
// choices) carries the usage object; it must survive the empty-choices
// guard and land on the streamed LLMResponse.
void TestLlmParsers::openaiStreamingUsageCapture() {
    TestableOpenAI svc;
    LLMRequestContext ctx;
    ctx.provider = svc.newState();

    svc.feed(ctx, QByteArrayLiteral(
        "data: {\"choices\":[{\"delta\":{\"content\":\"Hi\"}}]}"));
    svc.feed(ctx, QByteArrayLiteral(
        "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":120,"
        "\"completion_tokens\":34,\"total_tokens\":154,"
        "\"prompt_tokens_details\":{\"cached_tokens\":40}}}"));
    svc.feed(ctx, QByteArrayLiteral("data: [DONE]"));

    QVERIFY(ctx.doneReceived);
    const LLMResponse r = svc.result(ctx);
    QVERIFY(r.success);
    QCOMPARE(r.content, QStringLiteral("Hi"));
    QCOMPARE(r.promptTokens, 120);
    QCOMPARE(r.completionTokens, 34);
    QCOMPARE(r.cachedTokens, 40);
}

void TestLlmParsers::openaiRequestBodyIncludesStreamOptions() {
    TestableOpenAI svc;
    const QJsonArray msgs{QJsonObject{{"role", "user"}, {"content", "hi"}}};
    const QJsonObject body = svc.bodyFor(msgs);
    QCOMPARE(body.value("stream").toBool(), true);
    QCOMPARE(body.value("stream_options").toObject()
                 .value("include_usage").toBool(), true);
}

void TestLlmParsers::ollamaPlainText() {
    const LLMResponse r = OllamaService::parseResponse(jsonOf(R"({
        "message": {"role": "assistant", "content": "Hi"},
        "eval_count": 10,
        "prompt_eval_count": 5
    })"));
    QVERIFY(r.success);
    QCOMPARE(r.content, QStringLiteral("Hi"));
    QCOMPARE(r.tokensUsed, 15);
}

void TestLlmParsers::ollamaNestedToolCalls() {
    const QVector<ToolCallInfo> calls = OllamaService::parseToolCalls(jsonOf(R"({
        "tool_calls": [{"function": {"name": "list_dir", "arguments": {"path": "/x"}}}]
    })"));
    QCOMPARE(calls.size(), 1);
    QCOMPARE(calls.first().name, QStringLiteral("list_dir"));
    QCOMPARE(calls.first().arguments.value("path").toString(), QStringLiteral("/x"));
}

void TestLlmParsers::ollamaLegacyToolCalls() {
    // Older Ollama builds used a flat {name, arguments} shape
    const QVector<ToolCallInfo> calls = OllamaService::parseToolCalls(jsonOf(R"({
        "tool_calls": [{"name": "list_dir", "arguments": {"path": "/y"}}]
    })"));
    QCOMPARE(calls.size(), 1);
    QCOMPARE(calls.first().name, QStringLiteral("list_dir"));
}

void TestLlmParsers::ollamaToolCallIdFallback() {
    // Ollama does not emit ids; the parser must synthesize stable ones
    const QVector<ToolCallInfo> calls = OllamaService::parseToolCalls(jsonOf(R"({
        "tool_calls": [
            {"function": {"name": "a", "arguments": {}}},
            {"function": {"name": "b", "arguments": {}}}
        ]
    })"));
    QCOMPARE(calls.size(), 2);
    QCOMPARE(calls[0].id, QStringLiteral("call_0"));
    QCOMPARE(calls[1].id, QStringLiteral("call_1"));
}

void TestLlmParsers::anthropicPlainText() {
    const LLMResponse r = AnthropicService::parseResponse(jsonOf(R"({
        "type": "message",
        "role": "assistant",
        "content": [{"type": "text", "text": "Hi there"}],
        "stop_reason": "end_turn",
        "usage": {"input_tokens": 10, "output_tokens": 5}
    })"));
    QVERIFY(r.success);
    QCOMPARE(r.content, QStringLiteral("Hi there"));
    QCOMPARE(r.tokensUsed, 15);
    QVERIFY(r.toolCalls.isEmpty());
}

void TestLlmParsers::anthropicErrorPayload() {
    const LLMResponse r = AnthropicService::parseResponse(jsonOf(R"({
        "type": "error",
        "error": {"type": "authentication_error", "message": "invalid x-api-key"}
    })"));
    QVERIFY(!r.success);
    QVERIFY(r.error.contains("invalid x-api-key"));
}

void TestLlmParsers::anthropicConvertMessages() {
    const AnthropicService::ConvertedHistory out = AnthropicService::convertMessages(arrayOf(R"([
        {"role": "system", "content": "be brief"},
        {"role": "user", "content": "question"},
        {"role": "assistant", "content": null, "tool_calls": [{"id": "call_1"}]},
        {"role": "tool", "tool_call_id": "call_1", "content": "result"},
        {"role": "assistant", "content": "part one"},
        {"role": "assistant", "content": "part two"},
        {"role": "assistant", "content": "lead in"},
        {"role": "user", "content": "again"}
    ])"));

    QCOMPARE(out.system, QStringLiteral("be brief"));
    // tool_calls -> assistant tool_use block, tool result -> user
    // tool_result block, consecutive assistant turns merged
    QCOMPARE(out.messages.size(), 5);
    QCOMPARE(out.messages.at(0).toObject().value("role").toString(), QStringLiteral("user"));
    QCOMPARE(out.messages.at(0).toObject().value("content").toString(), QStringLiteral("question"));

    const QJsonObject toolUseTurn = out.messages.at(1).toObject();
    QCOMPARE(toolUseTurn.value("role").toString(), QStringLiteral("assistant"));
    const QJsonArray useBlocks = toolUseTurn.value("content").toArray();
    QCOMPARE(useBlocks.size(), 1);
    QCOMPARE(useBlocks.at(0).toObject().value("type").toString(), QStringLiteral("tool_use"));
    QCOMPARE(useBlocks.at(0).toObject().value("id").toString(), QStringLiteral("call_1"));

    const QJsonObject resultTurn = out.messages.at(2).toObject();
    QCOMPARE(resultTurn.value("role").toString(), QStringLiteral("user"));
    const QJsonArray resultBlocks = resultTurn.value("content").toArray();
    QCOMPARE(resultBlocks.size(), 1);
    QCOMPARE(resultBlocks.at(0).toObject().value("type").toString(), QStringLiteral("tool_result"));
    QCOMPARE(resultBlocks.at(0).toObject().value("tool_use_id").toString(), QStringLiteral("call_1"));
    QCOMPARE(resultBlocks.at(0).toObject().value("content").toString(), QStringLiteral("result"));

    QCOMPARE(out.messages.at(3).toObject().value("content").toString(),
             QStringLiteral("part one\n\npart two\n\nlead in"));
    QCOMPARE(out.messages.at(4).toObject().value("content").toString(), QStringLiteral("again"));
}

// Full tool_calls payload: arguments arrive as a JSON string and must be
// handed to the API as the parsed input object.
void TestLlmParsers::anthropicConvertToolCalls() {
    const AnthropicService::ConvertedHistory out = AnthropicService::convertMessages(arrayOf(R"([
        {"role": "user", "content": "read it"},
        {"role": "assistant", "content": "",
         "tool_calls": [{"id": "call_9", "type": "function", "function": {
             "name": "read_file",
             "arguments": "{\"path\": \"/tmp/a.txt\"}"
         }}]}
    ])"));

    QCOMPARE(out.messages.size(), 2);
    const QJsonArray blocks = out.messages.at(1).toObject().value("content").toArray();
    QCOMPARE(blocks.size(), 1); // empty text is not emitted as a block
    const QJsonObject use = blocks.at(0).toObject();
    QCOMPARE(use.value("type").toString(), QStringLiteral("tool_use"));
    QCOMPARE(use.value("id").toString(), QStringLiteral("call_9"));
    QCOMPARE(use.value("name").toString(), QStringLiteral("read_file"));
    QCOMPARE(use.value("input").toObject().value("path").toString(), QStringLiteral("/tmp/a.txt"));
}

void TestLlmParsers::anthropicConvertDropsLeadingAssistant() {
    // The Messages API requires the conversation to open with a user turn
    const AnthropicService::ConvertedHistory out = AnthropicService::convertMessages(arrayOf(R"([
        {"role": "assistant", "content": "stale"},
        {"role": "user", "content": "hello"}
    ])"));
    QCOMPARE(out.messages.size(), 1);
    QCOMPARE(out.messages.at(0).toObject().value("role").toString(), QStringLiteral("user"));
}

// Non-streamed tool use: content blocks carry tool_use entries whose input
// is already a JSON object.
// docs/16 §5.1: the done=true final line carries the eval counts; they must
// land on the streamed LLMResponse as the direction split.
void TestLlmParsers::ollamaStreamingUsageCapture() {
    TestableOllama svc;
    LLMRequestContext ctx;
    ctx.provider = svc.newState();

    svc.feed(ctx, QByteArrayLiteral(
        "{\"message\":{\"content\":\"你好\"},\"done\":false}"));
    svc.feed(ctx, QByteArrayLiteral(
        "{\"message\":{},\"done\":true,\"prompt_eval_count\":100,\"eval_count\":25}"));

    QVERIFY(ctx.doneReceived);
    const LLMResponse r = svc.result(ctx);
    QVERIFY(r.success);
    QCOMPARE(r.content, QStringLiteral("你好"));
    QCOMPARE(r.promptTokens, 100);
    QCOMPARE(r.completionTokens, 25);
    QCOMPARE(r.tokensUsed, 125);
}

// docs/16 §5.1: message_start carries input + cache split, message_delta
// the cumulative output count; both must reach streamResult.
void TestLlmParsers::anthropicStreamingUsageCapture() {
    TestableAnthropic svc;
    LLMRequestContext ctx;
    ctx.provider = svc.newState();

    svc.feed(ctx, QByteArrayLiteral(
        "data: {\"type\":\"message_start\",\"message\":{\"usage\":"
        "{\"input_tokens\":150,\"cache_read_input_tokens\":60,"
        "\"cache_creation_input_tokens\":10}}}"));
    svc.feed(ctx, QByteArrayLiteral(
        "data: {\"type\":\"content_block_delta\",\"delta\":"
        "{\"type\":\"text_delta\",\"text\":\"Hi\"}}"));
    svc.feed(ctx, QByteArrayLiteral(
        "data: {\"type\":\"message_delta\",\"usage\":{\"output_tokens\":45}}"));
    svc.feed(ctx, QByteArrayLiteral("data: {\"type\":\"message_stop\"}"));

    QVERIFY(ctx.doneReceived);
    const LLMResponse r = svc.result(ctx);
    QVERIFY(r.success);
    QCOMPARE(r.content, QStringLiteral("Hi"));
    QCOMPARE(r.promptTokens, 150);
    QCOMPARE(r.completionTokens, 45);
    QCOMPARE(r.cacheReadTokens, 60);
    QCOMPARE(r.cacheCreationTokens, 10);
}

void TestLlmParsers::anthropicParseToolUse() {
    const LLMResponse r = AnthropicService::parseResponse(jsonOf(R"({
        "type": "message",
        "role": "assistant",
        "content": [
            {"type": "text", "text": "Let me check."},
            {"type": "tool_use", "id": "toolu_1", "name": "list_dir",
             "input": {"path": "/x"}}
        ],
        "stop_reason": "tool_use",
        "usage": {"input_tokens": 10, "output_tokens": 5}
    })"));
    QVERIFY(r.success);
    QCOMPARE(r.content, QStringLiteral("Let me check."));
    QCOMPARE(r.toolCalls.size(), 1);
    QCOMPARE(r.toolCalls.first().id, QStringLiteral("toolu_1"));
    QCOMPARE(r.toolCalls.first().name, QStringLiteral("list_dir"));
    QCOMPARE(r.toolCalls.first().arguments.value("path").toString(), QStringLiteral("/x"));
}

// ── 思考强度：请求字段映射 + 思考正文与答案分离 ──────────────

void TestLlmParsers::openaiReasoningEffortOmittedWhenUnset() {
    TestableOpenAI svc;
    const QJsonArray msgs{QJsonObject{{"role", "user"}, {"content", "hi"}}};
    const QJsonObject body = svc.bodyFor(msgs);
    // 档位未设置时必须整个字段不发：默认配置的行为不能因为这个功能存在而改变
    QVERIFY2(!body.contains("reasoning_effort"),
             "reasoning_effort must be absent when no level is configured");
}

void TestLlmParsers::openaiReasoningEffortSentPerLevel() {
    const QJsonArray msgs{QJsonObject{{"role", "user"}, {"content", "hi"}}};
    TestableOpenAI svc;
    svc.setSamplingParams(0.7, {});
    svc.setReasoningEffort(QStringLiteral("high"));
    const QJsonObject body = svc.bodyFor(msgs);
    QCOMPARE(body.value("reasoning_effort").toString(), QStringLiteral("high"));
    // 两个字段各自独立 opt-in，设了思考档位不该把 temperature 挤掉
    QCOMPARE(body.value("temperature").toDouble(), 0.7);
}

void TestLlmParsers::openaiStreamingKeepsReasoningOutOfContent() {
    TestableOpenAI svc;
    LLMRequestContext ctx;
    ctx.provider = svc.newState();
    QSignalSpy answer(&svc, &IAIService::streamingChunk);
    QSignalSpy thought(&svc, &IAIService::reasoningChunk);

    svc.feed(ctx, QByteArrayLiteral(
        "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"Let me think\"}}]}"));
    svc.feed(ctx, QByteArrayLiteral(
        "data: {\"choices\":[{\"delta\":{\"content\":\"42\"}}]}"));
    svc.feed(ctx, QByteArrayLiteral("data: [DONE]"));

    const LLMResponse r = svc.result(ctx);
    QCOMPARE(r.reasoning, QStringLiteral("Let me think"));
    QCOMPARE(r.content, QStringLiteral("42"));
    // 通道分离是硬要求：思考增量漏一个进 streamingChunk，就会被当成最终答案
    // 写进 summary 节点
    QCOMPARE(answer.count(), 1);
    QCOMPARE(answer.at(0).at(0).toString(), QStringLiteral("42"));
    QCOMPARE(thought.count(), 1);
    QCOMPARE(thought.at(0).at(0).toString(), QStringLiteral("Let me think"));
}

void TestLlmParsers::anthropicThinkingBudgetMapping() {
    QCOMPARE(AnthropicService::thinkingBudgetTokens(QString(), 4096), 0);
    QCOMPARE(AnthropicService::thinkingBudgetTokens(QStringLiteral("off"), 4096), 0);
    QCOMPARE(AnthropicService::thinkingBudgetTokens(QStringLiteral("bogus"), 4096), 0);
    QCOMPARE(AnthropicService::thinkingBudgetTokens(QStringLiteral("low"), 4096), 2048);
    // 思考预算与正文共享 max_tokens 这一池，档位大于余量时折算到上限——
    // 宁可少给预算，也不能发出会被 400 拒掉的报文
    QCOMPARE(AnthropicService::thinkingBudgetTokens(QStringLiteral("medium"), 4096), 3072);
    QCOMPARE(AnthropicService::thinkingBudgetTokens(QStringLiteral("high"), 32768), 16384);
    // 输出上限小得连最低预算都留不出来时退回"不思考"
    QCOMPARE(AnthropicService::thinkingBudgetTokens(QStringLiteral("low"), 1500), 0);
}

void TestLlmParsers::anthropicStreamingThinkingBlockStaysOutOfContent() {
    TestableAnthropic svc;
    LLMRequestContext ctx;
    ctx.provider = svc.newState();
    QSignalSpy answer(&svc, &IAIService::streamingChunk);
    QSignalSpy thought(&svc, &IAIService::reasoningChunk);

    svc.feed(ctx, QByteArrayLiteral(
        "data: {\"type\":\"content_block_start\","
        "\"content_block\":{\"type\":\"thinking\"}}"));
    svc.feed(ctx, QByteArrayLiteral(
        "data: {\"type\":\"content_block_delta\",\"delta\":"
        "{\"type\":\"thinking_delta\",\"thinking\":\"considering\"}}"));
    // signature 是 API 自校验用的签名，与展示无关，必须被忽略
    svc.feed(ctx, QByteArrayLiteral(
        "data: {\"type\":\"content_block_delta\",\"delta\":"
        "{\"type\":\"signature_delta\",\"signature\":\"sig_1\"}}"));
    svc.feed(ctx, QByteArrayLiteral(
        "data: {\"type\":\"content_block_delta\",\"delta\":"
        "{\"type\":\"text_delta\",\"text\":\"done\"}}"));

    const LLMResponse r = svc.result(ctx);
    QCOMPARE(r.reasoning, QStringLiteral("considering"));
    QCOMPARE(r.content, QStringLiteral("done"));
    QCOMPARE(thought.count(), 1);
    QCOMPARE(answer.count(), 1);
    QCOMPARE(answer.at(0).at(0).toString(), QStringLiteral("done"));

    const LLMResponse full = AnthropicService::parseResponse(jsonOf(R"({
        "type": "message", "role": "assistant",
        "content": [{"type": "thinking", "thinking": "step by step"},
                    {"type": "text", "text": "answer"}],
        "usage": {"input_tokens": 3, "output_tokens": 4}
    })"));
    QCOMPARE(full.reasoning, QStringLiteral("step by step"));
    QCOMPARE(full.content, QStringLiteral("answer"));
}

void TestLlmParsers::ollamaThinkFlagMapping() {
    // 未设置/陌生档位 → invalid QVariant，think 字段不发，退回模型默认
    QVERIFY(!OllamaService::thinkFlagForLevel(QString()).isValid());
    QVERIFY(!OllamaService::thinkFlagForLevel(QStringLiteral("bogus")).isValid());
    QCOMPARE(OllamaService::thinkFlagForLevel(QStringLiteral("off")).toBool(), false);
    // Ollama 只有开/关，没有强度分级：低中高统一落到 think=true
    QCOMPARE(OllamaService::thinkFlagForLevel(QStringLiteral("low")).toBool(), true);
    QCOMPARE(OllamaService::thinkFlagForLevel(QStringLiteral("medium")).toBool(), true);
    QCOMPARE(OllamaService::thinkFlagForLevel(QStringLiteral("high")).toBool(), true);
}

void TestLlmParsers::ollamaStreamingKeepsThinkingOutOfContent() {
    TestableOllama svc;
    LLMRequestContext ctx;
    ctx.provider = svc.newState();
    QSignalSpy answer(&svc, &IAIService::streamingChunk);
    QSignalSpy thought(&svc, &IAIService::reasoningChunk);

    svc.feed(ctx, QByteArrayLiteral(
        "{\"message\":{\"role\":\"assistant\",\"thinking\":\"hmm\"}}"));
    svc.feed(ctx, QByteArrayLiteral(
        "{\"message\":{\"role\":\"assistant\",\"content\":\"ok\"}}"));
    svc.feed(ctx, QByteArrayLiteral("{\"message\":{},\"done\":true}"));

    const LLMResponse r = svc.result(ctx);
    QCOMPARE(r.reasoning, QStringLiteral("hmm"));
    QCOMPARE(r.content, QStringLiteral("ok"));
    QCOMPARE(thought.count(), 1);
    QCOMPARE(answer.count(), 1);

    const LLMResponse full = OllamaService::parseResponse(jsonOf(R"({
        "message": {"role": "assistant", "thinking": "because", "content": "42"}
    })"));
    QCOMPARE(full.reasoning, QStringLiteral("because"));
    QCOMPARE(full.content, QStringLiteral("42"));
}

QTEST_GUILESS_MAIN(TestLlmParsers)
#include "test_llm_parsers.moc"
