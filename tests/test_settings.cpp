// SettingsService multi-model profiles, defaults completion, model switching,
// addition, removal and persistence coverage.
#include "viewmodels/SettingsService.h"
#include "viewmodels/SettingsSchema.h"
#include "viewmodels/ThemeManager.h"
#include "core/tools/ToolRegistry.h"

#include "core/memory/SqliteMemory.h"
#include "core/utils/Logger.h"
#include "core/utils/PricingCalculator.h"

#include <QtTest>
#include <QSet>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStyleHints>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QSettings>

#include <memory>

using namespace NexusAI;

namespace {

class SettingsGuard {
public:
    SettingsGuard() {
        QSettings s("NexusAI", "NexusAI");
        rawProfiles = s.value("modelProfiles");
        activeId = s.value("activeProfileId");
        // Also guard the keys suites actually write; without this the tests
        // leaked settings into the user's real app (e.g. logging/level,
        // embedding/model, the security/allowFullDiskRead sandbox switch).
        loggingLevel = s.value("logging/level");
        embeddingModel = s.value("embedding/model");
        embeddingBaseUrl = s.value("embedding/baseUrl");
        allowFullDiskRead = s.value("security/allowFullDiskRead");
        localOnlyMode = s.value("security/localOnlyMode");
        allowLocalNetworkFetch = s.value("security/allowLocalNetworkFetch");
        systemPromptOverride = s.value("agent/systemPromptOverride");
        toolsDisabled = s.value("tools/disabled");
        webSearchProvider = s.value("webSearch/provider");
        webSearchApiKey = s.value("webSearch/apiKey");
        schemaVersion = s.value("schemaVersion");
        defaultPermissionMode = s.value("agent/defaultPermissionMode");
        readDenyList = s.value("security/readDenyList");
        maxSteps = s.value("agent/maxSteps");
        // 测试开始时把 schema 版本顶到最新：否则每个用例里第一次构造
        // SettingsService 都会跑一次 v1→v2 迁移（清空 modelProfiles 里的模型），
        // 让“先写 profile 再 load”的用例看到被迁移改过的数据。迁移本身由
        // schemaV1ProfilesLoseModelsOnUpgrade 单独覆盖。
        s.setValue("schemaVersion", SettingsSchema::kSchemaVersion);
    }
    ~SettingsGuard() {
        QSettings s("NexusAI", "NexusAI");
        if (rawProfiles.isValid()) s.setValue("modelProfiles", rawProfiles);
        else s.remove("modelProfiles");
        if (activeId.isValid()) s.setValue("activeProfileId", activeId);
        else s.remove("activeProfileId");
        if (loggingLevel.isValid()) s.setValue("logging/level", loggingLevel);
        else s.remove("logging/level");
        if (embeddingModel.isValid()) s.setValue("embedding/model", embeddingModel);
        else s.remove("embedding/model");
        if (embeddingBaseUrl.isValid()) s.setValue("embedding/baseUrl", embeddingBaseUrl);
        else s.remove("embedding/baseUrl");
        if (allowFullDiskRead.isValid())
            s.setValue("security/allowFullDiskRead", allowFullDiskRead);
        else s.remove("security/allowFullDiskRead");
        if (localOnlyMode.isValid())
            s.setValue("security/localOnlyMode", localOnlyMode);
        else s.remove("security/localOnlyMode");
        if (allowLocalNetworkFetch.isValid())
            s.setValue("security/allowLocalNetworkFetch", allowLocalNetworkFetch);
        else s.remove("security/allowLocalNetworkFetch");
        if (systemPromptOverride.isValid())
            s.setValue("agent/systemPromptOverride", systemPromptOverride);
        else s.remove("agent/systemPromptOverride");
        if (toolsDisabled.isValid())
            s.setValue("tools/disabled", toolsDisabled);
        else s.remove("tools/disabled");
        if (webSearchProvider.isValid())
            s.setValue("webSearch/provider", webSearchProvider);
        else s.remove("webSearch/provider");
        if (webSearchApiKey.isValid())
            s.setValue("webSearch/apiKey", webSearchApiKey);
        else s.remove("webSearch/apiKey");
        if (schemaVersion.isValid())
            s.setValue("schemaVersion", schemaVersion);
        else s.remove("schemaVersion");
        if (defaultPermissionMode.isValid())
            s.setValue("agent/defaultPermissionMode", defaultPermissionMode);
        else s.remove("agent/defaultPermissionMode");
        if (readDenyList.isValid())
            s.setValue("security/readDenyList", readDenyList);
        else s.remove("security/readDenyList");
        if (maxSteps.isValid()) s.setValue("agent/maxSteps", maxSteps);
        else s.remove("agent/maxSteps");
    }
    QVariant rawProfiles;
    QVariant activeId;
    QVariant loggingLevel;
    QVariant embeddingModel;
    QVariant embeddingBaseUrl;
    QVariant allowFullDiskRead;
    QVariant localOnlyMode;
    QVariant allowLocalNetworkFetch;
    QVariant systemPromptOverride;
    QVariant toolsDisabled;
    QVariant webSearchProvider;
    QVariant webSearchApiKey;
    QVariant schemaVersion;
    QVariant defaultPermissionMode;
    QVariant readDenyList;
    QVariant maxSteps;
};

// profile 列表按 id 取一项；找不到返回空 map（测试里用来断言刚建出来的 profile）。
QVariantMap profileById(const SettingsService& service, const QString& id) {
    for (const QVariant& entry : service.profiles()) {
        const QVariantMap p = entry.toMap();
        if (p.value("id").toString() == id) return p;
    }
    return {};
}

} // namespace

