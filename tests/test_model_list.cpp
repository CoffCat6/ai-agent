// 模型列表拉取的纯函数回归。
//
// 背景：设置页不该逼用户手打模型名（拼错一个字母要等聊天失败才发现）。
// 列举端点按线协议分化，响应形状也各不相同；这部分逻辑做成静态纯函数，
// 不依赖网络就能锁住行为：
//   1. modelsUrl：四种协议各自的列举端点、尾斜杠去重、空地址；
//   2. parseModelList：OpenAI 系 data[].id、Ollama models[].name、
//      Gemini models[].name（去 "models/" 前缀、只留 generateContent）、
//      纯字符串数组、去重、错误文案。
#include <QtTest>

#include "viewmodels/ConnectionTester.h"

using namespace NexusAI;

class TestModelList : public QObject {
    Q_OBJECT
private slots:
    void modelsUrlFollowsProtocol() {
        QCOMPARE(ConnectionTester::modelsUrl(
                     QStringLiteral("openai-chat"),
                     QStringLiteral("https://api.openai.com/v1")).toString(),
                 QStringLiteral("https://api.openai.com/v1/models"));
        QCOMPARE(ConnectionTester::modelsUrl(
                     QStringLiteral("openai-responses"),
                     QStringLiteral("https://api.openai.com/v1/")).toString(),
                 QStringLiteral("https://api.openai.com/v1/models"));
        QCOMPARE(ConnectionTester::modelsUrl(
                     QStringLiteral("anthropic-messages"),
                     QStringLiteral("https://api.anthropic.com")).toString(),
                 QStringLiteral("https://api.anthropic.com/v1/models"));
        QCOMPARE(ConnectionTester::modelsUrl(
                     QStringLiteral("gemini-generate"),
                     QStringLiteral("https://generativelanguage.googleapis.com/")).toString(),
                 QStringLiteral("https://generativelanguage.googleapis.com/v1beta/models"));
        QCOMPARE(ConnectionTester::modelsUrl(
                     QStringLiteral("ollama-chat"),
                     QStringLiteral("http://localhost:11434")).toString(),
                 QStringLiteral("http://localhost:11434/api/tags"));
        // 空地址返回空 URL，调用方据此直接报「先填服务地址」。
        QVERIFY(ConnectionTester::modelsUrl(QStringLiteral("openai-chat"),
                                            QStringLiteral("  ")).isEmpty());
    }

    void parsesOpenAiStyleList() {
        QString error;
        const QStringList names = ConnectionTester::parseModelList(
            QStringLiteral("openai-chat"),
            R"({"object":"list","data":[{"id":"gpt-4o"},{"id":"gpt-4o-mini"},
                {"id":"gpt-4o"}]})",
            &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(names, QStringList({QStringLiteral("gpt-4o"),
                                     QStringLiteral("gpt-4o-mini")}));
    }

    void parsesAnthropicStyleList() {
        // Anthropic 的列举响应与 OpenAI 同形（data[].id）。
        QString error;
        const QStringList names = ConnectionTester::parseModelList(
            QStringLiteral("anthropic-messages"),
            R"({"data":[{"id":"claude-sonnet-4-5"},{"id":"claude-3-5-haiku-20241022"}]})",
            &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(names.size(), 2);
        QCOMPARE(names.first(), QStringLiteral("claude-sonnet-4-5"));
    }

    void parsesOllamaTags() {
        QString error;
        const QStringList names = ConnectionTester::parseModelList(
            QStringLiteral("ollama-chat"),
            R"({"models":[{"name":"qwen2.5:7b"},{"name":"llama3.1:8b"}]})",
            &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(names, QStringList({QStringLiteral("qwen2.5:7b"),
                                     QStringLiteral("llama3.1:8b")}));
    }

    void parsesGeminiListAndDropsEmbeddingModels() {
        QString error;
        const QStringList names = ConnectionTester::parseModelList(
            QStringLiteral("gemini-generate"),
            R"({"models":[
                {"name":"models/gemini-2.5-flash",
                 "supportedGenerationMethods":["generateContent","countTokens"]},
                {"name":"models/text-embedding-004",
                 "supportedGenerationMethods":["embedContent"]},
                {"name":"models/gemini-2.5-pro",
                 "supportedGenerationMethods":["generateContent"]}]})",
            &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        // "models/" 前缀要去掉：能力表与 profile 里存的都是裸模型名。
        QCOMPARE(names, QStringList({QStringLiteral("gemini-2.5-flash"),
                                     QStringLiteral("gemini-2.5-pro")}));
    }

    void toleratesPlainStringArrays() {
        // 少数 OpenAI 兼容网关直接给 ["a","b"]。
        QString error;
        const QStringList names = ConnectionTester::parseModelList(
            QStringLiteral("openai-chat"), R"({"models":["m1","m2"]})", &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(names, QStringList({QStringLiteral("m1"), QStringLiteral("m2")}));
    }

    void reportsUnusableResponses() {
        QString error;
        QVERIFY(ConnectionTester::parseModelList(
            QStringLiteral("openai-chat"), "not json", &error).isEmpty());
        QVERIFY(!error.isEmpty());

        // 合法 JSON 但没有模型：错误文案要能直接显示给用户。
        error.clear();
        QVERIFY(ConnectionTester::parseModelList(
            QStringLiteral("openai-chat"), R"({"data":[]})", &error).isEmpty());
        QVERIFY(error.contains(QStringLiteral("没有返回任何模型")));
    }
};

QTEST_GUILESS_MAIN(TestModelList)
#include "test_model_list.moc"
