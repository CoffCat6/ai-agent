// Provider response parsers: plain text, tool calls, error bodies and
// protocol conversion for the Anthropic Messages API.
#include "infrastructure/llm/OpenAIService.h"
#include "infrastructure/llm/OllamaService.h"
#include "infrastructure/llm/AnthropicService.h"
#include "infrastructure/llm/GeminiService.h"
#include "infrastructure/llm/OpenAIResponsesService.h"
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

// 任务 #24：Gemini 流式解析同样通过受保护入口手工驱动。
class TestableGemini : public GeminiService {
public:
    using GeminiService::GeminiService;
    void feed(LLMRequestContext& ctx, const QByteArray& line) { processLine(ctx, line); }
    LLMResponse result(const LLMRequestContext& ctx) const { return streamResult(ctx); }
    ProviderRequestStatePtr newState() const { return createRequestState(); }
};

class TestableOpenAIResponses : public OpenAIResponsesService {
public:
    using OpenAIResponsesService::OpenAIResponsesService;
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

    // ── Gemini GenerateContent（任务 #24）──────────────
    void geminiConvertMessages();
    void geminiConvertToolCalls();
    void geminiConvertToolResults();
    void geminiConvertImages();
    void geminiParseTextAndUsage();
    void geminiParseToolCall();
    void geminiParseThoughtAndSafety();
    void geminiParseError();
    void geminiThinkingBudgetMapping();
    void geminiStreamingAccumulation();
    // 任务 #27：审查修复（M-1/M-2/m-1/m-2/s-2/s-3）配套用例
    void geminiThinkingBudgetClamping();
    void geminiConvertPlainTextToolResult();
    void geminiConvertUnknownToolCallId();
    void geminiParsePromptFeedbackBlock();
    void geminiStreamingBlockedUpgrade();
    void geminiStreamingInBandError();
    void geminiStreamingPromptFeedbackBlock();
    void geminiUrlDefense();
    void geminiChatCompleteRejectsBadModelName();

    // m-3：resetStreamState 必须清空 provider 侧的 tool 调用累积
    void resetStreamStateClearsGeminiToolCalls();
    void resetStreamStateClearsAnthropicToolUses();
    void resetStreamStateClearsOpenAIToolCalls();
    void resetStreamStateClearsResponsesCalls();

    // ── 思考强度：请求字段映射与思考正文分离 ──────────────
    void openaiReasoningEffortOmittedWhenUnset();
    void openaiReasoningEffortSentPerLevel();
    void openaiStreamingKeepsReasoningOutOfContent();
    void anthropicThinkingBudgetMapping();
    void anthropicStreamingThinkingBlockStaysOutOfContent();
    void ollamaThinkFlagMapping();
    void ollamaStreamingKeepsThinkingOutOfContent();

    // m-4：sanitizeSchema 补齐用例（共 11 个）
    void geminiSanitizeSchemaKeepsObject();
    void geminiSanitizeSchemaStripsUnsupported();
    void geminiSanitizeSchemaSkipsNonObject();
    void geminiSanitizeSchemaAllOf();
    void geminiSanitizeSchemaNestedCombinators();
    void geminiSanitizeSchemaEmpty();
    void geminiSanitizeSchemaDeepNesting();
    void geminiSanitizeSchemaAdditionalProperties();
    void geminiSanitizeSchemaNot();
    void geminiSanitizeSchemaPatternProperties();
    void geminiSanitizeSchemaItemsRecursiveFailure();
    void geminiConvertToolsSkipsUndegradable();
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

