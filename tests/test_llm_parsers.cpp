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

    // ── Ollama ────────────────────────────────────────
    void ollamaPlainText();
    void ollamaNestedToolCalls();
    void ollamaLegacyToolCalls();
    void ollamaToolCallIdFallback();

    // ── Anthropic ─────────────────────────────────────
    void anthropicPlainText();
    void anthropicErrorPayload();
    void anthropicConvertMessages();
    void anthropicConvertToolCalls();
    void anthropicConvertDropsLeadingAssistant();
    void anthropicParseToolUse();
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

QTEST_GUILESS_MAIN(TestLlmParsers)
#include "test_llm_parsers.moc"
