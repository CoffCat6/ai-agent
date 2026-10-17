// 协议注册表：描述符完整性、profile 归一化回退、按协议装配服务集合。
#include "infrastructure/llm/IAIService.h"
#include "infrastructure/llm/LlmProtocolRegistry.h"
#include "infrastructure/llm/GeminiService.h"

#include <QtTest>

using namespace NexusAI;

class TestProtocolRegistry : public QObject {
    Q_OBJECT

private slots:
    void descriptorsAreComplete() {
        const auto& table = LlmProtocolRegistry::descriptors();
        QVERIFY(!table.isEmpty());

        QStringList keys;
        for (const auto& d : table) {
            QVERIFY(!d.key.isEmpty());
            QVERIFY(!d.provider.isEmpty());
            QVERIFY(!d.defaultBaseUrl.isEmpty());
            QVERIFY(!d.defaultModel.isEmpty());
            QVERIFY(d.models.contains(d.defaultModel));
            QVERIFY(d.contextWindows.contains(d.defaultModel));
            QVERIFY(d.createService != nullptr);
            // 本地协议没有 key 概念，不该带环境变量
            if (d.local) QVERIFY(d.envApiKeyVar.isEmpty());
            else QVERIFY(!d.envApiKeyVar.isEmpty());
            QVERIFY(!keys.contains(d.key));
            keys.append(d.key);
        }
    }

    void findOnlyAcceptsRegisteredKeys() {
        QVERIFY(LlmProtocolRegistry::find(QString::fromLatin1(LlmProtocol::kOpenaiChat)));
        QVERIFY(LlmProtocolRegistry::find(QString::fromLatin1(LlmProtocol::kOllamaChat)));
        QVERIFY(LlmProtocolRegistry::find(QStringLiteral("openai-responses")));
        QVERIFY(LlmProtocolRegistry::find(QStringLiteral("openai-anthropic")) == nullptr);
        QVERIFY(LlmProtocolRegistry::find(QString()) == nullptr);
    }

    // 一个厂商可以挂多条协议。厂商默认协议 = 该厂商在表里的第一条，旧 profile
    // （只有 provider）迁移后必须落到 Chat Completions，不能被静默换成 Responses。
    void vendorDefaultIsFirstRegisteredProtocol() {
        const auto openai = LlmProtocolRegistry::protocolsOfProvider(QStringLiteral("openai"));
        QCOMPARE(openai.size(), 2);
        QCOMPARE(openai.at(0).key, QStringLiteral("openai-chat"));
        QCOMPARE(openai.at(1).key, QStringLiteral("openai-responses"));
        QVERIFY(!openai.at(0).label.isEmpty());
        QCOMPARE(LlmProtocolRegistry::resolve(QString(), QStringLiteral("openai"))->key,
                 QStringLiteral("openai-chat"));
        // 单协议厂商：UI 据此不显示协议选择行
        QCOMPARE(LlmProtocolRegistry::protocolsOfProvider(QStringLiteral("ollama")).size(), 1);
        QVERIFY(LlmProtocolRegistry::protocolsOfProvider(QStringLiteral("mistral")).isEmpty());
    }

    // 旧 profile 没有 apiProtocol；手改的配置可能是陌生协议或陌生厂商。
    void resolveAlwaysYieldsExistingDescriptor() {
        // 协议已知：以协议为准，即使厂商字段与它不一致
        QCOMPARE(LlmProtocolRegistry::resolve(
                     QString::fromLatin1(LlmProtocol::kOllamaChat),
                     QStringLiteral("openai"))->provider,
                 QStringLiteral("ollama"));
        // 协议缺失：回落该厂商的默认协议（覆盖迁移前的旧 profile）
        QCOMPARE(LlmProtocolRegistry::resolve(QString(), QStringLiteral("anthropic"))->key,
                 QString::fromLatin1(LlmProtocol::kAnthropicMessages));
        // 两个都不认识：回落 openai-chat，绝不返回空
        const auto* fallback = LlmProtocolRegistry::resolve(
            QStringLiteral("bogus"), QStringLiteral("mistral"));
        QVERIFY(fallback != nullptr);
        QCOMPARE(fallback->key, QString::fromLatin1(LlmProtocol::kOpenaiChat));
    }