    // "off" 只是内部意图，OpenAI 的枚举里没有这个值（关闭叫 "none"）。
    // 原样发出去会被严格端点当非法取值拒掉，选"关闭"就整条请求报错。
    svc.setReasoningEffort(QStringLiteral("off"));
    QCOMPARE(svc.bodyFor(msgs).value("reasoning_effort").toString(),
             QStringLiteral("none"));
    QVERIFY(OpenAIService::reasoningEffortParam(QStringLiteral("bogus")).isEmpty());
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

// ── Gemini GenerateContent（任务 #24）─────────────────────

// system 折叠进 systemInstruction、assistant → model、连续同角色纯文本
// 轮合并、开头的 model 轮删除（对齐 Anthropic 模板行为）。
void TestLlmParsers::geminiConvertMessages() {
    const GeminiService::ConvertedRequest out = GeminiService::convertMessages(arrayOf(R"([
        {"role": "system", "content": "be brief"},
        {"role": "system", "content": "and kind"},
        {"role": "assistant", "content": "stale"},
        {"role": "user", "content": "question"},
        {"role": "assistant", "content": "part one"},
        {"role": "assistant", "content": "part two"},
        {"role": "user", "content": "again"}
    ])"));

    QCOMPARE(out.systemInstruction, QStringLiteral("be brief\n\nand kind"));
    QCOMPARE(out.contents.size(), 3);

    const QJsonObject user = out.contents.at(0).toObject();
    QCOMPARE(user.value("role").toString(), QStringLiteral("user"));
    QCOMPARE(user.value("parts").toArray().at(0).toObject()
                 .value("text").toString(), QStringLiteral("question"));

    // 连续 model 纯文本轮合并成一个 content 的单个 text part
    const QJsonObject model = out.contents.at(1).toObject();
    QCOMPARE(model.value("role").toString(), QStringLiteral("model"));
    const QJsonArray modelParts = model.value("parts").toArray();
    QCOMPARE(modelParts.size(), 1);
    QCOMPARE(modelParts.at(0).toObject().value("text").toString(),
             QStringLiteral("part one\n\npart two"));

    QCOMPARE(out.contents.at(2).toObject().value("parts").toArray().at(0)
                 .toObject().value("text").toString(), QStringLiteral("again"));
}

// assistant tool_calls → model 轮的 functionCall parts：arguments JSON
// 字符串解析为 args 对象；text 与 functionCall 共存于同一轮。
void TestLlmParsers::geminiConvertToolCalls() {
    const GeminiService::ConvertedRequest out = GeminiService::convertMessages(arrayOf(R"([
        {"role": "user", "content": "read it"},
        {"role": "assistant", "content": "checking",
         "tool_calls": [
             {"id": "call_9", "type": "function", "function": {
                 "name": "read_file",
                 "arguments": "{\"path\": \"/tmp/a.txt\"}"
             }},
             {"id": "call_10", "type": "function", "function": {
                 "name": "list_dir",
                 "arguments": "{\"path\": \"/x\"}"
             }}
         ]}
    ])"));

    QCOMPARE(out.contents.size(), 2);
    const QJsonObject model = out.contents.at(1).toObject();
    QCOMPARE(model.value("role").toString(), QStringLiteral("model"));
    const QJsonArray parts = model.value("parts").toArray();
    QCOMPARE(parts.size(), 3);   // text + 两个 functionCall

    QCOMPARE(parts.at(0).toObject().value("text").toString(), QStringLiteral("checking"));

    const QJsonObject call0 = parts.at(1).toObject().value("functionCall").toObject();
    QCOMPARE(call0.value("name").toString(), QStringLiteral("read_file"));
    QCOMPARE(call0.value("args").toObject().value("path").toString(),
             QStringLiteral("/tmp/a.txt"));

    const QJsonObject call1 = parts.at(2).toObject().value("functionCall").toObject();
    QCOMPARE(call1.value("name").toString(), QStringLiteral("list_dir"));
    QCOMPARE(call1.value("args").toObject().value("path").toString(),
             QStringLiteral("/x"));
}

// 连续 tool 结果 run-merge 进一个 user content 的多个 functionResponse
// parts；函数名从历史 assistant tool_calls 按 tool_call_id 反查（tool
// 消息本身只有 id）。
void TestLlmParsers::geminiConvertToolResults() {
    const GeminiService::ConvertedRequest out = GeminiService::convertMessages(arrayOf(R"([
        {"role": "user", "content": "go"},
        {"role": "assistant", "content": "", "tool_calls": [
            {"id": "call_1", "type": "function",
             "function": {"name": "read_file", "arguments": "{}"}},
            {"id": "call_2", "type": "function",
             "function": {"name": "list_dir", "arguments": "{}"}}
        ]},
        {"role": "tool", "tool_call_id": "call_1", "content": "{\"lines\": 3}"},
        {"role": "tool", "tool_call_id": "call_2", "content": "{\"files\": []}"},
        {"role": "user", "content": "thanks"}
    ])"));

    QCOMPARE(out.contents.size(), 4);   // user / model(functionCall) / user(结果) / user
    const QJsonObject resultTurn = out.contents.at(2).toObject();
    QCOMPARE(resultTurn.value("role").toString(), QStringLiteral("user"));
    const QJsonArray parts = resultTurn.value("parts").toArray();
    QCOMPARE(parts.size(), 2);          // 两个结果同轮送达

    const QJsonObject fr0 = parts.at(0).toObject().value("functionResponse").toObject();
    QCOMPARE(fr0.value("name").toString(), QStringLiteral("read_file"));
    QCOMPARE(fr0.value("response").toObject().value("lines").toInt(), 3);

    const QJsonObject fr1 = parts.at(1).toObject().value("functionResponse").toObject();
    QCOMPARE(fr1.value("name").toString(), QStringLiteral("list_dir"));
    QCOMPARE(fr1.value("response").toObject().value("files").toArray().size(), 0);
}

// images data URL → inlineData parts（mimeType/data 拆开），text part
// 在前；无 images 的 user 轮保持纯 text parts。
void TestLlmParsers::geminiConvertImages() {
    const QJsonObject user{
        {"role", "user"},
        {"content", "what is in this picture?"},
        {"images", QJsonArray{
            QStringLiteral("data:image/png;base64,QUJD")
        }}};
    const GeminiService::ConvertedRequest out = GeminiService::convertMessages({user});

    QCOMPARE(out.contents.size(), 1);
    const QJsonArray parts = out.contents.at(0).toObject().value("parts").toArray();
    QCOMPARE(parts.size(), 2);
    QCOMPARE(parts.at(0).toObject().value("text").toString(),
             QStringLiteral("what is in this picture?"));
    const QJsonObject inlineData = parts.at(1).toObject().value("inlineData").toObject();
    QCOMPARE(inlineData.value("mimeType").toString(), QStringLiteral("image/png"));
    QCOMPARE(inlineData.value("data").toString(), QStringLiteral("QUJD"));
}

// 非流式完整响应：text parts 拼接 + usageMetadata 映射（prompt/
// candidates/合计含 thoughts）+ finishReason=STOP 正常成功。
void TestLlmParsers::geminiParseTextAndUsage() {
    const LLMResponse r = GeminiService::parseResponse(jsonOf(R"({
        "candidates": [{
            "content": {"role": "model", "parts": [
                {"text": "Hello"}, {"text": "!"}
            ]},
            "finishReason": "STOP"
        }],
        "usageMetadata": {"promptTokenCount": 120, "candidatesTokenCount": 34,
                          "thoughtsTokenCount": 6}
    })"));
    QVERIFY(r.success);
    QCOMPARE(r.content, QStringLiteral("Hello!"));
    QVERIFY(r.toolCalls.isEmpty());
    QCOMPARE(r.promptTokens, 120);
    QCOMPARE(r.completionTokens, 34);
    QCOMPARE(r.tokensUsed, 160);   // 120 + 34 + 6
}

// functionCall parts → ToolCallInfo；Gemini 不返回调用 id，必须合成稳定
// 的 gemini-call-{序号}（Agent 层按 id 关联工具结果）。
void TestLlmParsers::geminiParseToolCall() {
    const LLMResponse r = GeminiService::parseResponse(jsonOf(R"({
        "candidates": [{
            "content": {"role": "model", "parts": [
                {"text": "Let me check."},
                {"functionCall": {"name": "read_file", "args": {"path": "/tmp/a"}}},
                {"functionCall": {"name": "list_dir", "args": {"path": "/x"}}}
            ]},
            "finishReason": "STOP"
        }]
    })"));
    QVERIFY(r.success);
    QCOMPARE(r.content, QStringLiteral("Let me check."));
    QCOMPARE(r.toolCalls.size(), 2);
    QCOMPARE(r.toolCalls.at(0).id, QStringLiteral("gemini-call-0"));
    QCOMPARE(r.toolCalls.at(0).name, QStringLiteral("read_file"));
    QCOMPARE(r.toolCalls.at(0).arguments.value("path").toString(), QStringLiteral("/tmp/a"));
    QCOMPARE(r.toolCalls.at(1).id, QStringLiteral("gemini-call-1"));
    QCOMPARE(r.toolCalls.at(1).name, QStringLiteral("list_dir"));
}

// thought:true 的 parts 走 reasoning 通道、不进 content；SAFETY 拦截且
// 无任何输出时升级为错误；MAX_TOKENS 截断时保留已有内容正常返回。
void TestLlmParsers::geminiParseThoughtAndSafety() {
    const LLMResponse withThought = GeminiService::parseResponse(jsonOf(R"({
        "candidates": [{
            "content": {"role": "model", "parts": [
                {"thought": true, "text": "step by step"},
                {"text": "answer"}
            ]},
            "finishReason": "STOP"
        }]
    })"));
    QVERIFY(withThought.success);
    QCOMPARE(withThought.reasoning, QStringLiteral("step by step"));
    QCOMPARE(withThought.content, QStringLiteral("answer"));

    // SAFETY 且无输出：不能静默给空响应
    const LLMResponse blocked = GeminiService::parseResponse(jsonOf(R"({
        "candidates": [{
            "content": {"role": "model", "parts": []},
            "finishReason": "SAFETY"
        }]
    })"));
    QVERIFY(!blocked.success);
    QVERIFY(blocked.error.contains(QStringLiteral("SAFETY")));

    // MAX_TOKENS 截断：内容仍有价值，正常返回
    const LLMResponse truncated = GeminiService::parseResponse(jsonOf(R"({
        "candidates": [{
            "content": {"role": "model", "parts": [{"text": "partial"}]},
            "finishReason": "MAX_TOKENS"
        }]
    })"));
    QVERIFY(truncated.success);
    QCOMPARE(truncated.content, QStringLiteral("partial"));

    // 无 candidates 的空体：失败而非"成功但空"
    QVERIFY(!GeminiService::parseResponse(jsonOf("{}")).success);
}

// 错误体 {"error":{code,message,status}} → success=false，message 带
// status 后缀（排查 INVALID_ARGUMENT / API_KEY_INVALID 一眼可见）。
void TestLlmParsers::geminiParseError() {
    const LLMResponse r = GeminiService::parseResponse(jsonOf(R"({
        "error": {"code": 400, "message": "API key not valid",
                  "status": "INVALID_ARGUMENT"}
    })"));
    QVERIFY(!r.success);
    QVERIFY(r.error.contains(QStringLiteral("API key not valid")));
    QVERIFY(r.error.contains(QStringLiteral("INVALID_ARGUMENT")));
}

// 思考档位表：与 Anthropic 对齐（low 2048 / medium 8192 / high 16384），
// off/未设置/未知 → 0（不发 thinkingConfig，服务端默认）。
void TestLlmParsers::geminiThinkingBudgetMapping() {
    QCOMPARE(GeminiService::thinkingBudgetFor(QString()), 0);
    QCOMPARE(GeminiService::thinkingBudgetFor(QStringLiteral("off")), 0);
    QCOMPARE(GeminiService::thinkingBudgetFor(QStringLiteral("bogus")), 0);
    QCOMPARE(GeminiService::thinkingBudgetFor(QStringLiteral("low")), 2048);
    QCOMPARE(GeminiService::thinkingBudgetFor(QStringLiteral("medium")), 8192);
    QCOMPARE(GeminiService::thinkingBudgetFor(QStringLiteral("high")), 16384);
}

// 流式路径：text/thought/functionCall 增量 + finishReason 置 done +
// usageMetadata 累积覆盖 + 通道分离（reasoning 永不进 streamingChunk）。
void TestLlmParsers::geminiStreamingAccumulation() {
    TestableGemini svc;
    LLMRequestContext ctx;
    ctx.provider = svc.newState();
    QSignalSpy answer(&svc, &IAIService::streamingChunk);
    QSignalSpy thought(&svc, &IAIService::reasoningChunk);

    svc.feed(ctx, QByteArrayLiteral(
        "data: {\"candidates\":[{\"content\":{\"role\":\"model\",\"parts\":"
        "[{\"thought\":true,\"text\":\"thinking\"}]}}],\"usageMetadata\":"
        "{\"promptTokenCount\":100,\"candidatesTokenCount\":5}}"));
    svc.feed(ctx, QByteArrayLiteral(
        "data: {\"candidates\":[{\"content\":{\"role\":\"model\",\"parts\":"
        "[{\"text\":\"Hi\"},{\"functionCall\":{\"name\":\"read_file\","
        "\"args\":{\"path\":\"/a\"}}}]}}],\"usageMetadata\":"
        "{\"promptTokenCount\":100,\"candidatesTokenCount\":20}}"));
    svc.feed(ctx, QByteArrayLiteral(
        "data: {\"candidates\":[{\"content\":{\"role\":\"model\",\"parts\":[]},"
        "\"finishReason\":\"STOP\"}],\"usageMetadata\":"
        "{\"promptTokenCount\":100,\"candidatesTokenCount\":25,"
        "\"thoughtsTokenCount\":10}}"));

    QVERIFY(ctx.doneReceived);
    const LLMResponse r = svc.result(ctx);
    QVERIFY(r.success);
    QCOMPARE(r.reasoning, QStringLiteral("thinking"));
    QCOMPARE(r.content, QStringLiteral("Hi"));
    QCOMPARE(r.toolCalls.size(), 1);
    QCOMPARE(r.toolCalls.at(0).id, QStringLiteral("gemini-call-0"));
    QCOMPARE(r.toolCalls.at(0).name, QStringLiteral("read_file"));
    QCOMPARE(r.toolCalls.at(0).arguments.value("path").toString(), QStringLiteral("/a"));
    // 累积 usageMetadata 取最后一 chunk 的值（覆盖而非累加）
    QCOMPARE(r.promptTokens, 100);
    QCOMPARE(r.completionTokens, 25);
    QCOMPARE(r.tokensUsed, 135);   // 100 + 25 + 10
    // 通道分离：思考增量只走 reasoningChunk，答案只走 streamingChunk
    QCOMPARE(answer.count(), 1);
    QCOMPARE(answer.at(0).at(0).toString(), QStringLiteral("Hi"));
    QCOMPARE(thought.count(), 1);
    QCOMPARE(thought.at(0).at(0).toString(), QStringLiteral("thinking"));
}

// M-1：thinkingBudget 与 maxOutputTokens 共池（Gemini 的输出上限包含
// 思考 token），档位超上限必须钳制、留不出最低预算必须降级为不发——
// 宁可少思考，也不能发必 400 的报文。
void TestLlmParsers::geminiThinkingBudgetClamping() {
    // medium/high 超过 maxOutputTokens-1024 时钳到上限（4096-1024=3072）
    QCOMPARE(GeminiService::clampedThinkingBudget(
                 QStringLiteral("medium"), 4096), 3072);
    QCOMPARE(GeminiService::clampedThinkingBudget(
                 QStringLiteral("high"), 4096), 3072);
    // 档位在池内时原样通过（不误伤正常配置）
    QCOMPARE(GeminiService::clampedThinkingBudget(
                 QStringLiteral("low"), 4096), 2048);
    QCOMPARE(GeminiService::clampedThinkingBudget(
                 QStringLiteral("medium"), 9216), 8192);
    QCOMPARE(GeminiService::clampedThinkingBudget(
                 QStringLiteral("high"), 32768), 16384);
    // 输出上限连最低预算都留不出来：降级为 0（不发 thinkingConfig）
    QCOMPARE(GeminiService::clampedThinkingBudget(
                 QStringLiteral("low"), 1500), 0);
    QCOMPARE(GeminiService::clampedThinkingBudget(
                 QStringLiteral("medium"), 1024), 0);
    // off/未设置/未知档位维持不发
    QCOMPARE(GeminiService::clampedThinkingBudget(QString(), 4096), 0);
    QCOMPARE(GeminiService::clampedThinkingBudget(
                 QStringLiteral("off"), 4096), 0);
    QCOMPARE(GeminiService::clampedThinkingBudget(
                 QStringLiteral("bogus"), 4096), 0);
}

// M-2：Agent 层三条真实路径（用户拒绝授权/重复调用跳过/封口 note）的
// tool content 是纯文本，必须包装成 {"result": 原文} 而非静默空 {}——
// 否则模型看不到"用户拒绝"语义可能重复调用被拒工具；合法 JSON 但非
// 对象（数组）同样包装；JSON 对象内容保持原样透传。
void TestLlmParsers::geminiConvertPlainTextToolResult() {
    const QString declined = QStringLiteral(
        "User declined execution of tool 'read_file'. "
        "Adapt your plan accordingly.");
    const QJsonArray messages = QJsonArray{
        QJsonObject{{"role", "user"}, {"content", "read it"}},
        QJsonObject{{"role", "assistant"}, {"content", ""},
            {"tool_calls", QJsonArray{QJsonObject{
                {"id", "call_1"}, {"type", "function"},
                {"function", QJsonObject{
                    {"name", "read_file"}, {"arguments", "{}"}}}
            }}}},
        QJsonObject{{"role", "tool"}, {"tool_call_id", "call_1"},
                    {"content", declined}},
    };
    const GeminiService::ConvertedRequest out =
        GeminiService::convertMessages(messages);

    QCOMPARE(out.contents.size(), 3);
    const QJsonObject fr = out.contents.at(2).toObject()
                               .value("parts").toArray().at(0).toObject()
                               .value("functionResponse").toObject();
    QCOMPARE(fr.value("name").toString(), QStringLiteral("read_file"));
    // 纯文本结果包成 {"result": 原文}，模型必须收到拒绝语义
    QCOMPARE(fr.value("response").toObject().value("result").toString(),
             declined);
    QCOMPARE(fr.value("response").toObject().size(), 1);

    // 合法 JSON 但非对象（数组）：同样包装，response 只要对象
    const GeminiService::ConvertedRequest arr = GeminiService::convertMessages(
        QJsonArray{
            QJsonObject{{"role", "user"}, {"content", "go"}},
            QJsonObject{{"role", "tool"}, {"tool_call_id", "c"},
                        {"content", "[1, 2]"}},
        });
    const QJsonObject frArr = arr.contents.at(1).toObject()
                                  .value("parts").toArray().at(0).toObject()
                                  .value("functionResponse").toObject();
    QCOMPARE(frArr.value("response").toObject().value("result").toString(),
             QStringLiteral("[1, 2]"));
}

// s-2 缺口 3：tool_call_id 反查不到历史 assistant tool_calls 时，
// functionResponse.name 退回 id 本身（报文结构仍可发）。
void TestLlmParsers::geminiConvertUnknownToolCallId() {
    const GeminiService::ConvertedRequest out = GeminiService::convertMessages(
        arrayOf(R"([
        {"role": "user", "content": "go"},
        {"role": "tool", "tool_call_id": "ghost_id", "content": "{\"ok\": true}"}
    ])"));
    QCOMPARE(out.contents.size(), 2);
    const QJsonObject fr = out.contents.at(1).toObject()
                               .value("parts").toArray().at(0).toObject()
                               .value("functionResponse").toObject();
    QCOMPARE(fr.value("name").toString(), QStringLiteral("ghost_id"));
    QCOMPARE(fr.value("response").toObject().value("ok").toBool(), true);
}

// m-1（非流式）：请求侧安全拦截（无 candidates 无 error，只有
// promptFeedback.blockReason）报明确错误；s-3：拦截 finishReason 家族
// 集合判断 + PROHIBITED 零输出也升级为错误。
void TestLlmParsers::geminiParsePromptFeedbackBlock() {
    const LLMResponse blocked = GeminiService::parseResponse(jsonOf(
        R"({"promptFeedback": {"blockReason": "SAFETY"}})"));
    QVERIFY(!blocked.success);
    QVERIFY(blocked.error.contains(
        QStringLiteral("Request blocked by safety filters")));
    QVERIFY(blocked.error.contains(QStringLiteral("SAFETY")));

    // s-3：六值拦截集合（SAFETY/RECITATION/PROHIBITED/BLOCKLIST/
    // IMAGE_SAFETY/SPII），正常结束与截断不属于拦截
    QVERIFY(GeminiService::isBlockedFinishReason(QStringLiteral("SAFETY")));
    QVERIFY(GeminiService::isBlockedFinishReason(QStringLiteral("RECITATION")));
    QVERIFY(GeminiService::isBlockedFinishReason(QStringLiteral("PROHIBITED")));
    QVERIFY(GeminiService::isBlockedFinishReason(QStringLiteral("BLOCKLIST")));
    QVERIFY(GeminiService::isBlockedFinishReason(QStringLiteral("IMAGE_SAFETY")));
    QVERIFY(GeminiService::isBlockedFinishReason(QStringLiteral("SPII")));
    QVERIFY(!GeminiService::isBlockedFinishReason(QStringLiteral("STOP")));
    QVERIFY(!GeminiService::isBlockedFinishReason(QStringLiteral("MAX_TOKENS")));
    QVERIFY(!GeminiService::isBlockedFinishReason(QString()));

    // PROHIBITED 零输出：升级为错误而非"成功但空内容"
    const LLMResponse prohibited = GeminiService::parseResponse(jsonOf(R"({
        "candidates": [{
            "content": {"role": "model", "parts": []},
            "finishReason": "PROHIBITED"
        }]
    })"));
    QVERIFY(!prohibited.success);
    QVERIFY(prohibited.error.contains(QStringLiteral("PROHIBITED")));
}

// s-2 缺口 1 + s-3（流式）：拦截型 finishReason 结束流且无任何输出时，
// streamResult 同样把空响应升级为错误（此前只测了非流式路径）。
void TestLlmParsers::geminiStreamingBlockedUpgrade() {
    TestableGemini svc;
    LLMRequestContext ctx;
    ctx.provider = svc.newState();

    svc.feed(ctx, QByteArrayLiteral(
        "data: {\"candidates\":[{\"content\":{\"role\":\"model\",\"parts\":[]},"
        "\"finishReason\":\"SAFETY\"}]}"));
    QVERIFY(ctx.doneReceived);
    const LLMResponse safety = svc.result(ctx);
    QVERIFY(!safety.success);
    QVERIFY(safety.error.contains(QStringLiteral("SAFETY")));

    // s-3：PROHIBITED 走同一集合判断
    LLMRequestContext ctx2;
    ctx2.provider = svc.newState();
    svc.feed(ctx2, QByteArrayLiteral(
        "data: {\"candidates\":[{\"content\":{\"role\":\"model\",\"parts\":[]},"
        "\"finishReason\":\"PROHIBITED\"}]}"));
    const LLMResponse prohibited = svc.result(ctx2);
    QVERIFY(!prohibited.success);
    QVERIFY(prohibited.error.contains(QStringLiteral("PROHIBITED")));
}

// s-2 缺口 2：流内 error 事件置 errorEmitted + 发 errorOccurred；同一
// 流内后续 error 事件不再重复上报（基类回退梯子也据此短路）。
void TestLlmParsers::geminiStreamingInBandError() {
    TestableGemini svc;
    LLMRequestContext ctx;
    ctx.provider = svc.newState();
    QSignalSpy errors(&svc, &IAIService::errorOccurred);

    svc.feed(ctx, QByteArrayLiteral(
        "data: {\"error\":{\"code\":429,\"message\":\"Resource exhausted\","
        "\"status\":\"RESOURCE_EXHAUSTED\"}}"));
    QCOMPARE(errors.count(), 1);
    QVERIFY(errors.at(0).at(0).toString()
                .contains(QStringLiteral("Resource exhausted")));
    QVERIFY(errors.at(0).at(0).toString()
                .contains(QStringLiteral("RESOURCE_EXHAUSTED")));
    QVERIFY(ctx.errorEmitted);

    // 第二个 error 事件：不重复上报
    svc.feed(ctx, QByteArrayLiteral(
        "data: {\"error\":{\"code\":429,\"message\":\"again\","
        "\"status\":\"RESOURCE_EXHAUSTED\"}}"));
    QCOMPARE(errors.count(), 1);
}

// m-1（流式）：请求侧拦截以 chunk 形态出现（无 error 无 candidates，
// 只有 promptFeedback.blockReason）——同样置 errorEmitted 并报明确的
// 拦截原因，而不是流结束后落进基类"Empty or invalid response"回退。
void TestLlmParsers::geminiStreamingPromptFeedbackBlock() {
    TestableGemini svc;
    LLMRequestContext ctx;
    ctx.provider = svc.newState();
    QSignalSpy errors(&svc, &IAIService::errorOccurred);

    svc.feed(ctx, QByteArrayLiteral(
        "data: {\"promptFeedback\":{\"blockReason\":\"SAFETY\"}}"));
    QCOMPARE(errors.count(), 1);
    QVERIFY(errors.at(0).at(0).toString().contains(
        QStringLiteral("Request blocked by safety filters")));
    QVERIFY(errors.at(0).at(0).toString().contains(QStringLiteral("SAFETY")));
    QVERIFY(ctx.errorEmitted);
}

// m-2：模型名白名单（trim + [A-Za-z0-9._:~-]）与 baseUrl 尾斜杠防御。
void TestLlmParsers::geminiUrlDefense() {
    // trim 后白名单内的名字原样通过（: 给 tag 风格命名、~ 给本地命名）
    QCOMPARE(GeminiService::sanitizeModelName(
                 QStringLiteral("  gemini-2.5-flash\n")),
             QStringLiteral("gemini-2.5-flash"));
    QCOMPARE(GeminiService::sanitizeModelName(QStringLiteral("my-model:latest")),
             QStringLiteral("my-model:latest"));
    QCOMPARE(GeminiService::sanitizeModelName(QStringLiteral("gemini-2.5-pro")),
             QStringLiteral("gemini-2.5-pro"));
    // 破坏 URL 结构的字符一律拒绝（? 截断 query、/ 改写 path、空白 %20、
    // # 截断 fragment）；空/纯空白同样拒绝
    QVERIFY(GeminiService::sanitizeModelName(
                 QStringLiteral("gemini?x")).isEmpty());
    QVERIFY(GeminiService::sanitizeModelName(
                 QStringLiteral("gemini 2.5")).isEmpty());
    QVERIFY(GeminiService::sanitizeModelName(
                 QStringLiteral("gemini/2.5")).isEmpty());
    QVERIFY(GeminiService::sanitizeModelName(
                 QStringLiteral("gemini#frag")).isEmpty());
    QVERIFY(GeminiService::sanitizeModelName(QString()).isEmpty());
    QVERIFY(GeminiService::sanitizeModelName(
                 QStringLiteral("   ")).isEmpty());

    const QString expected = QStringLiteral(
        "https://generativelanguage.googleapis.com/v1beta/models/"
        "gemini-2.5-flash:streamGenerateContent?alt=sse");
    // 无尾斜杠：原样拼接
    QCOMPARE(GeminiService::buildStreamUrl(
                 QStringLiteral("https://generativelanguage.googleapis.com"),
                 QStringLiteral("gemini-2.5-flash")),
             expected);
    // baseUrl 带尾斜杠（乃至多个）：去重后不再出现 "//v1beta"
    QCOMPARE(GeminiService::buildStreamUrl(
                 QStringLiteral("https://generativelanguage.googleapis.com/"),
                 QStringLiteral("gemini-2.5-flash")),
             expected);
    QCOMPARE(GeminiService::buildStreamUrl(
                 QStringLiteral("https://host//"),
                 QStringLiteral("m")),
             QStringLiteral(
                 "https://host/v1beta/models/m:streamGenerateContent?alt=sse"));
}

// m-2：模型名含非法字符时 chatComplete 提前 errorOccurred，不发结构
// 被破坏的 URL 请求。
void TestLlmParsers::geminiChatCompleteRejectsBadModelName() {
    TestableGemini svc;
    svc.setApiKey(QStringLiteral("test-key"));
    QSignalSpy errors(&svc, &IAIService::errorOccurred);

    svc.setModel(QStringLiteral("bad model/name"));
    svc.chatComplete(QJsonArray{
        QJsonObject{{"role", "user"}, {"content", "hi"}}});
    QCOMPARE(errors.count(), 1);
    QVERIFY(errors.at(0).at(0).toString()
                .contains(QStringLiteral("Invalid Gemini model name")));
    QVERIFY(errors.at(0).at(0).toString()
                .contains(QStringLiteral("bad model/name")));
}

// m-3（Gemini 流式解析路径）：resetStreamState 清空 functionCall 分片累积
// 后重试不重复。
void TestLlmParsers::resetStreamStateClearsGeminiToolCalls() {
    TestableGemini svc;
    LLMRequestContext ctx;
    ctx.provider = svc.newState();

    svc.feed(ctx, QByteArrayLiteral(
        "data: {\"candidates\":[{\"content\":{\"role\":\"model\",\"parts\":"
        "[{\"functionCall\":{\"name\":\"read_file\",\"args\":{\"path\":\"/a\"}}}]}}]}"));
    QCOMPARE(svc.result(ctx).toolCalls.size(), 1);

    ctx.resetStreamState();

    svc.feed(ctx, QByteArrayLiteral(
        "data: {\"candidates\":[{\"content\":{\"role\":\"model\",\"parts\":"
        "[{\"functionCall\":{\"name\":\"list_dir\",\"args\":{\"path\":\"/x\"}}}]}}]}"));
    const LLMResponse r = svc.result(ctx);
    QCOMPARE(r.toolCalls.size(), 1);
    QCOMPARE(r.toolCalls.first().name, QStringLiteral("list_dir"));
    QCOMPARE(r.toolCalls.first().id, QStringLiteral("gemini-call-0"));
}

// m-3（Anthropic toolUses 增量合并路径）：resetStreamState 同样清空 tool_use
// 分片累积，重试后不重复。
void TestLlmParsers::resetStreamStateClearsAnthropicToolUses() {
    TestableAnthropic svc;
        LLMRequestContext ctx;
        ctx.provider = svc.newState();

        svc.feed(ctx, QByteArrayLiteral(
            "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":"
            "{\"type\":\"tool_use\",\"id\":\"toolu_1\",\"name\":\"read_file\"}}"));
        svc.feed(ctx, QByteArrayLiteral(
            "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":"
            "{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"path\\\":\\\"/a\\\"}\"}}"));
        QCOMPARE(svc.result(ctx).toolCalls.size(), 1);

        ctx.resetStreamState();

        svc.feed(ctx, QByteArrayLiteral(
            "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":"
            "{\"type\":\"tool_use\",\"id\":\"toolu_2\",\"name\":\"list_dir\"}}"));
        svc.feed(ctx, QByteArrayLiteral(
            "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":"
            "{\"type\":\"input_json_delta\",\"partial_json\":\"{}\"}}"));
        const LLMResponse r = svc.result(ctx);
        QCOMPARE(r.toolCalls.size(), 1);
        QCOMPARE(r.toolCalls.first().name, QStringLiteral("list_dir"));
    }

