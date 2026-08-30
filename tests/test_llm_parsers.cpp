// Provider response parsers: plain text, tool calls, error bodies and
// protocol conversion for the Anthropic Messages API.
#include "infrastructure/llm/OpenAIService.h"
#include "infrastructure/llm/OllamaService.h"
#include "infrastructure/llm/AnthropicService.h"

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
    // ── OpenAI-compatible ─────────────────────────────
    void openaiPlainText();
    void openaiToolCalls();
    void openaiMalformedToolArguments();
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
    void anthropicConvertDropsLeadingAssistant();
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
    // Tool traffic is dropped, consecutive assistant turns merged
    QCOMPARE(out.messages.size(), 3);
    QCOMPARE(out.messages.at(0).toObject().value("role").toString(), QStringLiteral("user"));
    QCOMPARE(out.messages.at(0).toObject().value("content").toString(), QStringLiteral("question"));
    QCOMPARE(out.messages.at(1).toObject().value("content").toString(),
             QStringLiteral("part one\n\npart two\n\nlead in"));
    QCOMPARE(out.messages.at(2).toObject().value("content").toString(), QStringLiteral("again"));
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

QTEST_GUILESS_MAIN(TestLlmParsers)
#include "test_llm_parsers.moc"