    void vendorAxisStaysQueryable() {
        QStringList expected;
        for (const auto& d : LlmProtocolRegistry::descriptors()) {
            if (!expected.contains(d.provider)) expected.append(d.provider);
        }
        QCOMPARE(LlmProtocolRegistry::providers(), expected);
        QVERIFY(LlmProtocolRegistry::isKnownProvider(QStringLiteral("ollama")));
        QVERIFY(!LlmProtocolRegistry::isKnownProvider(QStringLiteral("mistral")));
    }

    void localFlagDrivesTheCloudDetection() {
        QVERIFY(LlmProtocolRegistry::isLocal(QString::fromLatin1(LlmProtocol::kOllamaChat),
                                             QStringLiteral("ollama")));
        // 未知协议按厂商回落，所以旧 profile（只有 provider）也能判定
        QVERIFY(LlmProtocolRegistry::isLocal(QString(), QStringLiteral("ollama")));
        QVERIFY(!LlmProtocolRegistry::isLocal(QString::fromLatin1(LlmProtocol::kOpenaiChat),
                                              QStringLiteral("openai")));
        QVERIFY(!LlmProtocolRegistry::isLocal(QString(), QString()));
    }

    // main.cpp 的装配表就是这张注册表的展开：每个协议一个实例，父对象统一。
    void serviceSetCoversEveryProtocol() {
        QObject parent;
        const auto set = LlmProtocolRegistry::createServiceSet(&parent);
        QCOMPARE(set.size(), LlmProtocolRegistry::descriptors().size());
        for (const auto& d : LlmProtocolRegistry::descriptors()) {
            IAIService* service = set.value(d.key);
            QVERIFY2(service != nullptr, qPrintable(QStringLiteral("missing ") + d.key));
            QCOMPARE(service->parent(), &parent);
        }
        QVERIFY(LlmProtocolRegistry::createService(QStringLiteral("nope"), &parent) == nullptr);
    }

    // 任务 #24（批次四）：Gemini 原生 GenerateContent 协议的注册面——
    // key/provider/默认端点模型/envVar/local=false（云 gate 生效前提）。
    void geminiDescriptorRegistered() {
        const QString key = QString::fromLatin1(LlmProtocol::kGeminiGenerate);
        const auto* d = LlmProtocolRegistry::find(key);
        QVERIFY(d != nullptr);
        QCOMPARE(d->provider, QStringLiteral("gemini"));
        QCOMPARE(d->defaultBaseUrl,
                 QStringLiteral("https://generativelanguage.googleapis.com"));
        QCOMPARE(d->defaultModel, QStringLiteral("gemini-2.5-flash"));
        QCOMPARE(d->envApiKeyVar, QStringLiteral("GEMINI_API_KEY"));
        QVERIFY(!d->local);   // 云 provider：localOnlyMode 的云 gate 必须拦得住
        QVERIFY(d->models.contains(QStringLiteral("gemini-2.5-pro")));
        QCOMPARE(d->contextWindows.value(QStringLiteral("gemini-2.5-flash")), 1000000);
        QCOMPARE(d->contextWindows.value(QStringLiteral("gemini-2.5-pro")), 1000000);

        // 厂商轴可查询：providers 含 gemini、按厂商取协议非空、旧 profile
        // （只有 provider=gemini）resolve 回落到该厂商默认协议。
        QVERIFY(LlmProtocolRegistry::providers().contains(QStringLiteral("gemini")));
        QVERIFY(LlmProtocolRegistry::isKnownProvider(QStringLiteral("gemini")));
        const auto protocols =
            LlmProtocolRegistry::protocolsOfProvider(QStringLiteral("gemini"));
        QCOMPARE(protocols.size(), 1);
        QCOMPARE(protocols.at(0).key, key);
        QCOMPARE(LlmProtocolRegistry::resolve(QString(), QStringLiteral("gemini"))->key,
                 key);

        // local-only 门控按 (协议, 厂商) 判定：gemini 是云服务
        QVERIFY(!LlmProtocolRegistry::isLocal(key, QStringLiteral("gemini")));
        QVERIFY(!LlmProtocolRegistry::isLocal(QString(), QStringLiteral("gemini")));

        // 工厂产出的是 GeminiService 实例（装配链端到端）
        QObject parent;
        QVERIFY(qobject_cast<GeminiService*>(
                    LlmProtocolRegistry::createService(key, &parent)) != nullptr);
    }
};

QTEST_GUILESS_MAIN(TestProtocolRegistry)
#include "test_protocol_registry.moc"