    // m-3（OpenAI toolCalls 增量合并路径）：resetStreamState 清空 tool_call
    // 分片累积，重试后不重复。
    void TestLlmParsers::resetStreamStateClearsOpenAIToolCalls() {
        TestableOpenAI svc;
        LLMRequestContext ctx;
        ctx.provider = svc.newState();

        svc.feed(ctx, QByteArrayLiteral(
            "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call_1\","
            "\"function\":{\"name\":\"read_file\",\"arguments\":\"\"}}]}}]}"));
        svc.feed(ctx, QByteArrayLiteral(
            "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"arguments\":"
            "\"{\\\"path\\\":\\\"/a\\\"}\"}]}}]}"));
        QCOMPARE(svc.result(ctx).toolCalls.size(), 1);

        ctx.resetStreamState();

        svc.feed(ctx, QByteArrayLiteral(
            "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call_2\","
            "\"function\":{\"name\":\"list_dir\",\"arguments\":\"\"}}]}}]}"));
        svc.feed(ctx, QByteArrayLiteral(
            "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"arguments\":"
            "\"{}\"}]}}]}"));
        const LLMResponse r = svc.result(ctx);
        QCOMPARE(r.toolCalls.size(), 1);
        QCOMPARE(r.toolCalls.first().name, QStringLiteral("list_dir"));
    }