class TestSettingsService : public QObject {
    Q_OBJECT

private slots:
    // 全新安装：没有任何厂商预设模型，当前模型也留空，用户手填。厂商默认地址
    // 仍会预填（那是端点的一部分，不是模型）。
    void freshInstallStartsWithBlankModelList() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("modelProfiles");
            s.remove("activeProfileId");
        }

        SettingsService service(nullptr);
        const QVariantList profiles = service.profiles();
        QVERIFY(!profiles.isEmpty());

        const QVariantMap active = service.activeProfile();
        QVERIFY(!active.isEmpty());
        QVERIFY(active.contains("models"));

        QVERIFY(active.value("model").toString().isEmpty());
        QVERIFY(active.value("models").toStringList().isEmpty());
        QVERIFY(!active.value("baseUrl").toString().isEmpty());
    }

    // apiProtocol 轴：0.3 之前持久化的 profile 只有 provider，加载时必须按
    // 厂商补齐协议，运行侧（main.cpp）才能直接按协议取服务。
    void legacyProfileGainsApiProtocolOnLoad() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            QJsonArray arr;
            QJsonObject legacy;
            legacy["id"] = QStringLiteral("legacy-1");
            legacy["displayName"] = QStringLiteral("旧配置");
            legacy["provider"] = QStringLiteral("anthropic");
            legacy["baseUrl"] = QStringLiteral("https://example.test");
            legacy["model"] = QStringLiteral("claude-sonnet-4-5");
            arr.append(legacy);
            s.setValue("modelProfiles", QString::fromUtf8(
                QJsonDocument(arr).toJson(QJsonDocument::Compact)));
            s.setValue("activeProfileId", QStringLiteral("legacy-1"));
        }

        SettingsService service(nullptr);
        const QVariantMap active = service.activeProfile();
        QCOMPARE(active.value("provider").toString(), QStringLiteral("anthropic"));
        QCOMPARE(active.value("apiProtocol").toString(),
                 QStringLiteral("anthropic-messages"));
        // 补齐不能动用户已经配好的端点
        QCOMPARE(active.value("baseUrl").toString(), QStringLiteral("https://example.test"));
    }

    // 协议比厂商更具体：两者冲突时以协议为准回填厂商；协议本身没注册过
    // （手改坏的配置）则回落该厂商的默认协议。
    void endpointPairIsNormalizedOnLoad() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            QJsonArray arr;
            QJsonObject conflict;
            conflict["id"] = QStringLiteral("c1");
            conflict["provider"] = QStringLiteral("openai");
            conflict["apiProtocol"] = QStringLiteral("ollama-chat");
            arr.append(conflict);
            QJsonObject bogus;
            bogus["id"] = QStringLiteral("b1");
            bogus["provider"] = QStringLiteral("ollama");
            bogus["apiProtocol"] = QStringLiteral("ollama-messages");   // 没注册过的协议
            arr.append(bogus);
            s.setValue("modelProfiles", QString::fromUtf8(
                QJsonDocument(arr).toJson(QJsonDocument::Compact)));
            s.setValue("activeProfileId", QStringLiteral("c1"));
        }

        SettingsService service(nullptr);
        const QVariantList profiles = service.profiles();
        QCOMPARE(profiles.size(), 2);
        const QVariantMap first = profiles.at(0).toMap();
        QCOMPARE(first.value("apiProtocol").toString(), QStringLiteral("ollama-chat"));
        QCOMPARE(first.value("provider").toString(), QStringLiteral("ollama"));
        const QVariantMap second = profiles.at(1).toMap();
        QCOMPARE(second.value("apiProtocol").toString(), QStringLiteral("ollama-chat"));
        QCOMPARE(second.value("provider").toString(), QStringLiteral("ollama"));
    }

    // 协议轴自洽性：同一厂商的两条协议各自查自己的能力表，未注册的协议值必须
    // 被忽略。
    void responsesProtocolKeepsVendorAndOwnsCapabilityTable() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("modelProfiles");
            s.remove("activeProfileId");
        }

        SettingsService service(nullptr);
        const QString id = service.activeProfileId();   // 默认 openai + openai-chat
        QCOMPARE(service.activeProfile().value("apiProtocol").toString(),
                 QStringLiteral("openai-chat"));

        service.updateProfile(id, {{QStringLiteral("apiProtocol"),
                                    QStringLiteral("openai-responses")}});
        QVariantMap profile = service.activeProfile();
        QCOMPARE(profile.value("apiProtocol").toString(), QStringLiteral("openai-responses"));
        // 换协议不能把厂商改掉
        QCOMPARE(profile.value("provider").toString(), QStringLiteral("openai"));

        // gpt-4.1 只在 Responses 的能力表里；Chat 协议查它是 0。
        QCOMPARE(service.modelContextWindow(QStringLiteral("openai-responses"),
                                            QStringLiteral("gpt-4.1")), 1000000);
        QCOMPARE(service.modelContextWindow(QStringLiteral("openai-chat"),
                                            QStringLiteral("gpt-4.1")), 0);

        // 陌生协议值被拒绝，profile 保持在 Responses 上
        service.updateProfile(id, {{QStringLiteral("apiProtocol"),
                                    QStringLiteral("openai-whatever")}});
        QCOMPARE(service.activeProfile().value("apiProtocol").toString(),
                 QStringLiteral("openai-responses"));

        // 设置页的协议选择行就绑这个列表（只有一条时不显示）：内容、顺序、
        // 默认端点都要齐，否则按钮点出空白 profile。
        const QVariantList openaiOptions = service.endpointOptions(QStringLiteral("openai"));
        QCOMPARE(openaiOptions.size(), 2);
        for (const QVariant& option : openaiOptions) {
            const QVariantMap map = option.toMap();
            QVERIFY(!map.value("label").toString().isEmpty());
            QVERIFY(!map.value("apiProtocol").toString().isEmpty());
        }
        QCOMPARE(openaiOptions.at(0).toMap().value("apiProtocol").toString(),
                 QStringLiteral("openai-chat"));
        QCOMPARE(openaiOptions.at(1).toMap().value("apiProtocol").toString(),
                 QStringLiteral("openai-responses"));
        QCOMPARE(service.endpointOptions(QStringLiteral("ollama")).size(), 1);
        // 陌生厂商按 openai 兜底，不给空列表（空列表会让协议行消失）
        QCOMPARE(service.endpointOptions(QStringLiteral("mistral")).size(), 2);
    }

    // 换厂商 = 换端点：baseUrl 换成新厂商的默认地址，当前模型与模型列表清空
    // （不再有预设可回填），生成参数不属于端点，必须原样留下。UI 只提交被点的厂商键。
    void vendorSwitchClearsModelAndRewritesBaseUrl() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("modelProfiles");
            s.remove("activeProfileId");
        }

        SettingsService service(nullptr);
        const QString id = service.activeProfileId();
        service.updateProfile(id, {{QStringLiteral("baseUrl"), QStringLiteral("http://my-proxy/v1")},
                                   {QStringLiteral("model"), QStringLiteral("gpt-4o")},
                                   {QStringLiteral("temperature"), QStringLiteral("0.4")},
                                   {QStringLiteral("maxOutputTokens"), QStringLiteral("32768")}});
        service.addModelToProfile(id, QStringLiteral("my-chat-model"));

        service.updateProfile(id, {{QStringLiteral("provider"), QStringLiteral("anthropic")}});
        const QVariantMap p = service.activeProfile();
        QCOMPARE(p.value("apiProtocol").toString(), QStringLiteral("anthropic-messages"));
        QCOMPARE(p.value("baseUrl").toString(), QStringLiteral("https://api.anthropic.com"));
        // 旧厂商的模型在新厂商用不了，又没有预设可回填，于是清空等用户重填
        QVERIFY(p.value("model").toString().isEmpty());
        QVERIFY(p.value("models").toStringList().isEmpty());
        // 生成参数与端点无关
        QCOMPARE(p.value("temperature").toString(), QStringLiteral("0.4"));
        QCOMPARE(p.value("maxOutputTokens").toString(), QStringLiteral("32768"));
    }

    // 同厂商换协议：还是同一个网关，baseUrl 与用户列表原样保留（不再合并预设模型）。
    void protocolSwitchWithinVendorKeepsUserModels() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("modelProfiles");
            s.remove("activeProfileId");
        }

        SettingsService service(nullptr);
        const QString id = service.activeProfileId();
        service.updateProfile(id, {{QStringLiteral("baseUrl"), QStringLiteral("http://gw.local/v1")},
                                   {QStringLiteral("model"), QStringLiteral("gpt-4o")}});
        service.addModelToProfile(id, QStringLiteral("my-gateway-model"));

        service.updateProfile(id, {{QStringLiteral("apiProtocol"),
                                    QStringLiteral("openai-responses")}});
        const QVariantMap p = service.activeProfile();
        QCOMPARE(p.value("provider").toString(), QStringLiteral("openai"));
        QCOMPARE(p.value("apiProtocol").toString(), QStringLiteral("openai-responses"));
        QCOMPARE(p.value("baseUrl").toString(), QStringLiteral("http://gw.local/v1"));
        QCOMPARE(p.value("model").toString(), QStringLiteral("gpt-4o"));
        const QStringList models = p.value("models").toStringList();
        QVERIFY(models.contains(QStringLiteral("gpt-4o")));
        QVERIFY2(models.contains(QStringLiteral("my-gateway-model")),
                 "同厂商换协议不能丢用户自己加的模型");
        // 换协议不再把新协议的预设模型并进列表
        QVERIFY2(!models.contains(QStringLiteral("gpt-4.1")), qPrintable(models.join(',')));
    }

    // 自定义网关是一条 kind=custom 的 openai/openai-chat 配置，新建时端点三项
    // （baseUrl / model / models）全空——接第三方服务时预填官方值只会让人先删后填。
    void customProfileStartsBlank() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("modelProfiles");
            s.remove("activeProfileId");
        }

        SettingsService service(nullptr);
        const int before = service.profiles().size();
        const QString id = service.addProfile(QStringLiteral("custom"));
        QCOMPARE(service.profiles().size(), before + 1);

        const QVariantMap p = profileById(service, id);
        QVERIFY2(!p.isEmpty(), qPrintable(id));
        QCOMPARE(p.value("kind").toString(), QStringLiteral("custom"));
        // 仍走 openai-chat：自定义不是第五种线协议，门控与能力表照旧按厂商/协议判
        QCOMPARE(p.value("provider").toString(), QStringLiteral("openai"));
        QCOMPARE(p.value("apiProtocol").toString(), QStringLiteral("openai-chat"));
        QVERIFY(p.value("baseUrl").toString().isEmpty());
        QVERIFY(p.value("model").toString().isEmpty());
        QVERIFY(p.value("models").toStringList().isEmpty());
    }

    // 自定义配置填好网关之后，换协议或点厂商来源芯片都不能把用户值换成官方默认值
    // （旧逻辑在 endpointChanged / vendorChanged 两处都会回填）。
    void customProfileIgnoresEndpointRefills() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("modelProfiles");
            s.remove("activeProfileId");
        }

        SettingsService service(nullptr);
        const QString id = service.addProfile(QStringLiteral("custom"));
        service.updateProfile(id, {{QStringLiteral("baseUrl"), QStringLiteral("http://gw.local/v1")},
                                   {QStringLiteral("model"), QStringLiteral("glm-4")}});

        // 同厂商换协议：Responses 的预设里没有 glm-4，回填会把模型名换成 gpt-4o-mini
        service.updateProfile(id, {{QStringLiteral("apiProtocol"),
                                    QStringLiteral("openai-responses")}});
        QVariantMap p = profileById(service, id);
        QCOMPARE(p.value("apiProtocol").toString(), QStringLiteral("openai-responses"));
        QCOMPARE(p.value("baseUrl").toString(), QStringLiteral("http://gw.local/v1"));
        QCOMPARE(p.value("model").toString(), QStringLiteral("glm-4"));
        QStringList models = p.value("models").toStringList();
        QVERIFY2(models.contains(QStringLiteral("glm-4")), qPrintable(models.join(',')));
        QVERIFY(!models.contains(QStringLiteral("gpt-4o-mini")));
        QVERIFY(!models.contains(QStringLiteral("gpt-4.1")));

        // 点回「OpenAI 兼容」来源芯片：厂商没变（自定义也骑在 openai 上），
        // 所以什么都不清，网关地址与手填模型名原样保留；也不会冒出预设模型。
        service.updateProfile(id, {{QStringLiteral("kind"), QStringLiteral("openai")}});
        p = profileById(service, id);
        QCOMPARE(p.value("kind").toString(), QStringLiteral("openai"));
        QCOMPARE(p.value("apiProtocol").toString(), QStringLiteral("openai-chat"));
        QCOMPARE(p.value("baseUrl").toString(), QStringLiteral("http://gw.local/v1"));
        QCOMPARE(p.value("model").toString(), QStringLiteral("glm-4"));
        models = p.value("models").toStringList();
        QVERIFY2(models.contains(QStringLiteral("glm-4")), qPrintable(models.join(',')));
        QVERIFY2(!models.contains(QStringLiteral("gpt-4o-mini")), qPrintable(models.join(',')));
    }

    // 未登记的 kind 会被丢弃（保持原 kind），旧 profile 缺 kind 时按厂商推断。
    void unknownKindIsIgnoredAndLegacyKindIsInferred() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            QJsonArray arr;
            QJsonObject legacy;
            legacy["id"] = QStringLiteral("legacy-kind");
            legacy["displayName"] = QStringLiteral("旧配置");
            legacy["provider"] = QStringLiteral("ollama");
            legacy["baseUrl"] = QStringLiteral("http://localhost:11434");
            legacy["model"] = QStringLiteral("qwen2.5:7b");
            arr.append(legacy);
            s.remove("activeProfileId");
            s.setValue("modelProfiles",
                       QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }

        SettingsService service(nullptr);
        QVariantMap p = profileById(service, QStringLiteral("legacy-kind"));
        QVERIFY2(!p.isEmpty(), "旧 profile 应被加载");
        QCOMPARE(p.value("kind").toString(), QStringLiteral("ollama"));

        service.updateProfile(p.value("id").toString(),
                             {{QStringLiteral("kind"), QStringLiteral("not-a-kind")}});
        p = profileById(service, QStringLiteral("legacy-kind"));
        QCOMPARE(p.value("kind").toString(), QStringLiteral("ollama"));
        QCOMPARE(p.value("provider").toString(), QStringLiteral("ollama"));
    }

    // 重新 load（模拟重启）时，空的自定义/任意 profile 不得被厂商预设模型列表灌满。
    void customProfileStaysBlankAcrossReload() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("modelProfiles");
            s.remove("activeProfileId");
        }

        QString id;
        {
            SettingsService s1(nullptr);
            id = s1.addProfile(QStringLiteral("custom"));
        }

        SettingsService s2(nullptr);
        const QVariantMap p = profileById(s2, id);
        QVERIFY2(!p.isEmpty(), qPrintable(id));
        QCOMPARE(p.value("kind").toString(), QStringLiteral("custom"));
        QVERIFY(p.value("baseUrl").toString().isEmpty());
        QVERIFY(p.value("model").toString().isEmpty());
        QVERIFY(p.value("models").toStringList().isEmpty());
    }

    // 厂商来源也不再预填模型（地址照旧预填）；未知来源回落 openai。
    void addProfileLeavesModelBlank() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("modelProfiles");
            s.remove("activeProfileId");
        }

        SettingsService service(nullptr);
        const QVariantMap ollama = profileById(service, service.addProfile(QStringLiteral("ollama")));
        QCOMPARE(ollama.value("kind").toString(), QStringLiteral("ollama"));
        QCOMPARE(ollama.value("apiProtocol").toString(), QStringLiteral("ollama-chat"));
        QCOMPARE(ollama.value("baseUrl").toString(), QStringLiteral("http://localhost:11434"));
        QVERIFY(ollama.value("model").toString().isEmpty());
        QVERIFY(ollama.value("models").toStringList().isEmpty());

        const QVariantMap openai = profileById(service, service.addProfile(QStringLiteral("openai")));
        QVERIFY(openai.value("model").toString().isEmpty());
        QVERIFY(openai.value("models").toStringList().isEmpty());

        const QVariantMap fallback = profileById(service, service.addProfile(QStringLiteral("nope")));
        QCOMPARE(fallback.value("kind").toString(), QStringLiteral("openai"));
        QCOMPARE(fallback.value("baseUrl").toString(), QStringLiteral("https://api.openai.com/v1"));
        QVERIFY(fallback.value("model").toString().isEmpty());
    }

    // 调用方显式给了 baseUrl / model / models，就以它为准，不再自动回填。
    void explicitEndpointValuesWin() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("modelProfiles");
            s.remove("activeProfileId");
        }

        SettingsService service(nullptr);
        const QString id = service.activeProfileId();
        service.updateProfile(id, {{QStringLiteral("provider"), QStringLiteral("ollama")},
                                   {QStringLiteral("baseUrl"), QStringLiteral("http://10.0.0.9:11434")},
                                   {QStringLiteral("model"), QStringLiteral("mistral:7b")}});
        const QVariantMap p = service.activeProfile();
        QCOMPARE(p.value("baseUrl").toString(), QStringLiteral("http://10.0.0.9:11434"));
        QCOMPARE(p.value("model").toString(), QStringLiteral("mistral:7b"));
        const QStringList models = p.value("models").toStringList();
        QCOMPARE(models, QStringList{QStringLiteral("mistral:7b")});
    }

    // 重复点当前厂商/当前协议的按钮不能把人改过的 baseUrl 和模型抹掉。
    void repeatEndpointSubmitIsHarmless() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("modelProfiles");
            s.remove("activeProfileId");
        }

        SettingsService service(nullptr);
        const QString id = service.activeProfileId();
        service.updateProfile(id, {{QStringLiteral("baseUrl"), QStringLiteral("http://keep/v1")},
                                   {QStringLiteral("model"), QStringLiteral("deepseek-chat")}});
        service.updateProfile(id, {{QStringLiteral("provider"), QStringLiteral("openai")}});
        service.updateProfile(id, {{QStringLiteral("apiProtocol"), QStringLiteral("openai-chat")}});
        const QVariantMap p = service.activeProfile();
        QCOMPARE(p.value("baseUrl").toString(), QStringLiteral("http://keep/v1"));
        QCOMPARE(p.value("model").toString(), QStringLiteral("deepseek-chat"));
        QVERIFY(p.value("models").toStringList().contains(QStringLiteral("deepseek-chat")));
    }

    // 当前模型永远在模型列表里；模型名两侧空格在写入时去掉（否则列表项与
    // 当前模型对不上，chip 高亮和聊天侧选择器都会错位）。
    void currentModelAlwaysListedAndTrimmed() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("modelProfiles");
            s.remove("activeProfileId");
        }

        SettingsService service(nullptr);
        const QString id = service.activeProfileId();
        service.updateProfile(id, {{QStringLiteral("model"), QStringLiteral("  spaced-model  ")}});
        const QVariantMap p = service.activeProfile();
        QCOMPARE(p.value("model").toString(), QStringLiteral("spaced-model"));
        QVERIFY(p.value("models").toStringList().contains(QStringLiteral("spaced-model")));
        QVERIFY(!p.value("models").toStringList().contains(QStringLiteral("  spaced-model  ")));
    }

    // UI 上换厂商（三个按钮之一）只提交 provider，协议要跟着重置成该厂商的
    // 默认协议；未注册的协议值必须被忽略。
    void updateProfileKeepsEndpointPairConsistent() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("modelProfiles");
            s.remove("activeProfileId");
        }

        SettingsService service(nullptr);
        const QString id = service.activeProfileId();
        service.updateProfile(id, {{QStringLiteral("provider"), QStringLiteral("ollama")}});
        QVariantMap profile = service.activeProfile();
        QCOMPARE(profile.value("id").toString(), id);
        QCOMPARE(profile.value("apiProtocol").toString(), QStringLiteral("ollama-chat"));

        service.updateProfile(id, {{QStringLiteral("apiProtocol"), QStringLiteral("nope")}});
        QCOMPARE(service.activeProfile().value("apiProtocol").toString(),
                 QStringLiteral("ollama-chat"));

        service.updateProfile(id, {{QStringLiteral("provider"), QStringLiteral("bogus-vendor")}});
        QCOMPARE(service.activeProfile().value("provider").toString(),
                 QStringLiteral("ollama"));

        // 单协议厂商：选项只有一条，UI 据此不显示协议行
        const QVariantList anthropicOptions = service.endpointOptions(QStringLiteral("anthropic"));
        QCOMPARE(anthropicOptions.size(), 1);
        QCOMPARE(anthropicOptions.at(0).toMap().value("apiProtocol").toString(),
                 QStringLiteral("anthropic-messages"));
    }

    // 会话权限模式全局默认：默认 confirm；非法值被拒绝；合法值持久化。
    void defaultPermissionModeSetting() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("agent/defaultPermissionMode");
        }

        SettingsService service(nullptr);
        QCOMPARE(service.defaultPermissionMode(), QStringLiteral("confirm"));

        QSignalSpy spy(&service, &SettingsService::defaultPermissionModeChanged);

        service.setDefaultPermissionMode(QStringLiteral("auto_edit"));
        QCOMPARE(service.defaultPermissionMode(), QStringLiteral("auto_edit"));
        QCOMPARE(spy.count(), 1);

        service.setDefaultPermissionMode(QStringLiteral("full_access"));
        QCOMPARE(service.defaultPermissionMode(), QStringLiteral("full_access"));

        // 非法值：拒绝且不发信号
        service.setDefaultPermissionMode(QStringLiteral("yolo"));
        QCOMPARE(service.defaultPermissionMode(), QStringLiteral("full_access"));
        QCOMPARE(spy.count(), 2);

        // 重启语义：新实例从 QSettings 读回
        {
            QSettings s("NexusAI", "NexusAI");
            s.setValue("agent/defaultPermissionMode", QStringLiteral("auto_edit"));
        }
        SettingsService reloaded(nullptr);
        QCOMPARE(reloaded.defaultPermissionMode(), QStringLiteral("auto_edit"));
    }

    // docs/17 B2: resetGeneralToDefaults 把通用页 9 个键回写为 schema
    // 登记的默认值（硬编码的 reset 列表与 schema 默认值靠本测试锁定同步）。
    void resetGeneralDefaultsMatchesSchema() {
        SettingsGuard guard;
        SettingsService svc(nullptr);

        // 先全部调到非默认值
        svc.setContextTokens(4096);
        svc.setMaxSteps(99);
        svc.setSimulationMode(true);
        svc.setLogLevel(QStringLiteral("debug"));
        svc.setEmbeddingModel(QStringLiteral("bge-m3"));
        svc.setEmbeddingBaseUrl(QStringLiteral("http://localhost:12345"));
        svc.setSystemPromptOverride(QStringLiteral("custom prompt"));

        // docs/17 A6: 安全键已迁"安全"页——通用页重置不得触碰它们。
        svc.setAllowFullDiskRead(true);
        svc.setLocalOnlyMode(true);
        svc.setAllowLocalNetworkFetch(true);

        svc.resetGeneralToDefaults();

        // 安全三键保持原值（属于安全页语义，不随通用页重置）。
        QVERIFY(svc.allowFullDiskRead());
        QVERIFY(svc.localOnlyMode());
        QVERIFY(svc.allowLocalNetworkFetch());

        QVariantMap defaults;
        for (const auto& k : SettingsSchema::registeredKeys())
            if (k.defaultValue.isValid())
                defaults.insert(k.key, k.defaultValue);
        QCOMPARE(svc.contextTokens(),
                 defaults.value("contextTokens").toInt());
        QCOMPARE(svc.maxSteps(), defaults.value("agent/maxSteps").toInt());
        QCOMPARE(svc.simulationMode(),
                 defaults.value("simulationMode").toBool());
        QCOMPARE(svc.logLevel(), defaults.value("logging/level").toString());
        QCOMPARE(svc.embeddingModel(),
                 defaults.value("embedding/model").toString());
        QCOMPARE(svc.embeddingBaseUrl(),
                 defaults.value("embedding/baseUrl").toString());
        QCOMPARE(svc.systemPromptOverride(),
                 defaults.value("agent/systemPromptOverride").toString());
    }

    // 步数预算（agent/maxSteps）：钳制到 [kMinMaxIterations, kIterationCeiling]，
    // 改动即时持久化。默认 30 取代了原先硬编码的 10。
    void maxStepsClampsAndPersists() {
        SettingsGuard guard;
        SettingsService svc(nullptr);

        QCOMPARE(svc.maxSteps(), AIAgent::kDefaultMaxIterations);

        svc.setMaxSteps(0);
        QCOMPARE(svc.maxSteps(), AIAgent::kMinMaxIterations);
        svc.setMaxSteps(9999);
        QCOMPARE(svc.maxSteps(), AIAgent::kIterationCeiling);

        svc.setMaxSteps(42);
        QCOMPARE(svc.maxSteps(), 42);
        QCOMPARE(QSettings("NexusAI", "NexusAI").value("agent/maxSteps").toInt(), 42);
    }

    // docs/17 B3: 账本完整性——三类旁路写入方（ToolRegistry/AgentViewModel/
    // PreferenceStore）的键必须全部登记进 schema，防止"唯一事实源"再漂移。
    void schemaCoversBypassWriters() {
        const auto& keys = SettingsSchema::registeredKeys();
        QSet<QString> registered;
        for (const auto& k : keys) registered.insert(k.key);

        const QStringList expected = {
            // M3: ToolRegistry 用户禁用清单
            "tools/disabled",
            // 迁移钩子（load() 写入，无静态默认）
            "schemaVersion",
            // M4: 运行时布局（AgentViewModel 直写）
            "ui/sidebarWidth", "ui/panelWidth",
            "ui/panelVisible", "ui/sidebarExpanded",
            // M2: PreferenceStore 物理键（prefs/v1/<key>）
            "prefs/v1/ui.themeMode", "prefs/v1/ui.fontSans",
            "prefs/v1/ui.fontMono", "prefs/v1/ui.baseFontSize",
            "prefs/v1/ui.locale",
            "prefs/v1/ui.defaultSidebarWidth", "prefs/v1/ui.defaultPanelWidth",
            "prefs/v1/ui.defaultSidebarExpanded", "prefs/v1/ui.defaultPanelVisible",
            "prefs/v1/ui.restoreWindowOnStartup",
            // M2 内部键
            "settings.lastCategory",
            // docs/17 A0/A1
            "projects/activeId",
            "security/readDenyList",
        };
        for (const QString& key : expected) {
            QVERIFY2(registered.contains(key), qPrintable(
                QStringLiteral("settings key not in schema ledger: %1").arg(key)));
        }
    }

    void modelSelectionAndSignals() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("modelProfiles");
            s.remove("activeProfileId");
        }

        SettingsService service(nullptr);
        const QVariantMap active = service.activeProfile();
        const QString activeId = active.value("id").toString();
        const QString currentModel = active.value("model").toString();
        QSignalSpy activeProfileSpy(&service, &SettingsService::activeProfileChanged);
        QSignalSpy profilesSpy(&service, &SettingsService::profilesChanged);

        // Select a new model for active profile
        const QString newModel = QStringLiteral("custom-gpt-4o-test");
        service.selectModelForProfile(activeId, newModel);

        QCOMPARE(activeProfileSpy.count(), 1);
        QCOMPARE(profilesSpy.count(), 1);
        QCOMPARE(service.activeProfile().value("model").toString(), newModel);

        const QStringList models = service.activeProfile().value("models").toStringList();
        QVERIFY(models.contains(newModel));

        // Selecting same model should be no-op
        service.selectModelForProfile(activeId, newModel);
        QCOMPARE(activeProfileSpy.count(), 1);
    }

    void addAndRemoveModels() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("modelProfiles");
            s.remove("activeProfileId");
        }

        SettingsService service(nullptr);
        const QString activeId = service.activeProfileId();

        service.addModelToProfile(activeId, QStringLiteral("extra-model-1"));
        QStringList models = service.activeProfile().value("models").toStringList();
        QVERIFY(models.contains(QStringLiteral("extra-model-1")));

        // Remove non-active model
        service.removeModelFromProfile(activeId, QStringLiteral("extra-model-1"));
        models = service.activeProfile().value("models").toStringList();
        QVERIFY(!models.contains(QStringLiteral("extra-model-1")));
    }

    void persistenceAcrossInstances() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("modelProfiles");
            s.remove("activeProfileId");
        }

        {
            SettingsService s1(nullptr);
            const QString activeId = s1.activeProfileId();
            s1.addModelToProfile(activeId, QStringLiteral("persisted-model-x"));
            s1.selectModelForProfile(activeId, QStringLiteral("persisted-model-x"));
        }

        SettingsService s2(nullptr);
        QCOMPARE(s2.activeProfile().value("model").toString(), QStringLiteral("persisted-model-x"));
        QVERIFY(s2.activeProfile().value("models").toStringList().contains(QStringLiteral("persisted-model-x")));
    }

    // Batch 8/日志开发: log level property (default, persistence, signal)
    void logLevelPersistence() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("logging/level");
        }

        {
            SettingsService service(nullptr);
            QCOMPARE(service.logLevel(), QStringLiteral("info"));

            QSignalSpy spy(&service, &SettingsService::logLevelChanged);
            service.setLogLevel(QStringLiteral("DEBUG"));
            QCOMPARE(spy.count(), 1);
            QCOMPARE(service.logLevel(), QStringLiteral("debug"));   // normalized
            service.setLogLevel(QStringLiteral(""));                  // blank → ignored
            QCOMPARE(service.logLevel(), QStringLiteral("debug"));
            // Instant effect on the logger itself (doc 6 §3.5)
            QCOMPARE(Logger::minLevel(), QtDebugMsg);
        }
        {
            SettingsService service(nullptr);
            QCOMPARE(service.logLevel(), QStringLiteral("debug"));
        }
        Logger::setMinLevel(QtInfoMsg);   // restore suite default
    }

    // Batch 22 (F8): user-editable system prompt; empty (after trim) clears
    // the override instead of being ignored.
    void systemPromptOverridePersistence() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("agent/systemPromptOverride");
        }

        {
            SettingsService service(nullptr);
            QCOMPARE(service.systemPromptOverride(), QString());

            QSignalSpy spy(&service, &SettingsService::systemPromptOverrideChanged);
            service.setSystemPromptOverride(QStringLiteral("  自定义提示词  "));
            QCOMPARE(spy.count(), 1);
            QCOMPARE(service.systemPromptOverride(), QStringLiteral("自定义提示词"));  // trimmed

            service.setSystemPromptOverride(QStringLiteral("自定义提示词"));          // no-op write
            QCOMPARE(spy.count(), 1);

            service.setSystemPromptOverride(QStringLiteral("   "));                   // blank clears
            QCOMPARE(spy.count(), 2);
            QCOMPARE(service.systemPromptOverride(), QString());
        }
        {
            SettingsService service(nullptr);
            service.setSystemPromptOverride(QStringLiteral("persisted prompt"));
        }
        {
            SettingsService service(nullptr);
            QCOMPARE(service.systemPromptOverride(), QStringLiteral("persisted prompt"));
        }
    }

    // FEAT-801: embedding model + Batch 5 full-disk-read persistence round trip
    // + Batch 14 local-only-mode persistence round trip
    void embeddingModelAndReadPolicyPersistence() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("embedding/model");
            s.remove("embedding/baseUrl");
            s.remove("security/allowFullDiskRead");
            s.remove("security/localOnlyMode");
            s.remove("security/allowLocalNetworkFetch");
        }

        {
            SettingsService service(nullptr);
            QCOMPARE(service.embeddingModel(), QStringLiteral("nomic-embed-text"));
            QCOMPARE(service.embeddingBaseUrl(), QStringLiteral("http://localhost:11434"));
            QVERIFY(!service.allowFullDiskRead());
            QVERIFY(!service.localOnlyMode());
            QVERIFY(!service.allowLocalNetworkFetch());

            QSignalSpy modelSpy(&service, &SettingsService::embeddingModelChanged);
            service.setEmbeddingModel(QStringLiteral("bge-m3"));
            QCOMPARE(modelSpy.count(), 1);
            service.setEmbeddingModel(QStringLiteral("bge-m3"));   // unchanged → silent
            QCOMPARE(modelSpy.count(), 1);
            service.setEmbeddingModel(QStringLiteral("   "));      // blank → ignored
            QCOMPARE(service.embeddingModel(), QStringLiteral("bge-m3"));

            // Batch 25 (G11): embedding endpoint persistence round trip.
            QSignalSpy urlSpy(&service, &SettingsService::embeddingBaseUrlChanged);
            service.setEmbeddingBaseUrl(QStringLiteral(" http://192.168.1.10:11434 "));
            QCOMPARE(urlSpy.count(), 1);
            QCOMPARE(service.embeddingBaseUrl(), QStringLiteral("http://192.168.1.10:11434"));
            service.setEmbeddingBaseUrl(QStringLiteral("http://192.168.1.10:11434"));  // unchanged → silent
            QCOMPARE(urlSpy.count(), 1);
            service.setEmbeddingBaseUrl(QStringLiteral("   "));    // blank → ignored
            QCOMPARE(service.embeddingBaseUrl(), QStringLiteral("http://192.168.1.10:11434"));

            service.setAllowFullDiskRead(true);

            QSignalSpy localOnlySpy(&service, &SettingsService::localOnlyModeChanged);
            service.setLocalOnlyMode(true);
            QCOMPARE(localOnlySpy.count(), 1);
            service.setLocalOnlyMode(true);                        // unchanged → silent
            QCOMPARE(localOnlySpy.count(), 1);

            QSignalSpy fetchSpy(&service, &SettingsService::allowLocalNetworkFetchChanged);
            service.setAllowLocalNetworkFetch(true);
            QCOMPARE(fetchSpy.count(), 1);
            service.setAllowLocalNetworkFetch(true);               // unchanged → silent
            QCOMPARE(fetchSpy.count(), 1);
        }
        {
            SettingsService service(nullptr);
            QCOMPARE(service.embeddingModel(), QStringLiteral("bge-m3"));
            QCOMPARE(service.embeddingBaseUrl(), QStringLiteral("http://192.168.1.10:11434"));
            QVERIFY(service.allowFullDiskRead());
            QVERIFY(service.localOnlyMode());
            QVERIFY(service.allowLocalNetworkFetch());
        }
    }

    // Regression (BUG settings-echo): GeneralPage's embedding-model combo
    // showed the default item again on every page rebuild while the value
    // itself was persisted correctly. Root cause (Qt 6.9 qquickcombobox.cpp):
    // componentComplete() calls updateCurrentText(), which force-writes
    // currentText (first item) over an editable combo's editText and thereby
    // wipes the `editText: Settings.embeddingModel` binding — unless
    // hasCurrentIndex is set. These cases lock the QML-side contract:
    //   - the pre-fix pattern must reproduce the default-echo defect
    //     (if a future Qt stops resetting editText, this workaround pair
    //     in GeneralPage.qml can be dropped together with this test);
    //   - the fixed pattern (currentIndex bound to the persisted index)
    //     must echo the saved value;
    //   - a persisted value outside the model list must still echo through
    //     the Component.onCompleted fallback.
    void embeddingModelComboEcho() {
        // Pre-fix layout: binding only, no currentIndex — Qt resets editText
        // to currentText (the first item) during component completion.
        static const char* kBeforeFix = R"QML(
import QtQuick
import QtQuick.Controls
ComboBox {
    editable: true
    model: ["nomic-embed-text", "bge-m3", "mxbai-embed-large", "all-minilm"]
    property string savedModel: "bge-m3"
    editText: savedModel
}
)QML";
        // Fixed layout: select the persisted index so componentComplete's
        // updateCurrentText() writes the same text (setEditText no-ops).
        static const char* kAfterFix = R"QML(
import QtQuick
import QtQuick.Controls
ComboBox {
    editable: true
    model: ["nomic-embed-text", "bge-m3", "mxbai-embed-large", "all-minilm"]
    property string savedModel: "bge-m3"
    currentIndex: model.indexOf(savedModel)
    editText: savedModel
    Component.onCompleted: if (editText !== savedModel) editText = savedModel
}
)QML";
        // Persisted value outside the model list: indexOf is -1, so the echo
        // relies on the onCompleted fallback after textAt(-1) == "".
        static const char* kCustomValue = R"QML(
import QtQuick
import QtQuick.Controls
ComboBox {
    editable: true
    model: ["nomic-embed-text", "bge-m3", "mxbai-embed-large", "all-minilm"]
    property string savedModel: "custom-embed"
    currentIndex: model.indexOf(savedModel)
    editText: savedModel
    Component.onCompleted: if (editText !== savedModel) editText = savedModel
}
)QML";

        const auto editTextAfterBuild = [](const char* qml) -> QString {
            QQmlEngine engine;
            QQmlComponent component(&engine);
            component.setData(QByteArray(qml), QUrl());
            if (!component.isReady())
                return component.errorString();
            std::unique_ptr<QObject> item(component.create());
            if (!item)
                return component.errorString();
            return item->property("editText").toString();
        };

        // Defect reproduction: the pre-fix combo echoed the default item.
        QCOMPARE(editTextAfterBuild(kBeforeFix), QStringLiteral("nomic-embed-text"));

        // The fixed combo echoes the persisted model.
        QCOMPARE(editTextAfterBuild(kAfterFix), QStringLiteral("bge-m3"));

        // Out-of-list persisted values fall back to the raw saved text.
        QCOMPARE(editTextAfterBuild(kCustomValue), QStringLiteral("custom-embed"));
    }

    void toolRegistryEmitsToolsChanged() {
        SettingsGuard guard;
        auto& reg = ToolRegistry::instance();
        QSignalSpy spy(&reg, &ToolRegistry::toolsChanged);
        QVERIFY(spy.isValid());

        reg.setToolEnabled("read_file", false);
        QCOMPARE(spy.count(), 1);
        reg.setToolEnabled("read_file", true);
        QCOMPARE(spy.count(), 2);

        spy.clear();
        auto taken = reg.takeTool("list_dir");
        QVERIFY(taken != nullptr);
        QCOMPARE(spy.count(), 1);
        reg.registerTool(std::move(taken));
        QCOMPARE(spy.count(), 2);
    }

    void toolDisabledStatePersists() {
        SettingsGuard guard;
        auto& reg = ToolRegistry::instance();

        reg.setToolEnabled("read_file", false);
        {
            QSettings s("NexusAI", "NexusAI");
            QVERIFY(s.value("tools/disabled").toStringList().contains("read_file"));
        }
        reg.setToolEnabled("read_file", true);
        {
            QSettings s("NexusAI", "NexusAI");
            QVERIFY(!s.value("tools/disabled").toStringList().contains("read_file"));
        }
    }

    // Built-in tools must not carry the Dangerous level: destructive work
    // goes through the confirm-gated shell_exec instead (the rm_rf sentinel
    // was removed). Re-adding a Dangerous built-in must be a conscious act.
    void noBuiltinToolIsDangerous() {
        auto& reg = ToolRegistry::instance();
        for (const auto& def : reg.listAll()) {
            QVERIFY2(def.level != ToolLevel::Dangerous, qPrintable(def.name));
        }
    }

    // ── T5 theme mode (auto / dark / light) ─────────────────────────
    void themeModeDefaultsToAuto() {
        SettingsGuard guard;
        clearThemeKeys();
        ThemeManager tm(nullptr);
        QCOMPARE(tm.themeMode(), QStringLiteral("auto"));
    }

    void themeModeExplicitPersistsAndReadsBack() {
        SettingsGuard guard;
        clearThemeKeys();
        {
            ThemeManager tm(nullptr);
            tm.setThemeMode(QStringLiteral("light"));
            QCOMPARE(tm.themeMode(), QStringLiteral("light"));
            QCOMPARE(tm.isDark(), false);
        }
        {
            QSettings s("NexusAI", "NexusAI");
            QCOMPARE(s.value("prefs/v1/ui.themeMode").toString(), QStringLiteral("light"));
        }
        // A fresh instance reads the persisted explicit choice back.
        ThemeManager tm2(nullptr);
        QCOMPARE(tm2.themeMode(), QStringLiteral("light"));
        QCOMPARE(tm2.isDark(), false);
    }

    void themeModeAutoUpgradesOnExplicitToggle() {
        SettingsGuard guard;
        clearThemeKeys();
        ThemeManager tm(nullptr);
        QCOMPARE(tm.themeMode(), QStringLiteral("auto"));
        // REQ-T5-04: an explicit toggle must upgrade "auto" to a concrete mode.
        tm.setIsDark(false);
        QCOMPARE(tm.themeMode(), QStringLiteral("light"));
        {
            QSettings s("NexusAI", "NexusAI");
            QCOMPARE(s.value("prefs/v1/ui.themeMode").toString(), QStringLiteral("light"));
        }
    }

    void themeModeLegacyDarkKeyCompat() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("prefs/v1/ui.themeMode");
            s.setValue("theme/dark", false);   // legacy light preference
        }
        ThemeManager tm(nullptr);
        QCOMPARE(tm.themeMode(), QStringLiteral("light"));
        QCOMPARE(tm.isDark(), false);
    }

    void themeModeAutoResolvesSystemScheme() {
        SettingsGuard guard;
        clearThemeKeys();
        ThemeManager tm(nullptr);
        QCOMPARE(tm.themeMode(), QStringLiteral("auto"));
        // auto follows the OS scheme; Unknown keeps the historical dark look.
        const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
        QCOMPARE(tm.isDark(), scheme != Qt::ColorScheme::Light);
    }

    // L4: setWebSearchProvider rejects unknown provider keys — the setting
    // stays at its default and no signal fires.
    void webSearchProviderRejectsUnknownKey() {
        SettingsGuard guard;
        SettingsService svc(nullptr);
        svc.setWebSearchProvider(QStringLiteral("tavily"));
        QSignalSpy spy(&svc, &SettingsService::webSearchProviderChanged);
        svc.setWebSearchProvider(QStringLiteral("google"));
        QCOMPARE(spy.count(), 0);
        QCOMPARE(svc.webSearchProvider(), QStringLiteral("tavily"));
    }

    // L2: SettingsSchema key registry is populated and contains known keys.
    void schemaHasKeyRegistry() {
        const auto& keys = SettingsSchema::registeredKeys();
        QVERIFY(!keys.isEmpty());
        bool foundProvider = false;
        bool foundApiKey = false;
        for (const auto& k : keys) {
            if (k.key == QStringLiteral("webSearch/provider")) foundProvider = true;
            if (k.key == QStringLiteral("webSearch/apiKey")) {
                foundApiKey = true;
                QVERIFY(k.encrypted);
            }
        }
        QVERIFY(foundProvider);
        QVERIFY(foundApiKey);
    }

    // L2: load() stamps the schema version into QSettings.
    void schemaVersionStampedOnLoad() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("schemaVersion");
        }
        SettingsService svc(nullptr);
        QSettings s("NexusAI", "NexusAI");
        QCOMPARE(s.value("schemaVersion").toInt(), SettingsSchema::kSchemaVersion);
    }

    // v1 → v2：模型列表改由用户手填，存量 profile 的 model/models 在 load 时清空；
    // 其余字段（地址、密钥、生成参数）与当前激活项不能动，而且迁移只跑一次（版本
    // 已顶到最新后再 load 不能把用户新填的模型又清掉）。
    void schemaV1ProfilesLoseModelsOnUpgrade() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            QJsonArray arr;
            QJsonObject legacy;
            legacy["id"] = QStringLiteral("v1-profile");
            legacy["displayName"] = QStringLiteral("旧配置");
            legacy["provider"] = QStringLiteral("openai");
            legacy["kind"] = QStringLiteral("openai");
            legacy["apiProtocol"] = QStringLiteral("openai-chat");
            legacy["baseUrl"] = QStringLiteral("https://api.openai.com/v1");
            // 明文旧密钥：迁移不该碰 apiKey（加密串要能解开才能断言，明文同样
            // 能验证"只动模型两项"）。
            legacy["apiKey"] = QStringLiteral("legacy-plain-key");
            legacy["model"] = QStringLiteral("gpt-4o");
            legacy["models"] = QJsonArray{QStringLiteral("gpt-4o"), QStringLiteral("gpt-4o-mini")};
            legacy["temperature"] = QStringLiteral("0.3");
            arr.append(legacy);
            s.setValue("modelProfiles",
                       QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
            s.setValue("activeProfileId", QStringLiteral("v1-profile"));
            s.setValue("schemaVersion", 1);
        }

        {
            SettingsService service(nullptr);
            const QVariantMap p = profileById(service, QStringLiteral("v1-profile"));
            QVERIFY2(!p.isEmpty(), "迁移不能丢掉 profile");
            QVERIFY(p.value("model").toString().isEmpty());
            QVERIFY(p.value("models").toStringList().isEmpty());
            // 迁移只碰模型两项：地址、加密串、生成参数原样保留
            QCOMPARE(p.value("baseUrl").toString(), QStringLiteral("https://api.openai.com/v1"));
            QCOMPARE(p.value("apiKey").toString(), QStringLiteral("legacy-plain-key"));
            QCOMPARE(p.value("temperature").toString(), QStringLiteral("0.3"));
            QCOMPARE(service.activeProfileId(), QStringLiteral("v1-profile"));

            // 迁移是一次性的：版本已到最新后再 load 不得再清空（此时加回来的模型
            // 是用户填的，重启后必须还在，顺带被设为当前模型）。
            service.addModelToProfile(QStringLiteral("v1-profile"), QStringLiteral("user-model"));
        }
        {
            SettingsService service(nullptr);
            QCOMPARE(service.activeProfile().value("models").toStringList(),
                     QStringList{QStringLiteral("user-model")});
            QCOMPARE(service.activeProfile().value("model").toString(),
                     QStringLiteral("user-model"));
        }
    }

    // docs/15 §6.1.1: modelContextWindow capability-table lookup by API
    // protocol (the table lives on the protocol, not the vendor: OpenAI 厂商的
    // Chat 与 Responses 两条协议可用模型不同).
    void testModelContextWindowKnownModel() {
        SettingsGuard guard;
        SettingsService service(nullptr);
        QCOMPARE(service.modelContextWindow(QStringLiteral("openai-chat"), QStringLiteral("gpt-4o")), 128000);
        QCOMPARE(service.modelContextWindow(QStringLiteral("anthropic-messages"), QStringLiteral("claude-sonnet-4-5")), 200000);
        QCOMPARE(service.modelContextWindow(QStringLiteral("ollama-chat"), QStringLiteral("qwen2.5:7b")), 32768);
    }

    // docs/15 §6.1.1: modelContextWindow returns 0 for an unknown protocol or
    // model so the caller falls back to the global contextTokens. 厂商名不是协议
    // 键，同样返回 0（调用方必须传 profile 的 apiProtocol）。
    void testModelContextWindowUnknownModel() {
        SettingsGuard guard;
        SettingsService service(nullptr);
        QCOMPARE(service.modelContextWindow(QStringLiteral("openai-chat"), QStringLiteral("nonexistent-model")), 0);
        QCOMPARE(service.modelContextWindow(QStringLiteral("unknown-protocol"), QStringLiteral("any")), 0);
        QCOMPARE(service.modelContextWindow(QStringLiteral("openai"), QStringLiteral("gpt-4o")), 0);
    }

    // docs/15 §6.1.1: resolveContextLimit three-level priority:
    //   1. Profile-level contextLength override (highest)
    //   2. Capability-table contextWindow (default per model)
    //   3. Global contextTokens (fallback for unknown models)
    void testResolveContextLimitPriority() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("modelProfiles");
            s.remove("activeProfileId");
            s.remove("contextTokens");
            // Clear legacy provider-scoped keys so migrateLegacy falls back to
            // the protocol's default baseUrl (no model is preset any more).
            for (const QString& p : {QStringLiteral("openai"),
                                     QStringLiteral("anthropic"),
                                     QStringLiteral("ollama")}) {
                s.remove(p + QStringLiteral("/model"));
                s.remove(p + QStringLiteral("/baseUrl"));
                s.remove(p + QStringLiteral("/apiKey"));
            }
        }
        SettingsService service(nullptr);
        const QString profileId = service.activeProfileId();
        QVERIFY(!profileId.isEmpty());

        // Priority 1: Profile-level contextLength override.
        service.updateProfile(profileId, QVariantMap{{QStringLiteral("contextLength"), 99999}});
        QCOMPARE(service.resolveContextLimit(), 99999);

        // Priority 2: capability table (after clearing the override). 模型不再
        // 预填，所以用已知模型把能力表那一条跑出来（gpt-4o → 128000）。
        service.updateProfile(profileId, QVariantMap{{QStringLiteral("contextLength"), QVariant()},
                                                     {QStringLiteral("model"), QStringLiteral("gpt-4o")}});
        QCOMPARE(service.resolveContextLimit(), 128000);

        // Priority 3: global fallback (unknown model, no override).
        service.updateProfile(profileId, QVariantMap{{QStringLiteral("model"), QStringLiteral("unknown-model")}});
        QCOMPARE(service.resolveContextLimit(), service.contextTokens());
    }

    // docs/16 §13 AC-1: token_usage 表正确创建。SqliteMemory 构造后
    // token_usage 表可写入并聚合查询，证明 DDL 执行成功且列结构匹配。
    void testTokenUsageTableCreated() {
        SqliteMemory memory(QStringLiteral(":memory:"));
        QVariantMap rec;
        rec["requestId"] = quint64(1);
        rec["sessionId"] = QStringLiteral("s1");
        rec["model"] = QStringLiteral("gpt-4o");
        rec["inputTokens"] = 100;
        rec["outputTokens"] = 50;
        memory.logTokenUsage(rec);
        const QVariantMap stats = memory.tokenStatsDetailed(30);
        QCOMPARE(stats.value("totalCalls").toInt(), 1);
        QCOMPARE(stats.value("totalInputTokens").toLongLong(), qint64(100));
        QCOMPARE(stats.value("totalOutputTokens").toLongLong(), qint64(50));
    }

    // docs/16 §13 AC-9: logTokenUsage 写入分类明细，聚合查询回读一致
    // （现有 tokenStats 向后兼容，新明细表独立聚合）。
    void testLogTokenUsage() {
        SqliteMemory memory(QStringLiteral(":memory:"));
        QVariantMap rec;
        rec["requestId"] = quint64(1);
        rec["sessionId"] = QStringLiteral("s1");
        rec["model"] = QStringLiteral("gpt-4o");
        rec["inputTokens"] = 200;
        rec["outputTokens"] = 80;
        rec["cacheHitTokens"] = 20;
        rec["inputSystemPrompt"] = 50;
        rec["inputConversation"] = 100;
        rec["inputToolCall"] = 30;
        rec["inputOther"] = 20;
        rec["outputConversation"] = 60;
        rec["outputToolCall"] = 20;
        rec["cacheHitSystemPrompt"] = 10;
        rec["cacheHitConversation"] = 10;
        rec["costCents"] = qint64(15);
        rec["latencyMs"] = qint64(120);
        memory.logTokenUsage(rec);
        const QVariantMap stats = memory.tokenStatsDetailed(30);
        QCOMPARE(stats.value("totalCalls").toInt(), 1);
        QCOMPARE(stats.value("totalInputTokens").toLongLong(), qint64(200));
        QCOMPARE(stats.value("totalOutputTokens").toLongLong(), qint64(80));
        QCOMPARE(stats.value("totalCacheHitTokens").toLongLong(), qint64(20));
        QCOMPARE(stats.value("totalInputSystemPrompt").toLongLong(), qint64(50));
        QCOMPARE(stats.value("totalInputConversation").toLongLong(), qint64(100));
        QCOMPARE(stats.value("totalInputToolCall").toLongLong(), qint64(30));
        QCOMPARE(stats.value("totalInputOther").toLongLong(), qint64(20));
        QCOMPARE(stats.value("totalOutputConversation").toLongLong(), qint64(60));
        QCOMPARE(stats.value("totalOutputToolCall").toLongLong(), qint64(20));
        QCOMPARE(stats.value("totalCacheHitSystemPrompt").toLongLong(), qint64(10));
        QCOMPARE(stats.value("totalCacheHitConversation").toLongLong(), qint64(10));
    }

    // docs/16 §13 AC-4: 来源拆分总和 = 输入合计（方向/来源拆分一致）。
    void testTokenStatsDetailed() {
        SqliteMemory memory(QStringLiteral(":memory:"));
        for (int i = 0; i < 2; ++i) {
            QVariantMap rec;
            rec["requestId"] = quint64(i + 1);
            rec["sessionId"] = QStringLiteral("s1");
            rec["model"] = QStringLiteral("gpt-4o");
            rec["inputTokens"] = 100;
            rec["inputSystemPrompt"] = 30;
            rec["inputConversation"] = 50;
            rec["inputToolCall"] = 20;
            rec["outputTokens"] = 40;
            rec["outputConversation"] = 40;
            memory.logTokenUsage(rec);
        }
        const QVariantMap stats = memory.tokenStatsDetailed(30);
        QCOMPARE(stats.value("totalCalls").toInt(), 2);
        QCOMPARE(stats.value("totalInputTokens").toLongLong(), qint64(200));
        // AC-4: 输入来源拆分总和 = 输入合计
        const qint64 sourceSum =
              stats.value("totalInputSystemPrompt").toLongLong()
            + stats.value("totalInputConversation").toLongLong()
            + stats.value("totalInputToolCall").toLongLong()
            + stats.value("totalInputMcp").toLongLong()
            + stats.value("totalInputSkill").toLongLong()
            + stats.value("totalInputImage").toLongLong()
            + stats.value("totalInputOther").toLongLong();
        QCOMPARE(sourceSum, stats.value("totalInputTokens").toLongLong());
        // tokenStatsBySource 各行 inputTokens 之和同样等于输入合计
        const QVariantList bySource = memory.tokenStatsBySource(30);
        qint64 sourceInputSum = 0;
        for (const auto& entry : bySource)
            sourceInputSum += entry.toMap().value("inputTokens").toLongLong();
        QCOMPARE(sourceInputSum, stats.value("totalInputTokens").toLongLong());
    }

    // docs/16 §13 AC-6: 成本核算正确（按模型定价计算）。
    void testCostStats() {
        SqliteMemory memory(QStringLiteral(":memory:"));
        // gpt-4o 预置定价: input 250 / output 1000 / cacheHit 125 (分/1M token)
        int inP = 0, outP = 0, chP = 0;
        PricingCalculator::getPricing(QStringLiteral("gpt-4o"), inP, outP, chP);
        QCOMPARE(inP, 250);
        QCOMPARE(outP, 1000);
        QCOMPARE(chP, 125);
        // 1M 输入 + 0.5M 输出 + 0 缓存 → 250 + 500 = 750 分
        const qint64 cost = PricingCalculator::calculateCost(
            QStringLiteral("gpt-4o"), 1000000, 500000, 0, inP, outP, chP);
        QCOMPARE(cost, qint64(750));
        // 含缓存命中: 1M 输入(含 0.2M 缓存) + 0.5M 输出
        // → (1M-0.2M)*250/1M + 0.2M*125/1M + 0.5M*1000/1M = 200 + 25 + 500 = 725
        const qint64 costCache = PricingCalculator::calculateCost(
            QStringLiteral("gpt-4o"), 1000000, 500000, 200000, inP, outP, chP);
        QCOMPARE(costCache, qint64(725));

        QVariantMap rec;
        rec["requestId"] = quint64(1);
        rec["sessionId"] = QStringLiteral("s1");
        rec["model"] = QStringLiteral("gpt-4o");
        rec["inputTokens"] = 1000000;
        rec["outputTokens"] = 500000;
        rec["cacheHitTokens"] = 0;
        rec["costCents"] = cost;
        memory.logTokenUsage(rec);
        const QVariantMap cs = memory.costStats(30);
        QCOMPARE(cs.value("totalCostCents").toLongLong(), qint64(750));
        const QVariantList byModel = cs.value("byModel").toList();
        QVERIFY(!byModel.isEmpty());
        QCOMPARE(byModel.at(0).toMap().value("model").toString(), QStringLiteral("gpt-4o"));
        QCOMPARE(byModel.at(0).toMap().value("costCents").toLongLong(), qint64(750));
        // 本地模型定价 0（docs/16 §6.2）—— 仍统计 token，但不产生费用
        int ollamaIn = 1, ollamaOut = 1, ollamaCh = 1;
        PricingCalculator::getPricing(QStringLiteral("qwen2.5:7b"),
                                      ollamaIn, ollamaOut, ollamaCh);
        QCOMPARE(ollamaIn, 0);
        QCOMPARE(ollamaOut, 0);
        QCOMPARE(ollamaCh, 0);
    }

    // docs/16 §13 AC-7: 配额设置属性读写 + 信号 + 持久化。
    void testQuotaSettings() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("dailyTokenQuota");
            s.remove("dailyCostQuotaCents");
            s.remove("monthlyTokenQuota");
            s.remove("monthlyCostQuotaCents");
            s.remove("strictQuotaMode");
        }
        {
            SettingsService service(nullptr);
            QCOMPARE(service.dailyTokenQuota(), 0);
            QCOMPARE(service.dailyCostQuotaCents(), 0);
            QCOMPARE(service.monthlyTokenQuota(), 0);
            QCOMPARE(service.monthlyCostQuotaCents(), 0);
            QCOMPARE(service.strictQuotaMode(), false);

            QSignalSpy tokenSpy(&service, &SettingsService::dailyTokenQuotaChanged);
            QSignalSpy strictSpy(&service, &SettingsService::strictQuotaModeChanged);
            service.setDailyTokenQuota(50000);
            QCOMPARE(tokenSpy.count(), 1);
            QCOMPARE(service.dailyTokenQuota(), 50000);
            service.setDailyCostQuotaCents(2000);
            QCOMPARE(service.dailyCostQuotaCents(), 2000);
            service.setMonthlyTokenQuota(500000);
            QCOMPARE(service.monthlyTokenQuota(), 500000);
            service.setMonthlyCostQuotaCents(20000);
            QCOMPARE(service.monthlyCostQuotaCents(), 20000);
            service.setStrictQuotaMode(true);
            QCOMPARE(strictSpy.count(), 1);
            QCOMPARE(service.strictQuotaMode(), true);
        }
        // 持久化跨实例
        {
            SettingsService s2(nullptr);
            QCOMPARE(s2.dailyTokenQuota(), 50000);
            QCOMPARE(s2.dailyCostQuotaCents(), 2000);
            QCOMPARE(s2.monthlyTokenQuota(), 500000);
            QCOMPARE(s2.monthlyCostQuotaCents(), 20000);
            QCOMPARE(s2.strictQuotaMode(), true);
        }
        // 清理：guard 不覆盖这些新键，手动清除避免泄漏到后续套件
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("dailyTokenQuota");
            s.remove("dailyCostQuotaCents");
            s.remove("monthlyTokenQuota");
            s.remove("monthlyCostQuotaCents");
            s.remove("strictQuotaMode");
        }
    }

private:
    static void clearThemeKeys() {
        QSettings s("NexusAI", "NexusAI");
        s.remove("prefs/v1/ui.themeMode");
        s.remove("theme/dark");
    }
};

// Sampling-param normalization lives in test_sampling_params (separate
// executable): Qt documents qExec as single-call per test application, and
// two calls made CLI options / XML output ambiguous.

int main(int argc, char *argv[]) {
    QCoreApplication::setOrganizationName("NexusAI");
    QCoreApplication::setApplicationName("NexusAI");
    // QML combo-echo tests instantiate QtQuick items: QGuiApplication is
    // required (QCoreApplication would abort on item creation).
    QGuiApplication app(argc, argv);
    TestSettingsService tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_settings.moc"