    // m-3（Responses API calls 增量合并路径）：resetStreamState 清空 call
    // 累积，重试后不重复。
    void TestLlmParsers::resetStreamStateClearsResponsesCalls() {
        TestableOpenAIResponses svc;
        LLMRequestContext ctx;
        ctx.provider = svc.newState();

        svc.feed(ctx, QByteArrayLiteral(
            "data: {\"type\":\"function_call\",\"item_id\":\"fc_1\",\"call_id\":\"call_1\","
            "\"name\":\"read_file\",\"arguments\":\"\"}"));
        svc.feed(ctx, QByteArrayLiteral(
            "data: {\"type\":\"function_call\",\"item_id\":\"fc_1\",\"arguments\":"
            "\"{\\\"path\\\":\\\"/a\\\"}\"}"));
        QCOMPARE(svc.result(ctx).toolCalls.size(), 1);

        ctx.resetStreamState();

        svc.feed(ctx, QByteArrayLiteral(
            "data: {\"type\":\"function_call\",\"item_id\":\"fc_2\",\"call_id\":\"call_2\","
            "\"name\":\"list_dir\",\"arguments\":\"\"}"));
        svc.feed(ctx, QByteArrayLiteral(
            "data: {\"type\":\"function_call\",\"item_id\":\"fc_2\",\"arguments\":"
            "\"{}\"}"));
        const LLMResponse r = svc.result(ctx);
        QCOMPARE(r.toolCalls.size(), 1);
        QCOMPARE(r.toolCalls.first().name, QStringLiteral("list_dir"));
    }

// m-4：Gemini 子集内的普通 object schema 原样保留（type/properties/required/
// description 不丢）。
void TestLlmParsers::geminiSanitizeSchemaKeepsObject() {
    bool ok = false;
    const QJsonObject s = GeminiService::sanitizeSchema(jsonOf(R"({
        "type": "object",
        "properties": {
            "query": {"type": "string", "description": "the query"}
        },
        "required": ["query"]
    })"), &ok);
    QVERIFY(ok);
    QCOMPARE(s.value("type").toString(), QStringLiteral("object"));
    const QJsonObject query =
        s.value("properties").toObject().value("query").toObject();
    QCOMPARE(query.value("type").toString(), QStringLiteral("string"));
    QCOMPARE(query.value("description").toString(), QStringLiteral("the query"));
    QVERIFY(s.value("required").toArray().contains(QStringLiteral("query")));
}

// m-4：不支持关键字被裁剪 / 组合器收敛——$ref 属性退化为无约束、anyOf/
// oneOf 收敛到第一个分支、约束性关键字（pattern/const）被裁。
void TestLlmParsers::geminiSanitizeSchemaStripsUnsupported() {
    bool ok = false;
    const QJsonObject s = GeminiService::sanitizeSchema(jsonOf(R"({
        "type": "object",
        "properties": {
            "a": {"$ref": "#/defs/A"},
            "b": {"anyOf": [{"type": "string"}, {"type": "number"}]},
            "c": {"type": "string", "enum": ["x", "y"], "pattern": "^x"},
            "d": {"oneOf": [{"type": "integer"}, {"type": "boolean"}]},
            "e": {"type": "array", "items": {"type": "string"}, "maxItems": 5}
        }
    })"), &ok);
    QVERIFY(ok);
    const QJsonObject props = s.value("properties").toObject();

    // $ref 被裁剪，退化为空（无约束）属性，不残留 $ref
    const QJsonObject a = props.value("a").toObject();
    QVERIFY(a.isEmpty());

    // anyOf 收敛到第一个分支 string
    const QJsonObject b = props.value("b").toObject();
    QCOMPARE(b.value("type").toString(), QStringLiteral("string"));
    QVERIFY(!b.contains("anyOf"));

    // pattern 裁剪，type/enum 保留
    const QJsonObject c = props.value("c").toObject();
    QCOMPARE(c.value("type").toString(), QStringLiteral("string"));
    QVERIFY(c.contains("enum"));
    QVERIFY(!c.contains("pattern"));

    // oneOf 收敛到第一个分支 integer
    QCOMPARE(props.value("d").toObject().value("type").toString(),
             QStringLiteral("integer"));

    // array items 保留，maxItems 裁剪
    const QJsonObject e = props.value("e").toObject();
    QCOMPARE(e.value("type").toString(), QStringLiteral("array"));
    QCOMPARE(e.value("items").toObject().value("type").toString(),
             QStringLiteral("string"));
    QVERIFY(!e.contains("maxItems"));
}

// m-4：顶层是明确的非对象标量/数组且无 properties 可收敛时无法安全降级，
// sanitizeSchema 返回空并置 *ok=false（调用方跳过该工具）。
void TestLlmParsers::geminiSanitizeSchemaSkipsNonObject() {
    bool ok = true;
    const QJsonObject s = GeminiService::sanitizeSchema(
        jsonOf(R"({"type": "string"})"), &ok);
    QVERIFY(!ok);
    QVERIFY(s.isEmpty());

    bool arrok = true;
    const QJsonObject arr = GeminiService::sanitizeSchema(
        jsonOf(R"({"type": "array", "items": {"type": "string"}})"), &arrok);
    QVERIFY(!arrok);
    QVERIFY(arr.isEmpty());

    // 顶层 $ref 虽被裁剪，但会收敛成无约束 object，仍可发（不跳过）
    bool refok = false;
    const QJsonObject ref = GeminiService::sanitizeSchema(
        jsonOf(R"({"$ref": "#/defs/ToolArgs"})"), &refok);
    QVERIFY(refok);
    QCOMPARE(ref.value("type").toString(), QStringLiteral("object"));
}

// m-4：allOf 组合器收敛到第一个可降级分支。
void TestLlmParsers::geminiSanitizeSchemaAllOf() {
    bool ok = false;
    const QJsonObject s = GeminiService::sanitizeSchema(jsonOf(R"({
        "type": "object",
        "properties": {
            "a": {"allOf": [{"type": "string"}, {"type": "number"}]}
        }
    })"), &ok);
    QVERIFY(ok);
    const QJsonObject a = s.value("properties").toObject().value("a").toObject();
    QCOMPARE(a.value("type").toString(), QStringLiteral("string"));
    QVERIFY(!a.contains("allOf"));
}

// m-4：嵌套组合器（anyOf 内嵌 anyOf）递归展开。
void TestLlmParsers::geminiSanitizeSchemaNestedCombinators() {
    bool ok = false;
    const QJsonObject s = GeminiService::sanitizeSchema(jsonOf(R"({
        "type": "object",
        "properties": {
            "a": {"anyOf": [{"anyOf": [{"type": "string"}]}, {"type": "number"}]}
        }
    })"), &ok);
    QVERIFY(ok);
    const QJsonObject a = s.value("properties").toObject().value("a").toObject();
    QCOMPARE(a.value("type").toString(), QStringLiteral("string"));
    QVERIFY(!a.contains("anyOf"));
}

// m-4：空 schema 收敛成无约束 object（type+空 properties）。
void TestLlmParsers::geminiSanitizeSchemaEmpty() {
    bool ok = false;
    const QJsonObject s = GeminiService::sanitizeSchema(jsonOf(R"({})"), &ok);
    QVERIFY(ok);
    QCOMPARE(s.value("type").toString(), QStringLiteral("object"));
    QVERIFY(s.contains("properties"));
    QVERIFY(s.value("properties").toObject().isEmpty());
}

// m-4：深层嵌套节点递归降级，所有约束被裁时返回 false。
void TestLlmParsers::geminiSanitizeSchemaDeepNesting() {
    bool ok = false;
    const QJsonObject s = GeminiService::sanitizeSchema(jsonOf(R"({
        "type": "object",
        "properties": {
            "a": {
                "type": "object",
                "properties": {
                    "b": {
                        "type": "object",
                        "properties": {
                            "c": {"$ref": "#/defs/X"}
                        }
                    }
                }
            }
        }
    })"), &ok);
    QVERIFY(ok);
    const QJsonObject a = s.value("properties").toObject().value("a").toObject();
    const QJsonObject propsA = a.value("properties").toObject();
    QVERIFY(!propsA.isEmpty());
    const QJsonObject b = propsA.value("b").toObject();
    const QJsonObject propsB = b.value("properties").toObject();
    QVERIFY(!propsB.isEmpty());
    const QJsonObject c = propsB.value("c").toObject();
    // $ref 被裁剪，c 变成空对象（无约束）
    QVERIFY(c.isEmpty());
}

// m-4：additionalProperties 被裁剪。
void TestLlmParsers::geminiSanitizeSchemaAdditionalProperties() {
    bool ok = false;
    const QJsonObject s = GeminiService::sanitizeSchema(jsonOf(R"({
        "type": "object",
        "additionalProperties": false
    })"), &ok);
    QVERIFY(ok);
    QVERIFY(!s.contains("additionalProperties"));
    QCOMPARE(s.value("type").toString(), QStringLiteral("object"));
}

// m-4：not 关键字被裁剪。
void TestLlmParsers::geminiSanitizeSchemaNot() {
    bool ok = false;
    const QJsonObject s = GeminiService::sanitizeSchema(jsonOf(R"({
        "type": "object",
        "not": {"type": "string"}
    })"), &ok);
    QVERIFY(ok);
    QVERIFY(!s.contains("not"));
}

// m-4：patternProperties 被裁剪。
void TestLlmParsers::geminiSanitizeSchemaPatternProperties() {
    bool ok = false;
    const QJsonObject s = GeminiService::sanitizeSchema(jsonOf(R"({
        "type": "object",
        "patternProperties": {
            "^x_": {"type": "string"}
        }
    })"), &ok);
    QVERIFY(ok);
    QVERIFY(!s.contains("patternProperties"));
}

// m-4：items 无法安全降级时（递归失败），父节点被裁。
void TestLlmParsers::geminiSanitizeSchemaItemsRecursiveFailure() {
    bool ok = true;
    const QJsonObject s = GeminiService::sanitizeSchema(jsonOf(R"({
        "type": "array",
        "items": {"$ref": "#/defs/X"}
    })"), &ok);
    // 顶层是数组且无可收敛 properties，无法安全降级
    QVERIFY(!ok);
    QVERIFY(s.isEmpty());
}

// m-4：convertTools 跳过无法安全降级的工具，保留可降级工具。
void TestLlmParsers::geminiConvertToolsSkipsUndegradable() {
    const QJsonArray tools = arrayOf(R"([
        {"type":"function","function":{"name":"ok","parameters":{"type":"object","properties":{"x":{"type":"string"}}}}},
        {"type":"function","function":{"name":"skip","parameters":{"type":"string"}}}
    ])");
    const QJsonArray decls = GeminiService::convertTools(tools);
    QCOMPARE(decls.size(), 1);
    const QJsonObject decl = decls.at(0).toObject();
    QCOMPARE(decl.value("name").toString(), QStringLiteral("ok"));
}


    QTEST_GUILESS_MAIN(TestLlmParsers)
#include "test_llm_parsers.moc"
