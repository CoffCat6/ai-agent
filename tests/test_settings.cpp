// SettingsService multi-model profiles, defaults completion, model switching,
// addition, removal and persistence coverage.
#include "viewmodels/SettingsService.h"
#include "core/utils/SettingsSchema.h"
#include "viewmodels/ThemeManager.h"
#include "core/tools/ToolRegistry.h"
#include "core/tools/WebSearchBackend.h"

#include "core/memory/SqliteMemory.h"
#include "core/utils/Logger.h"
#include "core/utils/PricingCalculator.h"

#include <QtTest>
#include <QSet>
#include <QHash>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStyleHints>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QSettings>
#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QStandardPaths>
#include "viewmodels/PreferenceStore.h"
#include "viewmodels/PreferenceRegistry.h"
#include "viewmodels/SettingsCatalog.h"
#include "core/utils/SettingsRepository.h"

#include <memory>
#include "TestSettingsIsolation.h"

// 测试进程的 QSettings 重定向到临时 INI（见 TestSettingsIsolation.h），
// 防止测试改写用户真实设置。
namespace {
const NexusAI::TestSupport::SettingsIsolation kSettingsIsolation;
} // namespace

using namespace NexusAI;

namespace {

class SettingsGuard {
public:
    SettingsGuard() {
        QSettings s = NexusAI::TestSupport::testSettings();
        rawProfiles = s.value("modelProfiles");
        activeId = s.value("activeProfileId");
        // Also guard the keys suites actually write; without this the tests
        // leaked settings into the user's real app (e.g. logging/level,
        // embedding/model, the security/allowFullDiskRead sandbox switch).
        loggingLevel = s.value("logging/level");
        loggingDirectory = s.value("logging/directory");
        loggingMaxFileSizeMb = s.value("logging/maxFileSizeMb");
        loggingRetentionFiles = s.value("logging/retentionFiles");
        embeddingModel = s.value("embedding/model");
        embeddingBaseUrl = s.value("embedding/baseUrl");
        // 任务 #20：embedding provider/apiKey 行为测试需要守卫这两个键
        // （对齐 webSearch/provider、webSearch/*ApiKey 的守卫先例）。
        embeddingProvider = s.value("embedding/provider");
        embeddingApiKey = s.value("embedding/apiKey");
        allowFullDiskRead = s.value("security/allowFullDiskRead");
        localOnlyMode = s.value("security/localOnlyMode");
        allowLocalNetworkFetch = s.value("security/allowLocalNetworkFetch");
        systemPromptOverride = s.value("agent/systemPromptOverride");
        toolsDisabled = s.value("tools/disabled");
        webSearchProvider = s.value("webSearch/provider");
        // 密钥按 provider 分桶：两把独立的 key 都要进守卫。
        zhipuApiKey = s.value("webSearch/zhipuApiKey");
        deepseekApiKey = s.value("webSearch/deepseekApiKey");
        // H1: proxy settings (auth is a DPAPI blob).
        proxyMode = s.value("network/proxyMode");
        proxyHost = s.value("network/proxyHost");
        proxyPort = s.value("network/proxyPort");
        proxyAuth = s.value("network/proxyAuth");
        // B1: prompt asset keys (seeding tests remove and rewrite them).
        promptPresets = s.value("promptPresets");
        slashCommands = s.value("slashCommands");
        sessionTemplates = s.value("sessionTemplates");
        schemaVersion = s.value("schemaVersion");
        defaultPermissionMode = s.value("agent/defaultPermissionMode");
        readDenyList = s.value("security/readDenyList");
        // 设置中心拆页后新增的按页复位用例会写记忆设置，必须一并守卫，
        // 否则测试会把用户真实配置改掉。
        episodicMemoryEnabled = s.value("memory/episodicMemoryEnabled");
        auditRetentionDays = s.value("memory/auditRetentionDays");
        tokenRetentionDays = s.value("memory/tokenRetentionDays");
        // 测试开始时把 schema 版本顶到最新：否则每个用例里第一次构造
        // SettingsService 都会跑一次 v1→v2 迁移（清空 modelProfiles 里的模型），
        // 让“先写 profile 再 load”的用例看到被迁移改过的数据。迁移本身由
        // schemaV1ProfilesLoseModelsOnUpgrade 单独覆盖。
        s.setValue("schemaVersion", SettingsSchema::kSchemaVersion);
    }
    ~SettingsGuard() {
        QSettings s = NexusAI::TestSupport::testSettings();
        if (rawProfiles.isValid()) s.setValue("modelProfiles", rawProfiles);
        else s.remove("modelProfiles");
        if (activeId.isValid()) s.setValue("activeProfileId", activeId);
        else s.remove("activeProfileId");
        if (loggingLevel.isValid()) s.setValue("logging/level", loggingLevel);
        else s.remove("logging/level");
        if (loggingDirectory.isValid()) s.setValue("logging/directory", loggingDirectory);
        else s.remove("logging/directory");
        if (loggingMaxFileSizeMb.isValid()) s.setValue("logging/maxFileSizeMb", loggingMaxFileSizeMb);
        else s.remove("logging/maxFileSizeMb");
        if (loggingRetentionFiles.isValid()) s.setValue("logging/retentionFiles", loggingRetentionFiles);
        else s.remove("logging/retentionFiles");
        if (embeddingModel.isValid()) s.setValue("embedding/model", embeddingModel);
        else s.remove("embedding/model");
        if (embeddingBaseUrl.isValid()) s.setValue("embedding/baseUrl", embeddingBaseUrl);
        else s.remove("embedding/baseUrl");
        if (embeddingProvider.isValid())
            s.setValue("embedding/provider", embeddingProvider);
        else s.remove("embedding/provider");
        if (embeddingApiKey.isValid())
            s.setValue("embedding/apiKey", embeddingApiKey);
        else s.remove("embedding/apiKey");
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
        if (zhipuApiKey.isValid())
            s.setValue("webSearch/zhipuApiKey", zhipuApiKey);
        else s.remove("webSearch/zhipuApiKey");
        if (deepseekApiKey.isValid())
            s.setValue("webSearch/deepseekApiKey", deepseekApiKey);
        else s.remove("webSearch/deepseekApiKey");
        if (proxyMode.isValid()) s.setValue("network/proxyMode", proxyMode);
        else s.remove("network/proxyMode");
        if (proxyHost.isValid()) s.setValue("network/proxyHost", proxyHost);
        else s.remove("network/proxyHost");
        if (proxyPort.isValid()) s.setValue("network/proxyPort", proxyPort);
        else s.remove("network/proxyPort");
        if (proxyAuth.isValid()) s.setValue("network/proxyAuth", proxyAuth);
        else s.remove("network/proxyAuth");
        if (promptPresets.isValid()) s.setValue("promptPresets", promptPresets);
        else s.remove("promptPresets");
        if (slashCommands.isValid()) s.setValue("slashCommands", slashCommands);
        else s.remove("slashCommands");
        if (sessionTemplates.isValid()) s.setValue("sessionTemplates", sessionTemplates);
        else s.remove("sessionTemplates");
        if (schemaVersion.isValid())
            s.setValue("schemaVersion", schemaVersion);
        else s.remove("schemaVersion");
        if (defaultPermissionMode.isValid())
            s.setValue("agent/defaultPermissionMode", defaultPermissionMode);
        else s.remove("agent/defaultPermissionMode");
        if (readDenyList.isValid())
            s.setValue("security/readDenyList", readDenyList);
        else s.remove("security/readDenyList");
        if (episodicMemoryEnabled.isValid())
            s.setValue("memory/episodicMemoryEnabled", episodicMemoryEnabled);
        else s.remove("memory/episodicMemoryEnabled");
        if (auditRetentionDays.isValid())
            s.setValue("memory/auditRetentionDays", auditRetentionDays);
        else s.remove("memory/auditRetentionDays");
        if (tokenRetentionDays.isValid())
            s.setValue("memory/tokenRetentionDays", tokenRetentionDays);
        else s.remove("memory/tokenRetentionDays");
        Logger::setLogDirectory(loggingDirectory.isValid()
            ? loggingDirectory.toString() : QString());
        Logger::setMaxFileSizeMb(loggingMaxFileSizeMb.isValid()
            ? loggingMaxFileSizeMb.toInt() : 10);
        Logger::setRetentionFiles(loggingRetentionFiles.isValid()
            ? loggingRetentionFiles.toInt() : 14);
        Logger::setMinLevel(Logger::levelFromName(
            loggingLevel.isValid() ? loggingLevel.toString() : QStringLiteral("info"), QtInfoMsg));
    }
    QVariant rawProfiles;
    QVariant activeId;
    QVariant loggingLevel;
    QVariant loggingDirectory;
    QVariant loggingMaxFileSizeMb;
    QVariant loggingRetentionFiles;
    QVariant embeddingModel;
    QVariant embeddingBaseUrl;
    QVariant embeddingProvider;
    QVariant embeddingApiKey;
    QVariant allowFullDiskRead;
    QVariant localOnlyMode;
    QVariant allowLocalNetworkFetch;
    QVariant systemPromptOverride;
    QVariant toolsDisabled;
    QVariant webSearchProvider;
    QVariant zhipuApiKey;
    QVariant deepseekApiKey;
    QVariant proxyMode;
    QVariant proxyHost;
    QVariant proxyPort;
    QVariant proxyAuth;
    QVariant promptPresets;
    QVariant slashCommands;
    QVariant sessionTemplates;
    QVariant schemaVersion;
    QVariant defaultPermissionMode;
    QVariant readDenyList;
    QVariant episodicMemoryEnabled;
    QVariant auditRetentionDays;
    QVariant tokenRetentionDays;
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
            QSettings s = NexusAI::TestSupport::testSettings();
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
            QSettings s = NexusAI::TestSupport::testSettings();
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
            QSettings s = NexusAI::TestSupport::testSettings();
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
            QSettings s = NexusAI::TestSupport::testSettings();
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
            QSettings s = NexusAI::TestSupport::testSettings();
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
            QSettings s = NexusAI::TestSupport::testSettings();
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
            QSettings s = NexusAI::TestSupport::testSettings();
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
            QSettings s = NexusAI::TestSupport::testSettings();
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
            QSettings s = NexusAI::TestSupport::testSettings();
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
            QSettings s = NexusAI::TestSupport::testSettings();
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
            QSettings s = NexusAI::TestSupport::testSettings();
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

    // docs/31 S4.4 N4（4a）：复制继承除密钥外的全部字段，顺序可拖拽持久化。
    void duplicateProfileStripsApiKey() {
        SettingsGuard guard;
        {
            QSettings s = NexusAI::TestSupport::testSettings();
            s.remove("modelProfiles");
            s.remove("activeProfileId");
        }

        SettingsService service(nullptr);
        const QString sourceId = service.activeProfileId();
        service.updateProfile(sourceId,
            {{QStringLiteral("displayName"), QStringLiteral("源配置")},
             {QStringLiteral("baseUrl"), QStringLiteral("http://gw.local/v1")},
             {QStringLiteral("model"), QStringLiteral("m1")},
             {QStringLiteral("models"), QStringList{QStringLiteral("m1"), QStringLiteral("m2")}},
             {QStringLiteral("apiKey"), QStringLiteral("sk-secret")},
             {QStringLiteral("temperature"), QStringLiteral("0.4")}});
        QCOMPARE(service.activeProfile().value("apiKey").toString(),
                 QStringLiteral("sk-secret"));

        const QString copyId = service.duplicateProfile(sourceId, QStringLiteral("副本 A"));
        QVERIFY(!copyId.isEmpty());
        QVERIFY(copyId != sourceId);
        const QVariantMap copy = profileById(service, copyId);
        QCOMPARE(copy.value("displayName").toString(), QStringLiteral("副本 A"));
        QVERIFY2(copy.value("apiKey").toString().isEmpty(),
                 "复制出的配置绝不能携带 API Key");
        QCOMPARE(copy.value("baseUrl").toString(), QStringLiteral("http://gw.local/v1"));
        QCOMPARE(copy.value("model").toString(), QStringLiteral("m1"));
        QCOMPARE(copy.value("models").toStringList(),
                 (QStringList{QStringLiteral("m1"), QStringLiteral("m2")}));
        QCOMPARE(copy.value("temperature").toString(), QStringLiteral("0.4"));
        // 复制不动当前启用项，源配置的密钥也不受影响。
        QCOMPARE(service.activeProfileId(), sourceId);
        QCOMPARE(service.activeProfile().value("apiKey").toString(),
                 QStringLiteral("sk-secret"));

        // 落盘回读：新配置仍在、仍无密钥。
        SettingsService reloaded(nullptr);
        const QVariantMap persisted = profileById(reloaded, copyId);
        QCOMPARE(persisted.value("displayName").toString(), QStringLiteral("副本 A"));
        QVERIFY(persisted.value("apiKey").toString().isEmpty());

        // 空名字回落到 "<原名称> 副本"；未知 id 返回空串。
        const QString unnamed = service.duplicateProfile(sourceId, QStringLiteral("   "));
        QCOMPARE(profileById(service, unnamed).value("displayName").toString(),
                 QStringLiteral("源配置 副本"));
        QVERIFY(service.duplicateProfile(QStringLiteral("missing"), QStringLiteral("x")).isEmpty());
    }

    void moveProfilePersistsOrder() {
        SettingsGuard guard;
        {
            QSettings s = NexusAI::TestSupport::testSettings();
            s.remove("modelProfiles");
            s.remove("activeProfileId");
        }

        SettingsService service(nullptr);
        const QString a = service.activeProfileId();
        const QString b = service.addProfile(QStringLiteral("anthropic"));
        const QString c = service.addProfile(QStringLiteral("ollama"));
        const auto ids = [](const SettingsService& svc) {
            QStringList out;
            for (const QVariant& entry : svc.profiles())
                out.append(entry.toMap().value("id").toString());
            return out;
        };
        QCOMPARE(ids(service), (QStringList{a, b, c}));

        service.moveProfile(0, 2);
        QCOMPARE(ids(service), (QStringList{b, c, a}));
        QCOMPARE(service.activeProfileId(), a);   // 排序不改启用项

        SettingsService reloaded(nullptr);
        QCOMPARE(ids(reloaded), (QStringList{b, c, a}));

        service.moveProfile(2, 0);
        QCOMPARE(ids(service), (QStringList{a, b, c}));
        // 非法索引是 no-op，不写盘也不发信号。
        QSignalSpy profilesSpy(&service, &SettingsService::profilesChanged);
        service.moveProfile(-1, 0);
        service.moveProfile(0, 3);
        service.moveProfile(1, 1);
        QCOMPARE(profilesSpy.count(), 0);
        QCOMPARE(ids(service), (QStringList{a, b, c}));
    }

    // docs/31 S4.4 N4：切换活动配置（设置页「启用」按钮与托盘共用同一条写入
    // 路径；托盘入口已取消，数据层语义不变）只写全局 activeProfileId，
    // 重复点当前项与未知 id 都是 no-op，切换结果跨实例保留。
    void activeProfileSwitchPersists() {
        SettingsGuard guard;
        {
            QSettings s = NexusAI::TestSupport::testSettings();
            s.remove("modelProfiles");
            s.remove("activeProfileId");
        }

        SettingsService service(nullptr);
        const QString first = service.activeProfileId();
        const QString second = service.addProfile(QStringLiteral("anthropic"));
        QVERIFY(!second.isEmpty());
        QCOMPARE(service.activeProfileId(), first);

        QSignalSpy activeSpy(&service, &SettingsService::activeProfileChanged);
        service.setActiveProfile(second);
        QCOMPARE(activeSpy.count(), 1);
        QCOMPARE(service.activeProfileId(), second);
        service.setActiveProfile(second);
        QCOMPARE(activeSpy.count(), 1);
        service.setActiveProfile(QStringLiteral("missing"));
        QCOMPARE(service.activeProfileId(), second);

        SettingsService reloaded(nullptr);
        QCOMPARE(reloaded.activeProfileId(), second);
    }

    // 调用方显式给了 baseUrl / model / models，就以它为准，不再自动回填。
    void explicitEndpointValuesWin() {
        SettingsGuard guard;
        {
            QSettings s = NexusAI::TestSupport::testSettings();
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
            QSettings s = NexusAI::TestSupport::testSettings();
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
            QSettings s = NexusAI::TestSupport::testSettings();
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
            QSettings s = NexusAI::TestSupport::testSettings();
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
            QSettings s = NexusAI::TestSupport::testSettings();
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

        service.setDefaultPermissionMode(QStringLiteral("plan_only"));
        QCOMPARE(service.defaultPermissionMode(), QStringLiteral("plan_only"));

        // 非法值：拒绝且不发信号
        service.setDefaultPermissionMode(QStringLiteral("yolo"));
        QCOMPARE(service.defaultPermissionMode(), QStringLiteral("plan_only"));
        QCOMPARE(spy.count(), 3);

        // 重启语义：新实例从 QSettings 读回
        {
            QSettings s = NexusAI::TestSupport::testSettings();
            s.setValue("agent/defaultPermissionMode", QStringLiteral("auto_edit"));
        }
        SettingsService reloaded(nullptr);
        QCOMPARE(reloaded.defaultPermissionMode(), QStringLiteral("auto_edit"));
    }

    // 设置中心拆页后，原 resetGeneralToDefaults() 拆成按领域的五个入口。
    // 每个用例锁定两件事：自己的键回写为 schema 默认值；不碰其他页的键。
    void resetAgentDefaultsMatchesSchema() {
        SettingsGuard guard;
        SettingsService svc(nullptr);

        svc.setSystemPromptOverride(QStringLiteral("custom prompt"));
        // 其他领域保持非默认值，验证复位不越界。
        svc.setLogLevel(QStringLiteral("debug"));
        svc.setProxyMode(QStringLiteral("manual"));
        svc.setEmbeddingModel(QStringLiteral("bge-m3"));

        svc.resetAgentToDefaults();

        QCOMPARE(svc.systemPromptOverride(),
                 SettingsSchema::defaultValue(
                     QStringLiteral("agent/systemPromptOverride")).toString());
        QCOMPARE(svc.logLevel(), QStringLiteral("debug"));
        QCOMPARE(svc.proxyMode(), QStringLiteral("manual"));
        QCOMPARE(svc.embeddingModel(), QStringLiteral("bge-m3"));
    }

    void resetLoggingDefaultsMatchesSchema() {
        SettingsGuard guard;
        SettingsService svc(nullptr);

        svc.setLogLevel(QStringLiteral("debug"));
        svc.setLogDirectory(QStringLiteral("/tmp/nexus-test-logs"));
        svc.setLogMaxFileSizeMb(32);
        svc.setLogRetentionFiles(30);
        // 其他领域保持非默认值。
        svc.setProxyHost(QStringLiteral("proxy.example"));

        svc.resetLoggingToDefaults();

        QCOMPARE(svc.logLevel(),
                 SettingsSchema::defaultValue(QStringLiteral("logging/level")).toString());
        QCOMPARE(NexusAI::TestSupport::testSettings().value("logging/directory").toString(),
                 QString());
        QCOMPARE(svc.logMaxFileSizeMb(),
                 SettingsSchema::defaultValue(
                     QStringLiteral("logging/maxFileSizeMb")).toInt());
        QCOMPARE(svc.logRetentionFiles(),
                 SettingsSchema::defaultValue(
                     QStringLiteral("logging/retentionFiles")).toInt());
        QCOMPARE(svc.proxyHost(), QStringLiteral("proxy.example"));
    }

    void resetNetworkDefaultsMatchesSchema() {
        SettingsGuard guard;
        SettingsService svc(nullptr);

        svc.setProxyMode(QStringLiteral("manual"));
        svc.setProxyHost(QStringLiteral("proxy.example"));
        svc.setProxyPort(3128);
        svc.setProxyUsername(QStringLiteral("alice"));
        svc.setProxyPassword(QStringLiteral("secret"));
        // 其他领域保持非默认值。
        svc.setLogLevel(QStringLiteral("debug"));

        svc.resetNetworkToDefaults();

        QCOMPARE(svc.proxyMode(),
                 SettingsSchema::defaultValue(QStringLiteral("network/proxyMode")).toString());
        QCOMPARE(svc.proxyHost(),
                 SettingsSchema::defaultValue(QStringLiteral("network/proxyHost")).toString());
        QCOMPARE(svc.proxyPort(),
                 SettingsSchema::defaultValue(QStringLiteral("network/proxyPort")).toInt());
        QCOMPARE(svc.proxyUsername(), QString());
        QVERIFY(!svc.proxyPasswordSet());
        QCOMPARE(svc.logLevel(), QStringLiteral("debug"));
    }

    void resetEmbeddingDefaultsMatchesSchema() {
        SettingsGuard guard;
        SettingsService svc(nullptr);

        svc.setEmbeddingProvider(QStringLiteral("openai"));
        svc.setEmbeddingModel(QStringLiteral("text-embedding-3-large"));
        svc.setEmbeddingBaseUrl(QStringLiteral("https://api.example.com/v1"));
        svc.setEmbeddingApiKey(QStringLiteral("sk-reset-test"));
        const bool keyBefore = svc.embeddingApiKeySet();
        // 其他领域保持非默认值。
        svc.setSystemPromptOverride(QStringLiteral("custom prompt"));

        svc.resetEmbeddingToDefaults();

        QCOMPARE(svc.embeddingProvider(),
                 SettingsSchema::defaultValue(QStringLiteral("embedding/provider")).toString());
        QCOMPARE(svc.embeddingModel(),
                 SettingsSchema::defaultValue(QStringLiteral("embedding/model")).toString());
        QCOMPARE(svc.embeddingBaseUrl(),
                 SettingsSchema::defaultValue(
                     QStringLiteral("embedding/baseUrl")).toString());
        // 密钥是破坏性数据：恢复默认不清空，由页面「清除密钥」显式处理。
        QCOMPARE(svc.embeddingApiKeySet(), keyBefore);
        QCOMPARE(svc.systemPromptOverride(), QStringLiteral("custom prompt"));
    }

    void resetMemoryDefaultsMatchesSchema() {
        SettingsGuard guard;
        SettingsService svc(nullptr);

        svc.setEpisodicMemoryEnabled(true);
        svc.setAuditRetentionDays(5);
        svc.setTokenRetentionDays(7);
        // 其他领域保持非默认值。
        svc.setEmbeddingModel(QStringLiteral("bge-m3"));

        svc.resetMemoryToDefaults();

        QCOMPARE(svc.episodicMemoryEnabled(),
                 SettingsSchema::defaultValue(
                     QStringLiteral("memory/episodicMemoryEnabled")).toBool());
        QCOMPARE(svc.auditRetentionDays(),
                 SettingsSchema::defaultValue(
                     QStringLiteral("memory/auditRetentionDays")).toInt());
        QCOMPARE(svc.tokenRetentionDays(),
                 SettingsSchema::defaultValue(
                     QStringLiteral("memory/tokenRetentionDays")).toInt());
        QCOMPARE(svc.embeddingModel(), QStringLiteral("bge-m3"));
    }

    // 两级导航的数据契约：分组覆盖全部分类且组内不重不漏；分类都有已知
    // 分组、全局 order 唯一。导航/搜索依赖这两点，分组只是展示层，不改变
    // 任何存储键，但漏登一个分类会让设置页在侧栏消失。
    void categoryGroupsCoverAllCategories() {
        auto* reg = PreferenceRegistry::instance();
        QVERIFY(reg);

        const QStringList groups = reg->categoryGroups();
        QVERIFY(groups.size() >= 2);
        QSet<QString> groupSet;
        for (const QString& groupId : groups)
            groupSet.insert(groupId);
        QCOMPARE(groupSet.size(), groups.size());

        const QStringList categories = reg->categories();
        QVERIFY(!categories.isEmpty());
        QSet<int> orders;
        QSet<QString> grouped;
        for (const QString& id : categories) {
            const QString groupId = reg->categoryGroup(id);
            QVERIFY2(!groupId.isEmpty(), qPrintable(id));
            QVERIFY2(groups.contains(groupId), qPrintable(id + " -> " + groupId));
            QVERIFY2(!reg->categoryGroupName(groupId).isEmpty(), qPrintable(groupId));
            grouped.insert(id);
            orders.insert(reg->categoryOrder(id));
        }
        QCOMPARE(orders.size(), categories.size());

        QSet<QString> inGroupSet;
        for (const QString& groupId : groups) {
            const QStringList members = reg->categoriesInGroup(groupId);
            QVERIFY2(!members.isEmpty(), qPrintable(groupId));
            for (const QString& id : members)
                inGroupSet.insert(id);
        }
        QCOMPARE(inGroupSet.size(), categories.size());
        QCOMPARE(inGroupSet, grouped);
    }

    // docs/17 B3: 账本完整性——三类旁路写入方（ToolRegistry/AgentViewModel/
    // PreferenceStore）的键必须全部登记进 schema，防止"唯一事实源"再漂移。

    // B1: 默认资产种子——键未写入时种入（数量/固定 id/命令词不随 locale 变），
    // 键已存在（含用户清空的 "[]"）绝不回灌。
    void promptAssetsSeededOnceAndDeletionRespected() {
        {
            QSettings s = NexusAI::TestSupport::testSettings();
            s.remove("promptPresets");
            s.remove("slashCommands");
            s.remove("sessionTemplates");
        }
        {
            SettingsService service(nullptr);
            QCOMPARE(service.promptPresets().size(), 4);
            QCOMPARE(service.slashCommands().size(), 4);
            QCOMPARE(service.sessionTemplates().size(), 3);
            QStringList names;
            for (const QVariant& c : service.slashCommands())
                names << c.toMap().value("name").toString();
            QCOMPARE(names, (QStringList{"review", "explain", "translate", "commit"}));
            // 代码审查模板默认只读计划档
            for (const QVariant& t : service.sessionTemplates()) {
                const QVariantMap m = t.toMap();
                if (m.value("id").toString() == "default-tmpl-code-review")
                    QCOMPARE(m.value("permissionMode").toInt(), 3);   // PlanOnly
            }
            // 删掉一条默认预设
            service.removePromptPreset("default-preset-reviewer");
            QCOMPARE(service.promptPresets().size(), 3);
        }
        {
            // 重启：键已存在，不回灌被删的默认
            SettingsService service(nullptr);
            QCOMPARE(service.promptPresets().size(), 3);
            QCOMPARE(service.slashCommands().size(), 4);
            // 用户清空指令列表 → "[]"，仍是空，不回灌
            for (const QVariant& c : service.slashCommands())
                service.removeSlashCommand(c.toMap().value("id").toString());
            QCOMPARE(service.slashCommands().size(), 0);
        }
        {
            SettingsService service(nullptr);
            QCOMPARE(service.slashCommands().size(), 0);
        }
    }

    // docs/33 E2: 子代理角色档案——整表替换写入、重启读回、复位清空。
    void subagentRolesRoundTripAndReset() {
        {
            SettingsService service(nullptr);
            QCOMPARE(service.subagentRoles().size(), 0);
            const QVariantList roles{
                QVariantMap{{QStringLiteral("name"), QStringLiteral("auditor")},
                            {QStringLiteral("prompt"), QStringLiteral("审计优先")},
                            {QStringLiteral("background"), true},
                            {QStringLiteral("tools"),
                             QVariantList{QStringLiteral("read_file")}}},
                QVariantMap{{QStringLiteral("name"), QStringLiteral("triage")},
                            {QStringLiteral("prompt"),
                             QStringLiteral("先分类再深入")}}};
            service.setSubagentRoles(roles);
            QCOMPARE(service.subagentRoles().size(), 2);
        }
        {
            SettingsService service(nullptr);
            QCOMPARE(service.subagentRoles().size(), 2);
            QCOMPARE(service.subagentRoles().first().toMap()
                         .value("name").toString(), QStringLiteral("auditor"));
            // 角色级默认后台（docs/35）随 JSON 资产原样存回。
            QCOMPARE(service.subagentRoles().first().toMap()
                         .value("background").toBool(), true);
            // 复位智能体行为 → 自定义档案清空（预设内建于工具，不受影响）。
            service.resetAgentToDefaults();
            QCOMPARE(service.subagentRoles().size(), 0);
        }
    }

    // 2026-11-03：内置角色默认后台开关——持久化并在恢复智能体默认时复位。
    void subagentPresetBackgroundRoundTripAndReset() {
        {
            SettingsService service(nullptr);
            QCOMPARE(service.subagentPresetBackground(), false);
            service.setSubagentPresetBackground(true);
            QVERIFY(service.subagentPresetBackground());
        }
        {
            SettingsService service(nullptr);
            QVERIFY(service.subagentPresetBackground());
            service.resetAgentToDefaults();
            QVERIFY(!service.subagentPresetBackground());
        }
    }

    void schemaCoversBypassWriters() {
        const auto& keys = SettingsSchema::registeredKeys();
        QSet<QString> registered;
        for (const auto& k : keys) registered.insert(k.key);

        const QStringList expected = {
            // SettingsService-owned values
            "modelProfiles", "activeProfileId", "contextTokens",
            // 提示词与指令：三组用户自建资产（SettingsService JSON blob）
            "promptPresets", "slashCommands", "sessionTemplates",
            // docs/32 P1: 子代理设置键（步数/超时已于 2026-11-01 移除）
            "agent/subagentEnabled", "agent/subagentMaxParallel",
            "agent/subagentPresetBackground",
            // docs/33 E2: 子代理角色档案（复合 JSON 资产）
            "agent/subagentRoles",
            "security/allowFullDiskRead", "security/allowProjectWrite",
            "security/localOnlyMode", "security/allowLocalNetworkFetch",
            "security/readDenyList", "projects/activeId",
            "embedding/model", "embedding/baseUrl", "embedding/provider",
            "embedding/apiKey", "agent/systemPromptOverride",
            "logging/level", "logging/directory", "logging/maxFileSizeMb",
            "logging/retentionFiles", "webSearch/provider",
            "webSearch/zhipuApiKey", "webSearch/deepseekApiKey",
            "memory/episodicMemoryEnabled",
            // H1: proxy settings + encrypted credential blob
            "network/proxyMode", "network/proxyHost", "network/proxyPort",
            "network/proxyAuth",
            "memory/auditRetentionDays", "memory/tokenRetentionDays",
            "memory/backupOnStartup", "memory/backupKeepCount",
            // docs/31 S4.2 N6: 定时备份间隔（0 = 关闭，仅 0/1/6/24/168）
            "memory/backupIntervalHours",
            "dailyTokenQuota", "dailyCostQuotaCents", "monthlyTokenQuota",
            "monthlyCostQuotaCents", "strictQuotaMode", "agent/defaultPermissionMode",
            // docs/31 S4.1 N9: 用量告警阈值（0 = 关闭）
            "usage/alertPercent",
            // ToolRegistry and schema migration
            "tools/disabled", "schemaVersion", "theme/dark",
            // AgentViewModel runtime layout
            "ui/sidebarWidth", "ui/panelWidth",
            "ui/panelVisible", "ui/sidebarExpanded", "ui/showProcess",
            "ui/windowX", "ui/windowY", "ui/windowWidth", "ui/windowHeight",
            "ui/windowMaximized",
            // M2: PreferenceStore 物理键（prefs/v1/<key>）
            "prefs/v1/ui.themeMode", "prefs/v1/ui.fontSans",
            "prefs/v1/ui.fontMono", "prefs/v1/ui.baseFontSize",
            "prefs/v1/ui.locale",
            "prefs/v1/ui.defaultSidebarWidth", "prefs/v1/ui.defaultPanelWidth",
            "prefs/v1/ui.defaultSidebarExpanded", "prefs/v1/ui.defaultPanelVisible",
            "prefs/v1/ui.restoreWindowOnStartup",
            // docs/31 S4.7 O10: 减少动效开关（ThemeProvider 动效令牌归零）
            "prefs/v1/ui.reduceMotion",
            // docs/31 S4.1 O8: 设置中心尺寸记忆（隐藏键）
            "prefs/v1/ui.settingsDialogWidth", "prefs/v1/ui.settingsDialogHeight",
            // docs/31 N2: 快捷键覆盖项（ShortcutMap 写入的 JSON）
            "prefs/v1/ui.shortcuts",
            // docs/31 N1: 首次启动向导标记（Onboarding 写入，隐藏键）
            "prefs/v1/general.onboarded",
            // docs/20 工作流 B（桌面集成）偏好键
            "prefs/v1/window.closeToTray", "prefs/v1/general.autoStart",
            "prefs/v1/notice.enabled",
            // M2 内部键：PreferenceStore 写入的物理路径
            "prefs/v1/settings.lastCategory",
            "prefs/v1/settings.collapsedGroups",
            // Legacy dark-mode migration key remains in the catalog.
            "theme/dark",
            // docs/17 A0/A1
            "projects/activeId",
            "security/readDenyList",
        };
        QSet<QString> expectedSet;
        for (const QString& key : expected) {
            expectedSet.insert(key);
            QVERIFY2(registered.contains(key), qPrintable(
                QStringLiteral("settings key not in schema ledger: %1").arg(key)));
        }
        QVERIFY2(registered == expectedSet,
                 "schema ledger has keys missing from the audited key list");
    }

    void preferenceAndStorageMetadataShareOneSchema() {
        const auto* registry = PreferenceRegistry::instance();
        QVERIFY(registry);
        QCOMPARE(registry->categories().size(), SettingsSchema::categories().size());

        auto storageType = [](PreferenceType type) {
            switch (type) {
            case PreferenceType::Bool: return SettingValueType::Bool;
            case PreferenceType::Int: return SettingValueType::Int;
            case PreferenceType::Enum:
            case PreferenceType::String: return SettingValueType::String;
            }
            return SettingValueType::Any;
        };
        for (const PreferenceMeta& preference : SettingsSchema::preferences()) {
            const PreferenceMeta* registered = registry->meta(preference.key);
            QVERIFY2(registered, qPrintable(QStringLiteral("unregistered preference: %1")
                                                .arg(preference.key)));
            QCOMPARE(registered->categoryId, preference.categoryId);
            QCOMPARE(registered->displayName, preference.displayName);
            QCOMPARE(registered->type, preference.type);
            QCOMPARE(registered->constraint.options, preference.constraint.options);
            QCOMPARE(registered->constraint.min, preference.constraint.min);
            QCOMPARE(registered->constraint.max, preference.constraint.max);
            QCOMPARE(registered->constraint.hasRange, preference.constraint.hasRange);

            const SettingsKeyDef* persisted = SettingsSchema::definition(
                QStringLiteral("prefs/v1/") + preference.key);
            QVERIFY(persisted);
            QCOMPARE(persisted->type, storageType(preference.type));
            if (preference.defaultValue.isValid())
                QCOMPARE(persisted->defaultValue, preference.defaultValue);
        }

        QSet<QString> uniqueKeys;
        for (const SettingsKeyDef& definition : SettingsSchema::registeredKeys()) {
            QVERIFY2(!uniqueKeys.contains(definition.key), qPrintable(
                QStringLiteral("duplicate settings key: %1").arg(definition.key)));
            uniqueKeys.insert(definition.key);
        }
        QCOMPARE(SettingsSchema::clampInt(QStringLiteral("prefs/v1/ui.baseFontSize"), 100), 24);
        QCOMPARE(SettingsSchema::defaultValue(QStringLiteral("logging/maxFileSizeMb")).toInt(),
                 SettingsSchema::kDefaultLogMaxFileSizeMb);
        QCOMPARE(SettingsSchema::defaultValue(QStringLiteral("logging/retentionFiles")).toInt(),
                 SettingsSchema::kDefaultLogRetentionFiles);
        QCOMPARE(SettingsSchema::clampInt(QStringLiteral("logging/maxFileSizeMb"), 0), 1);
        QVERIFY(SettingsSchema::isValidValue(QStringLiteral("logging/maxFileSizeMb"), 10));
        QVERIFY(!SettingsSchema::isValidValue(QStringLiteral("logging/maxFileSizeMb"),
                                              QStringLiteral("10")));
        QVERIFY(!SettingsSchema::isValidValue(QStringLiteral("logging/maxFileSizeMb"), 10.5));
        QVERIFY(!SettingsSchema::isValidValue(QStringLiteral("logging/maxFileSizeMb"), 0));
        QVERIFY(SettingsSchema::isValidValue(QStringLiteral("prefs/v1/ui.locale"), QString()));
        QVERIFY(!SettingsSchema::isValidValue(QStringLiteral("prefs/v1/ui.locale"),
                                              QStringLiteral("fr")));
        QString validationError;
        QVERIFY(!SettingsRepository::writeValue(QStringLiteral("logging/maxFileSizeMb"),
                                                 QStringLiteral("10"), &validationError));
        QVERIFY(!validationError.isEmpty());
        // 合法 provider 键与后端注册表同源（单一事实源，不再两处硬编码）。
        QCOMPARE(SettingsSchema::options(QStringLiteral("webSearch/provider")),
                 WebSearchProviderRegistry::keys());
        QCOMPARE(SettingsSchema::options(QStringLiteral("webSearch/provider")),
                 QStringList({QStringLiteral("duckduckgo"), QStringLiteral("zhipu"),
                              QStringLiteral("deepseek")}));
        // 默认值也来自注册表：移除 Tavily 后首装应当是免密钥后端。
        QCOMPARE(SettingsSchema::defaultValue(QStringLiteral("webSearch/provider")).toString(),
                 WebSearchProviderRegistry::defaultProviderKey());
        QCOMPARE(WebSearchProviderRegistry::defaultProviderKey(),
                 QStringLiteral("duckduckgo"));
    }

    void modelSelectionAndSignals() {
        SettingsGuard guard;
        {
            QSettings s = NexusAI::TestSupport::testSettings();
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
            QSettings s = NexusAI::TestSupport::testSettings();
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
            QSettings s = NexusAI::TestSupport::testSettings();
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
            QSettings s = NexusAI::TestSupport::testSettings();
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
            service.setLogLevel(QStringLiteral("not-a-level"));
            QCOMPARE(service.logLevel(), QStringLiteral("debug"));
        }
        {
            SettingsService service(nullptr);
            QCOMPARE(service.logLevel(), QStringLiteral("debug"));
        }
        Logger::setMinLevel(QtInfoMsg);   // restore suite default
    }

    void logRotationSettingsPersistAndClamp() {
        SettingsGuard guard;
        SettingsService service(nullptr);
        QCOMPARE(service.logMaxFileSizeMb(), 10);
        QCOMPARE(service.logRetentionFiles(), 14);

        service.setLogMaxFileSizeMb(0);
        QCOMPARE(service.logMaxFileSizeMb(), 1);
        service.setLogMaxFileSizeMb(2048);
        QCOMPARE(service.logMaxFileSizeMb(), 1024);
        service.setLogRetentionFiles(-2);
        QCOMPARE(service.logRetentionFiles(), 1);
        service.setLogRetentionFiles(1000);
        QCOMPARE(service.logRetentionFiles(), 100);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        service.setLogDirectory(dir.path());
        QCOMPARE(service.logDirectory(), QDir::cleanPath(dir.path()));
        QCOMPARE(NexusAI::TestSupport::testSettings().value("logging/directory").toString(),
                 QDir::cleanPath(dir.path()));
    }

    // Batch 22 (F8): user-editable system prompt; empty (after trim) clears
    // the override instead of being ignored.
    void systemPromptOverridePersistence() {
        SettingsGuard guard;
        {
            QSettings s = NexusAI::TestSupport::testSettings();
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
            QSettings s = NexusAI::TestSupport::testSettings();
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

    // 回归（本次改动引入的缺陷）：密钥输入框的可见性绑定必须随 provider
    // 切换即时重算。
    //
    // 根因：把「是否需要密钥」写成 Q_INVOKABLE 后，QML 侧
    // `visible: Settings.webSearchProviderNeedsApiKey()` 是一次函数调用，
    // 结果不依赖任何 notify 信号；切换 provider 只发 webSearchProviderChanged，
    // 该绑定不会重算，输入框要等重新打开设置页（组件重建）才出现。
    // 修法是改成带 NOTIFY webSearchProviderChanged 的 Q_PROPERTY。
    void webSearchKeyFieldVisibilityTracksProviderSwitch() {
        SettingsGuard guard;
        {
            // 三把键全清：只清 provider 会让测试读到本机真实已配置的 key
            // （"切过去应该显示未配置"的断言就会依赖开发机的注册表状态）。
            QSettings s = NexusAI::TestSupport::testSettings();
            s.remove("webSearch/provider");
            s.remove("webSearch/zhipuApiKey");
            s.remove("webSearch/deepseekApiKey");
        }
        SettingsService svc(nullptr);
        QQmlEngine engine;

        // 与 ToolsPage 同形的绑定：属性读取，不是函数调用。
        static const char* kQml = R"QML(
import QtQuick
Item {
    property bool keyFieldVisible: settings.webSearchProviderNeedsApiKey
    property bool keyConfigured: settings.webSearchApiKeySet
}
)QML";

        // QML 通过 context property 拿到同一个实例（生产里是 QML_SINGLETON）。
        // 必须在创建组件之前设置，否则绑定求值时会解析不到。
        engine.rootContext()->setContextProperty(QStringLiteral("settings"), &svc);
        QQmlComponent component(&engine);
        component.setData(QByteArray(kQml), QUrl());
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> item2(component.create());
        QVERIFY2(item2 != nullptr, qPrintable(component.errorString()));

        QCOMPARE(svc.webSearchProvider(), QStringLiteral("duckduckgo"));
        // 免密钥后端：输入框隐藏。
        QCOMPARE(item2->property("keyFieldVisible").toBool(), false);

        // 切到需要密钥的后端：绑定必须立刻翻成可见，无需重建组件。
        svc.setWebSearchProvider(QStringLiteral("zhipu"));
        QCOMPARE(item2->property("keyFieldVisible").toBool(), true);
        QCOMPARE(item2->property("keyConfigured").toBool(), false);

        // 配好密钥后「已配置」也跟着走。
        svc.setWebSearchApiKey(QStringLiteral("zhipu-key"));
        QCOMPARE(item2->property("keyConfigured").toBool(), true);

        // 切到免密钥后端：输入框立即隐藏；再切回，已配置的状态仍在。
        svc.setWebSearchProvider(QStringLiteral("duckduckgo"));
        QCOMPARE(item2->property("keyFieldVisible").toBool(), false);
        svc.setWebSearchProvider(QStringLiteral("zhipu"));
        QCOMPARE(item2->property("keyFieldVisible").toBool(), true);
        QCOMPARE(item2->property("keyConfigured").toBool(), true);

        // 元对象层面锁定修法：必须是属性且带正确的 NOTIFY，否则上面的绑定
        // 会退回「只求值一次」的行为。
        const QMetaObject* mo = svc.metaObject();
        const int idx = mo->indexOfProperty("webSearchProviderNeedsApiKey");
        QVERIFY2(idx >= 0, "webSearchProviderNeedsApiKey must be a Q_PROPERTY");
        const QMetaProperty prop = mo->property(idx);
        QVERIFY(prop.hasNotifySignal());
        QCOMPARE(prop.notifySignal().name(), QByteArray("webSearchProviderChanged"));
    }

    // 搜索连通性测试的「前置校验」路径：未配置就得当场说清楚，不能发起
    // 一次注定失败的请求再把原始错误丢给用户。
    //
    // 真实检索路径不在这里测（会打真网络、结果依赖外部服务可用性），与
    // test_web_search 只测 build/parse 纯函数的既有口径一致；网络层由
    // 各后端的 errorReason 契约单独覆盖。
    void webSearchTestGuardsBeforeNetwork() {
        SettingsGuard guard;
        {
            QSettings s = NexusAI::TestSupport::testSettings();
            s.remove("webSearch/provider");
            s.remove("webSearch/zhipuApiKey");
        }
        SettingsService svc(nullptr);

        // 需要密钥的后端 + 没填 key：立即失败，文案点名是哪家。
        svc.setWebSearchProvider(QStringLiteral("zhipu"));
        QCOMPARE(svc.webSearchProviderLabel(QStringLiteral("zhipu")),
                 QStringLiteral("智谱 Web Search"));
        QSignalSpy spy(&svc, &SettingsService::webSearchTestChanged);
        svc.testWebSearch();
        QCOMPARE(spy.count(), 1);           // 同步完成，不发「正在测试」中间态
        QVERIFY(!svc.webSearchTesting());
        QVERIFY(!svc.webSearchTestOk());
        QVERIFY2(svc.webSearchTestResult().contains(QStringLiteral("智谱 Web Search")),
                 qPrintable(svc.webSearchTestResult()));
        QVERIFY(svc.webSearchTestResult().contains(QStringLiteral("API Key")));

        // 未知 provider：同样给出可读原因而不是静默失败。
        // （setWebSearchProvider 会拒绝未知键，这里直接验证 label 兜底行为。）
        QCOMPARE(svc.webSearchProviderLabel(QStringLiteral("bogus")),
                 QStringLiteral("bogus"));

        // 成功/失败结论必须能被 QML 观测到：属性存在且带通知信号。
        const QMetaObject* mo = svc.metaObject();
        for (const char* name : {"webSearchTesting", "webSearchTestOk",
                                 "webSearchTestResult"}) {
            const int idx = mo->indexOfProperty(name);
            QVERIFY2(idx >= 0, name);
            const QMetaProperty prop = mo->property(idx);
            QVERIFY2(prop.hasNotifySignal(), name);
            QCOMPARE(prop.notifySignal().name(), QByteArray("webSearchTestChanged"));
        }
    }

    // 连通性测试的完整异步状态机：进行中 → 结论。用替身后端把网络层摘掉，
    // 才能断言「先发正在测试、再发结论」的时序与文案（真实路径会打真网络，
    // 结果取决于外部服务，不适合做回归断言）。
    void webSearchTestRunsAsyncAndReportsOutcome() {
        SettingsGuard guard;
        {
            QSettings s = NexusAI::TestSupport::testSettings();
            s.remove("webSearch/provider");
            s.remove("webSearch/zhipuApiKey");
        }
        SettingsService svc(nullptr);
        svc.setWebSearchProvider(QStringLiteral("zhipu"));
        svc.setWebSearchApiKey(QStringLiteral("fake-key"));

        // 成功：后端返回 2 条结果、无错误原因。
        class TwoResultBackend : public WebSearchBackend {
        public:
            QString providerKey() const override { return QStringLiteral("zhipu"); }
            QList<WebSearchResult> search(const QString&, int, const QString&, int,
                                          QString* errorReason,
                                          const std::atomic<bool>*) const override {
                if (errorReason) *errorReason = QString();
                QList<WebSearchResult> out;
                for (int i = 0; i < 2; ++i) {
                    WebSearchResult r;
                    r.title = QStringLiteral("r%1").arg(i);
                    r.url = QStringLiteral("https://e.org/%1").arg(i);
                    out.append(r);
                }
                return out;
            }
        };
        svc.setWebSearchTestBackendForTesting(std::make_unique<TwoResultBackend>());
        QSignalSpy spy(&svc, &SettingsService::webSearchTestChanged);
        svc.testWebSearch();
        // 同步先进入"进行中"，异步回结论。
        QVERIFY(svc.webSearchTesting());
        QVERIFY(svc.webSearchTestResult().contains(QStringLiteral("正在测试")));
        QVERIFY(spy.wait(5000));
        QVERIFY(!svc.webSearchTesting());
        QVERIFY(svc.webSearchTestOk());
        QVERIFY2(svc.webSearchTestResult().contains(QStringLiteral("2")),
                 qPrintable(svc.webSearchTestResult()));
        QVERIFY(svc.webSearchTestResult().contains(QStringLiteral("智谱 Web Search")));

        // 失败：后端把原因写进 errorReason。
        class FailingBackend : public WebSearchBackend {
        public:
            QString providerKey() const override { return QStringLiteral("zhipu"); }
            QList<WebSearchResult> search(const QString&, int, const QString&, int,
                                          QString* errorReason,
                                          const std::atomic<bool>*) const override {
                if (errorReason) *errorReason = QStringLiteral("API Key 无效");
                return {};
            }
        };
        svc.setWebSearchTestBackendForTesting(std::make_unique<FailingBackend>());
        QSignalSpy failSpy(&svc, &SettingsService::webSearchTestChanged);
        svc.testWebSearch();
        QVERIFY(failSpy.wait(5000));
        QVERIFY(!svc.webSearchTesting());
        QVERIFY(!svc.webSearchTestOk());
        // 失败文案要点名是哪家 + 原始原因。
        QVERIFY2(svc.webSearchTestResult().contains(QStringLiteral("智谱 Web Search")),
                 qPrintable(svc.webSearchTestResult()));
        QVERIFY2(svc.webSearchTestResult().contains(QStringLiteral("API Key 无效")),
                 qPrintable(svc.webSearchTestResult()));
    }

    // 免密钥后端（duckduckgo）也能测：没有密钥可验，但同样是一次真实检索。
    void webSearchTestAllowsKeylessProvider() {
        SettingsGuard guard;
        {
            QSettings s = NexusAI::TestSupport::testSettings();
            s.remove("webSearch/provider");
        }
        SettingsService svc(nullptr);
        QCOMPARE(svc.webSearchProvider(), QStringLiteral("duckduckgo"));
        QVERIFY(!svc.webSearchProviderNeedsApiKey());

        class OneResultBackend : public WebSearchBackend {
        public:
            QString providerKey() const override { return QStringLiteral("duckduckgo"); }
            bool requiresApiKey() const override { return false; }
            QList<WebSearchResult> search(const QString&, int, const QString&, int,
                                          QString* errorReason,
                                          const std::atomic<bool>*) const override {
                if (errorReason) *errorReason = QString();
                WebSearchResult r;
                r.title = QStringLiteral("t");
                r.url = QStringLiteral("https://e.org");
                return {r};
            }
        };
        svc.setWebSearchTestBackendForTesting(std::make_unique<OneResultBackend>());
        QSignalSpy spy(&svc, &SettingsService::webSearchTestChanged);
        svc.testWebSearch();
        QVERIFY(spy.wait(5000));
        QVERIFY(svc.webSearchTestOk());
        QVERIFY(!svc.webSearchTestResult().contains(QStringLiteral("API Key")));
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
            QSettings s = NexusAI::TestSupport::testSettings();
            QVERIFY(s.value("tools/disabled").toStringList().contains("read_file"));
        }
        reg.setToolEnabled("read_file", true);
        {
            QSettings s = NexusAI::TestSupport::testSettings();
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
            QSettings s = NexusAI::TestSupport::testSettings();
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
            QSettings s = NexusAI::TestSupport::testSettings();
            QCOMPARE(s.value("prefs/v1/ui.themeMode").toString(), QStringLiteral("light"));
        }
    }

    void themeModeLegacyDarkKeyCompat() {
        SettingsGuard guard;
        {
            QSettings s = NexusAI::TestSupport::testSettings();
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
        svc.setWebSearchProvider(QStringLiteral("zhipu"));
        QCOMPARE(svc.webSearchProvider(), QStringLiteral("zhipu"));
        QSignalSpy spy(&svc, &SettingsService::webSearchProviderChanged);
        svc.setWebSearchProvider(QStringLiteral("google"));
        QCOMPARE(spy.count(), 0);
        QCOMPARE(svc.webSearchProvider(), QStringLiteral("zhipu"));
    }

    // 任务 #20（Mj1）：embedding provider 的 L4 守卫——合法键持久化并
    // 发射信号；未知键拒绝（值不变、无信号、落盘不动），对齐
    // webSearchProviderRejectsUnknownKey 的先例写法。
    void embeddingProviderRejectsUnknownKey() {
        SettingsGuard guard;
        {
            QSettings s = NexusAI::TestSupport::testSettings();
            s.remove("embedding/provider");   // 构造后回落 schema 默认 "ollama"
        }
        SettingsService svc(nullptr);
        QCOMPARE(svc.embeddingProvider(), QStringLiteral("ollama"));
        // 合法键：值生效、信号发射、落盘同步。
        QSignalSpy okSpy(&svc, &SettingsService::embeddingProviderChanged);
        svc.setEmbeddingProvider(QStringLiteral("openai"));
        QCOMPARE(okSpy.count(), 1);
        QCOMPARE(svc.embeddingProvider(), QStringLiteral("openai"));
        {
            QSettings s = NexusAI::TestSupport::testSettings();
            QCOMPARE(s.value("embedding/provider").toString(),
                     QStringLiteral("openai"));
        }
        // 未知键：拒绝——值保持不变、无信号。
        QSignalSpy rejectSpy(&svc, &SettingsService::embeddingProviderChanged);
        svc.setEmbeddingProvider(QStringLiteral("bogus"));
        QCOMPARE(rejectSpy.count(), 0);
        QCOMPARE(svc.embeddingProvider(), QStringLiteral("openai"));
        {
            QSettings s = NexusAI::TestSupport::testSettings();
            QCOMPARE(s.value("embedding/provider").toString(),
                     QStringLiteral("openai"));
        }
    }

    // 任务 #20（Mj1）：embedding API key 的 DPAPI 往返与空串清除；顺带
    // 断言真实值出口（embeddingApiKeyValue，修 B1）与掩码 getter（恒空）
    // 的区分，以及 webSearchApiKeyValue（修 Mj2）的同构往返——此前两者
    // 均零测试覆盖。
    void embeddingApiKeyRoundTripAndClear() {
        SettingsGuard guard;
        {
            QSettings s = NexusAI::TestSupport::testSettings();
            s.remove("embedding/apiKey");
            s.remove("webSearch/zhipuApiKey");
        }
        {
            // 写入：掩码 getter 恒空（QML 掩码语义），真实值出口返回
            // 内存值；落盘必须是 DPAPI 密文（绝不等于明文）。
            SettingsService svc(nullptr);
            svc.setEmbeddingApiKey(QStringLiteral("k1"));
            QCOMPARE(svc.embeddingApiKey(), QString());
            QVERIFY(svc.embeddingApiKeySet());
            QCOMPARE(svc.embeddingApiKeyValue(), QStringLiteral("k1"));
            // 搜索密钥分桶存储：先切到需要密钥的后端再写入。
            svc.setWebSearchProvider(QStringLiteral("zhipu"));
            svc.setWebSearchApiKey(QStringLiteral("k1"));
            QCOMPARE(svc.webSearchApiKey(), QString());
            QVERIFY(svc.webSearchApiKeySet());
            QCOMPARE(svc.webSearchApiKeyValue(), QStringLiteral("k1"));
#ifdef Q_OS_WIN
            QSettings s = NexusAI::TestSupport::testSettings();
            const QString stored = s.value("embedding/apiKey").toString();
            QVERIFY(stored.startsWith(QStringLiteral("dpapi:v1:")));
            QVERIFY(stored != QStringLiteral("k1"));
            // 搜索密钥同样必须是密文。
            const QString searchStored = s.value("webSearch/zhipuApiKey").toString();
            QVERIFY(searchStored.startsWith(QStringLiteral("dpapi:v1:")));
            QVERIFY(searchStored != QStringLiteral("k1"));
#endif
        }
        // 新实例 load：DPAPI 解密回填，真实值出口取回明文（往返成立）。
        {
            SettingsService svc2(nullptr);
            QCOMPARE(svc2.embeddingApiKeyValue(), QStringLiteral("k1"));
            QCOMPARE(svc2.webSearchProvider(), QStringLiteral("zhipu"));
            QCOMPARE(svc2.webSearchApiKeyValue(), QStringLiteral("k1"));
        }
        // 空串清除：apiKeySet 归零、落盘为空；新实例确认读回为空。
        {
            SettingsService svc3(nullptr);
            QVERIFY(svc3.embeddingApiKeySet());
            svc3.setEmbeddingApiKey(QString());
            QVERIFY(!svc3.embeddingApiKeySet());
            QCOMPARE(svc3.embeddingApiKeyValue(), QString());
            {
                QSettings s = NexusAI::TestSupport::testSettings();
                QVERIFY(s.value("embedding/apiKey").toString().isEmpty());
            }
            SettingsService svc4(nullptr);
            QVERIFY(!svc4.embeddingApiKeySet());
            QCOMPARE(svc4.embeddingApiKeyValue(), QString());
        }
    }

    // 密钥按 provider 分桶：切换 provider 不会串用另一家的凭据（否则会把
    // A 平台的密钥发给 B 平台——401 之外还是一次跨厂商凭据泄露）。
    void webSearchApiKeysAreBucketedPerProvider() {
        SettingsGuard guard;
        {
            QSettings s = NexusAI::TestSupport::testSettings();
            s.remove("webSearch/zhipuApiKey");
            s.remove("webSearch/deepseekApiKey");
            s.remove("webSearch/provider");
        }
        SettingsService svc(nullptr);
        QCOMPARE(svc.webSearchProvider(), QStringLiteral("duckduckgo"));
        // 免密钥后端：没有密钥桶，setWebSearchApiKey 是 no-op（不写脏数据）。
        QVERIFY(!svc.webSearchProviderNeedsApiKey());
        QVERIFY(!svc.webSearchApiKeySet());
        svc.setWebSearchApiKey(QStringLiteral("should-be-ignored"));
        QVERIFY(!svc.webSearchApiKeySet());
        {
            QSettings s = NexusAI::TestSupport::testSettings();
            QVERIFY(!s.contains("webSearch/zhipuApiKey"));
            QVERIFY(!s.contains("webSearch/deepseekApiKey"));
        }

        // 智谱写一把，DeepSeek 写另一把，两者互不可见。
        QSignalSpy keySpy(&svc, &SettingsService::webSearchApiKeyChanged);
        svc.setWebSearchProvider(QStringLiteral("zhipu"));
        QVERIFY(svc.webSearchProviderNeedsApiKey());
        // 切换 provider 必发一次 apiKeyChanged：QML 的「已配置/未配置」
        // 占位符判据随之变化，不通知就会继续显示上一家的状态。
        QVERIFY(keySpy.count() >= 1);
        svc.setWebSearchApiKey(QStringLiteral("zhipu-key"));
        QCOMPARE(svc.webSearchApiKeyValue(), QStringLiteral("zhipu-key"));

        svc.setWebSearchProvider(QStringLiteral("deepseek"));
        // 切过去时 DeepSeek 还没配 key：不能读到智谱那把。
        QVERIFY(!svc.webSearchApiKeySet());
        QCOMPARE(svc.webSearchApiKeyValue(), QString());
        svc.setWebSearchApiKey(QStringLiteral("deepseek-key"));
        QCOMPARE(svc.webSearchApiKeyValue(), QStringLiteral("deepseek-key"));

        // 切回智谱：原样取回智谱那把。
        svc.setWebSearchProvider(QStringLiteral("zhipu"));
        QCOMPARE(svc.webSearchApiKeyValue(), QStringLiteral("zhipu-key"));
        QVERIFY(svc.webSearchApiKeySet());

        // 两把 key 各自落在自己的设置键上，重启后仍分桶。
        {
            QSettings s = NexusAI::TestSupport::testSettings();
            QVERIFY(!s.value("webSearch/zhipuApiKey").toString().isEmpty());
            QVERIFY(!s.value("webSearch/deepseekApiKey").toString().isEmpty());
            QVERIFY(s.value("webSearch/zhipuApiKey").toString()
                    != s.value("webSearch/deepseekApiKey").toString());
        }
        SettingsService svc2(nullptr);
        QCOMPARE(svc2.webSearchApiKeyValue(), QStringLiteral("zhipu-key"));
        svc2.setWebSearchProvider(QStringLiteral("deepseek"));
        QCOMPARE(svc2.webSearchApiKeyValue(), QStringLiteral("deepseek-key"));

        // 清除只影响当前 provider。
        svc2.setWebSearchApiKey(QString());
        QVERIFY(!svc2.webSearchApiKeySet());
        svc2.setWebSearchProvider(QStringLiteral("zhipu"));
        QCOMPARE(svc2.webSearchApiKeyValue(), QStringLiteral("zhipu-key"));
    }

    // 未知 provider 键被拒绝（L4），且不再出现已被移除的后端。
    void webSearchProviderRejectsRemovedBackends() {
        SettingsGuard guard;
        SettingsService svc(nullptr);
        const QString before = svc.webSearchProvider();
        QSignalSpy spy(&svc, &SettingsService::webSearchProviderChanged);
        for (const QString& removed : {QStringLiteral("tavily"),
                                       QStringLiteral("searxng")}) {
            svc.setWebSearchProvider(removed);
            QCOMPARE(svc.webSearchProvider(), before);
        }
        QCOMPARE(spy.count(), 0);
        // UI 下拉的合法值里也不该再有它们。
        QVERIFY(!svc.webSearchProviders().contains(QStringLiteral("tavily")));
        QVERIFY(!svc.webSearchProviders().contains(QStringLiteral("searxng")));
    }

    // H1: proxy mode/host/port persist; credentials are encrypted as one
    // blob (never plain text), the password getter is masked, and a new
    // instance decrypts the round-trip.
    void proxySettingsPersistAndEncryptAuth() {
        SettingsGuard guard;
        {
            QSettings s = NexusAI::TestSupport::testSettings();
            s.remove("network/proxyMode");
            s.remove("network/proxyHost");
            s.remove("network/proxyPort");
            s.remove("network/proxyAuth");
        }
        {
            SettingsService svc(nullptr);
            // Defaults preserve the pre-H1 behavior: follow the system proxy.
            QCOMPARE(svc.proxyMode(), QStringLiteral("system"));
            QCOMPARE(svc.proxyPort(), 8080);
            QVERIFY(!svc.proxyPasswordSet());

            svc.setProxyMode(QStringLiteral("manual"));
            svc.setProxyHost(QStringLiteral("http://proxy.example/"));
            svc.setProxyPort(3128);
            svc.setProxyUsername(QStringLiteral("alice"));
            svc.setProxyPassword(QStringLiteral("p@ss word"));

            QCOMPARE(svc.proxyHost(), QStringLiteral("proxy.example"));
            QCOMPARE(svc.proxyPort(), 3128);
            QCOMPARE(svc.proxyUsername(), QStringLiteral("alice"));
            QCOMPARE(svc.proxyPassword(), QString());   // masked getter
            QVERIFY(svc.proxyPasswordSet());

            const NetworkProxyConfig config = svc.proxyConfig();
            QCOMPARE(static_cast<int>(config.mode),
                     static_cast<int>(NetworkProxyConfig::Mode::Manual));
            QCOMPARE(config.host, QStringLiteral("proxy.example"));
            QCOMPARE(config.port, 3128);
            QCOMPARE(config.username, QStringLiteral("alice"));
            QCOMPARE(config.password, QStringLiteral("p@ss word"));

#ifdef Q_OS_WIN
            const QString stored = NexusAI::TestSupport::testSettings()
                .value("network/proxyAuth").toString();
            QVERIFY(stored.startsWith(QStringLiteral("dpapi:v1:")));
            QVERIFY(!stored.contains(QStringLiteral("alice")));
            QVERIFY(!stored.contains(QStringLiteral("p@ss")));
#endif

            // Unknown modes are ignored; the port is clamped by the schema.
            svc.setProxyMode(QStringLiteral("bogus"));
            QCOMPARE(svc.proxyMode(), QStringLiteral("manual"));
            svc.setProxyPort(70000);
            QCOMPARE(svc.proxyPort(), 65535);
            svc.setProxyPort(3128);
        }
        {
            SettingsService svc(nullptr);
            QCOMPARE(svc.proxyMode(), QStringLiteral("manual"));
            QCOMPARE(svc.proxyHost(), QStringLiteral("proxy.example"));
            QCOMPARE(svc.proxyPort(), 3128);
            QCOMPARE(svc.proxyUsername(), QStringLiteral("alice"));
            QCOMPARE(svc.proxyPasswordValue(), QStringLiteral("p@ss word"));

            svc.setProxyPassword(QString());
            svc.setProxyUsername(QString());
            QVERIFY(!svc.proxyPasswordSet());
            QCOMPARE(svc.proxyUsername(), QString());
        }
        {
            // Clearing both credentials persists an empty blob.
            SettingsService svc(nullptr);
            QVERIFY(!svc.proxyPasswordSet());
            QCOMPARE(svc.proxyUsername(), QString());
            QCOMPARE(NexusAI::TestSupport::testSettings()
                         .value("network/proxyAuth").toString(), QString());
        }
    }

    // 任务 #20（Mj1）：localOnlyMode 拦截决策（批次三设计决策 2）的纯
    // 函数四组合——main.cpp applyEmbeddingConfig 的后端切换与此同源。
    void embeddingProviderLocalOnlyDecision() {
        // 纯本地模式开启：外部请求被拦截，回退本地 Ollama。
        QCOMPARE(SettingsService::embeddingUsesExternalProvider(
                     QStringLiteral("openai"), true), false);
        // 关闭：外部 provider 生效。
        QCOMPARE(SettingsService::embeddingUsesExternalProvider(
                     QStringLiteral("openai"), false), true);
        // 本地 provider 与纯本地模式无关，恒走本地。
        QCOMPARE(SettingsService::embeddingUsesExternalProvider(
                     QStringLiteral("ollama"), true), false);
        QCOMPARE(SettingsService::embeddingUsesExternalProvider(
                     QStringLiteral("ollama"), false), false);
    }

    // L2: SettingsSchema key registry is populated and contains known keys.
    void schemaHasKeyRegistry() {
        const auto& keys = SettingsSchema::registeredKeys();
        QVERIFY(!keys.isEmpty());
        bool foundProvider = false;
        // 密钥按 provider 分桶：每一把都要在账本里且标记 encrypted
        //（设置导出必须永远带不走凭据）。
        bool foundZhipuApiKey = false;
        bool foundDeepSeekApiKey = false;
        // docs/20 工作流 B（桌面集成）：新偏好键必须登记进账本。
        bool foundCloseToTray = false;
        bool foundAutoStart = false;
        bool foundNoticeEnabled = false;
        // H1: proxy keys must be registered, and the credential blob must be
        // marked encrypted so settings export can never carry it.
        bool foundProxyMode = false;
        bool foundProxyAuth = false;
        for (const auto& k : keys) {
            if (k.key == QStringLiteral("webSearch/provider")) {
                foundProvider = true;
                // provider 的合法值必须来自注册表，不是另一份手写清单。
                QCOMPARE(k.constraint.options, WebSearchProviderRegistry::keys());
            }
            if (k.key == QStringLiteral("webSearch/zhipuApiKey")) {
                foundZhipuApiKey = true;
                QVERIFY(k.encrypted);
            }
            if (k.key == QStringLiteral("webSearch/deepseekApiKey")) {
                foundDeepSeekApiKey = true;
                QVERIFY(k.encrypted);
            }
            if (k.key == QStringLiteral("network/proxyMode")) {
                foundProxyMode = true;
                QVERIFY(k.constraint.options.contains(QStringLiteral("system")));
                QVERIFY(k.constraint.options.contains(QStringLiteral("manual")));
                QVERIFY(k.constraint.options.contains(QStringLiteral("off")));
            }
            if (k.key == QStringLiteral("network/proxyAuth")) {
                foundProxyAuth = true;
                QVERIFY(k.encrypted);
            }
            if (k.key == QStringLiteral("prefs/v1/window.closeToTray"))
                foundCloseToTray = true;
            if (k.key == QStringLiteral("prefs/v1/general.autoStart"))
                foundAutoStart = true;
            if (k.key == QStringLiteral("prefs/v1/notice.enabled")) {
                foundNoticeEnabled = true;
                // 默认开：通知是既有行为，升级用户不该被静默关掉。
                QCOMPARE(k.defaultValue.toBool(), true);
            }
        }
        QVERIFY(foundProvider);
        // 键登记纪律（docs/17 B3）：漏登记会让"恢复默认"/账本完整性静默失效。
        QVERIFY2(foundZhipuApiKey, "webSearch/zhipuApiKey missing from schema");
        QVERIFY2(foundDeepSeekApiKey, "webSearch/deepseekApiKey missing from schema");
        QVERIFY2(foundProxyMode, "network/proxyMode missing from schema");
        QVERIFY2(foundProxyAuth, "network/proxyAuth missing from schema");
        QVERIFY2(foundCloseToTray, "prefs/v1/window.closeToTray missing from schema");
        QVERIFY2(foundAutoStart, "prefs/v1/general.autoStart missing from schema");
        QVERIFY2(foundNoticeEnabled, "prefs/v1/notice.enabled missing from schema");
    }

    // docs/20 §4.3（D8）：开机自启。注入假 backend，在不碰真实 HKCU Run 键
    // 的前提下校验：开关状态翻转 + 未成功写入时不发信号（UI 不误报）。
    void autoStartTogglesViaInjectedBackend() {
        SettingsGuard guard;
        SettingsService svc(nullptr);

        struct FakeBackend final : public SettingsService::AutoStartBackend {
            bool enabled{false};
            bool fail{false};
            QString lastPath;
            bool isEnabled() const override { return enabled; }
            bool setEnabled(bool on, const QString& exePath) override {
                if (fail) return false;
                enabled = on;
                lastPath = exePath;
                return true;
            }
        };
        FakeBackend backend;
        svc.setAutoStartBackend(&backend);

        QCOMPARE(svc.autoStart(), false);
        QSignalSpy spy(&svc, &SettingsService::autoStartChanged);
        QSignalSpy persistenceErrorSpy(&svc, &SettingsService::persistenceError);

        svc.setAutoStart(true);
        QCOMPARE(svc.autoStart(), true);
        QCOMPARE(spy.count(), 1);
        // 值必须是 exe 全路径（Run 键需要完整路径才能开机拉起）。
        QCOMPARE(backend.lastPath, QCoreApplication::applicationFilePath());

        // 幂等：同值再设不发信号（与其它 setter 的 no-op 语义一致）。
        spy.clear();
        svc.setAutoStart(true);
        QCOMPARE(spy.count(), 0);

        svc.setAutoStart(false);
        QCOMPARE(svc.autoStart(), false);
        QCOMPARE(spy.count(), 1);

        // 写入失败：状态不变、不发信号（UI 的开关保持原样，用户能看出没成）。
        backend.fail = true;
        spy.clear();
        svc.setAutoStart(true);
        QCOMPARE(svc.autoStart(), false);
        QCOMPARE(spy.count(), 0);
        QCOMPARE(persistenceErrorSpy.count(), 1);
        backend.fail = false;
        svc.setAutoStartBackend(nullptr);   // 还原默认 backend
    }

    void settingsRepositoryUsesSharedApplicationStore() {
        const QStringList keys = {
            QStringLiteral("test/settingsRepository/first"),
            QStringLiteral("test/settingsRepository/second")};
        struct KeyGuard {
            QStringList keys;
            QHash<QString, QVariant> values;
            QSet<QString> existing;
            explicit KeyGuard(const QStringList& guardedKeys) : keys(guardedKeys) {
                for (const QString& key : keys) {
                    if (SettingsRepository::contains(key)) {
                        existing.insert(key);
                        values.insert(key, SettingsRepository::value(key));
                    }
                }
            }
            ~KeyGuard() {
                for (const QString& key : keys) {
                    if (existing.contains(key))
                        SettingsRepository::writeValue(key, values.value(key));
                    else
                        SettingsRepository::removeBatch({key});
                }
            }
        } guard(keys);

        QVERIFY(SettingsRepository::writeBatch({
            {keys.at(0), QStringLiteral("one")}, {keys.at(1), 2}}));
        QCOMPARE(SettingsRepository::value(keys.at(0)).toString(), QStringLiteral("one"));
        QCOMPARE(SettingsRepository::value(keys.at(1)).toInt(), 2);
        QVariantMap snapshot;
        QStringList presentKeys;
        QString error;
        QVERIFY2(SettingsRepository::readBatch(keys, &snapshot, &presentKeys, &error),
                 qPrintable(error));
        QCOMPARE(snapshot.value(keys.at(0)).toString(), QStringLiteral("one"));
        QCOMPARE(presentKeys.size(), 2);
        QVERIFY(SettingsRepository::removeBatch(keys));
        QVERIFY(!SettingsRepository::contains(keys.at(0)));
        QVERIFY(!SettingsRepository::contains(keys.at(1)));
    }

    // PreferenceStore 写入及批量重置：同时锁定 bool 类型回读和测试数据隔离。
    void closeToTrayPreferenceRoundTrips() {
        struct KeyGuard {
            QSettings settings{"NexusAI", "NexusAI"};
            QStringList keys;
            QHash<QString, QVariant> values;
            QSet<QString> existing;

            explicit KeyGuard(const QStringList& guardedKeys) : keys(guardedKeys) {
                for (const QString& key : keys) {
                    if (settings.contains(key)) {
                        existing.insert(key);
                        values.insert(key, settings.value(key));
                    }
                }
            }
            ~KeyGuard() {
                for (const QString& key : keys) {
                    if (existing.contains(key)) settings.setValue(key, values.value(key));
                    else settings.remove(key);
                }
                settings.sync();
            }
        } guard({QStringLiteral("prefs/v1/window.closeToTray"),
                 QStringLiteral("prefs/v1/notice.enabled")});

        const QStringList keys = {
            QStringLiteral("prefs/v1/window.closeToTray"),
            QStringLiteral("prefs/v1/notice.enabled")};
        for (const QString& key : keys) guard.settings.remove(key);
        guard.settings.sync();


        PreferenceStore store(nullptr);
        QSignalSpy savedSpy(&store, &PreferenceStore::saved);
        QCOMPARE(store.load("window.closeToTray"), QVariant(false));
        QVERIFY(!store.hasStoredValue("window.closeToTray"));
        QVERIFY(!store.saveBatch({{QStringLiteral("window.closeToTray"), true},
                                 {QStringLiteral("not.registered"), true}}));
        QCOMPARE(savedSpy.count(), 0);
        QCOMPARE(store.load("window.closeToTray"), QVariant(false));
        QVERIFY(store.saveBatch({{QStringLiteral("window.closeToTray"), true},
                                 {QStringLiteral("notice.enabled"), false}}));
        QCOMPARE(savedSpy.count(), 1);
        const QStringList savedKeys = savedSpy.takeFirst().at(0).toStringList();
        QCOMPARE(savedKeys.size(), 2);
        QVERIFY(savedKeys.contains("window.closeToTray"));
        QVERIFY(savedKeys.contains("notice.enabled"));
        QCOMPARE(store.load("window.closeToTray"), QVariant(true));
        QCOMPARE(store.load("notice.enabled"), QVariant(false));
        QVERIFY(store.hasStoredValue("window.closeToTray"));

        QVERIFY(store.resetBatch({QStringLiteral("window.closeToTray"),
                                  QStringLiteral("notice.enabled")}));
        // 复位有独立反馈，不能被通用的"已保存"覆盖。
        QCOMPARE(savedSpy.count(), 0);
        QCOMPARE(store.load("window.closeToTray"), QVariant(false));
        QCOMPARE(store.load("notice.enabled"), QVariant(true));
        QVERIFY(!store.hasStoredValue("window.closeToTray"));
    }

    // v2 → v3/v4：contextTokens 旧默认值（8192、以及短暂存在过的 300000）归一
    // 为当前的保守默认 128000，而用户的自定义值必须原样保留——通用页输入框已
    // 移除，不能把用户的主动设置抹掉。
    void schemaV3UpgradesContextTokenDefault() {
        auto clean = [] {
            QSettings s = NexusAI::TestSupport::testSettings();
            s.remove("contextTokens");
            s.sync();
        };
        clean();

        // 存量值 = 旧默认 8192 → 归一。
        {
            QSettings s = NexusAI::TestSupport::testSettings();
            s.setValue("contextTokens", 8192);
            s.setValue("schemaVersion", 2);
            s.sync();
        }
        {
            SettingsService svc(nullptr);
            QCOMPARE(svc.contextTokens(), AIAgent::kDefaultContextTokens);
        }

        // 中间版本的默认 300000（已升到 v3 的库）→ 同样归一。
        {
            QSettings s = NexusAI::TestSupport::testSettings();
            s.setValue("contextTokens", 300000);
            s.setValue("schemaVersion", 3);
            s.sync();
        }
        {
            SettingsService svc(nullptr);
            QCOMPARE(svc.contextTokens(), AIAgent::kDefaultContextTokens);
        }

        // 用户自定义值（≠ 任一旧默认）→ 保持不动。
        {
            QSettings s = NexusAI::TestSupport::testSettings();
            s.setValue("contextTokens", 65536);
            s.setValue("schemaVersion", 2);
            s.sync();
        }
        {
            SettingsService svc(nullptr);
            QCOMPARE(svc.contextTokens(), 65536);
        }

        // 未写过该键 → 直接拿到新默认（不需要迁移参与）。
        clean();
        {
            SettingsService svc(nullptr);
            QCOMPARE(svc.contextTokens(), AIAgent::kDefaultContextTokens);
        }
        clean();
    }

    // 默认值自检：账本登记的默认与 AIAgent 的常量必须一致（三处单一事实源的
    // 收口点；漂移会让 UI 显示与运行侧兜底对不上）。
    void defaultContextTokensMatchesAgentConstant() {
        QVariant ledger;
        for (const auto& k : SettingsSchema::registeredKeys())
            if (k.key == QStringLiteral("contextTokens")) ledger = k.defaultValue;
        QVERIFY2(ledger.isValid(), "contextTokens missing from schema ledger");
        QCOMPARE(ledger.toInt(), AIAgent::kDefaultContextTokens);
        // 保守取值：未知模型按 128k 兜底，避免用超大默认值撑爆小窗口模型。
        QCOMPARE(AIAgent::kDefaultContextTokens, 128000);
    }

    // L2: load() stamps the schema version into QSettings.
    void schemaVersionStampedOnLoad() {
        SettingsGuard guard;
        {
            QSettings s = NexusAI::TestSupport::testSettings();
            s.remove("schemaVersion");
        }
        SettingsService svc(nullptr);
        QSettings s = NexusAI::TestSupport::testSettings();
        QCOMPARE(s.value("schemaVersion").toInt(), SettingsSchema::kSchemaVersion);
    }

    // v1 → v2：模型列表改由用户手填，存量 profile 的 model/models 在 load 时清空；
    // 其余字段（地址、密钥、生成参数）与当前激活项不能动，而且迁移只跑一次（版本
    // 已顶到最新后再 load 不能把用户新填的模型又清掉）。
    void schemaV1ProfilesLoseModelsOnUpgrade() {
        SettingsGuard guard;
        {
            QSettings s = NexusAI::TestSupport::testSettings();
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

    // docs/24 H10: the pre-0.3 top-level apiKey is stored in PLAINTEXT —
    // after it is folded into an encrypted profile, the registry must not
    // keep the residue.
    void legacyPlaintextApiKeyRemovedAfterMigration() {
        SettingsGuard guard;
        {
            QSettings s = NexusAI::TestSupport::testSettings();
            s.remove("modelProfiles");
            s.setValue("provider", QStringLiteral("openai"));
            s.setValue("apiKey", QStringLiteral("legacy-plain-key"));
            s.sync();
        }
        QVERIFY(SettingsRepository::contains(QStringLiteral("apiKey")));

        SettingsService service(nullptr);
        // The key migrated into a (still readable) profile…
        QVERIFY(!service.profiles().isEmpty());
        // …and the plaintext residue is gone from the registry.
        QVERIFY(!SettingsRepository::contains(QStringLiteral("apiKey")));
        QVERIFY(!SettingsRepository::contains(QStringLiteral("provider")));
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
            QSettings s = NexusAI::TestSupport::testSettings();
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
            QSettings s = NexusAI::TestSupport::testSettings();
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
            QSettings s = NexusAI::TestSupport::testSettings();
            s.remove("dailyTokenQuota");
            s.remove("dailyCostQuotaCents");
            s.remove("monthlyTokenQuota");
            s.remove("monthlyCostQuotaCents");
            s.remove("strictQuotaMode");
        }
    }

    // 设置中心目录（SettingsCatalog）的登记纪律：schema 账本里的每个键都必须
    // 在目录里出现（含隐藏内部键），否则搜索找不到、跳转也定位不到。声明式偏好
    // 在账本里带 prefs/v1/ 前缀，目录键是偏好键本身。
    void catalogCoversEveryRegisteredKey() {
        auto* catalog = SettingsCatalog::instance();
        QVERIFY(catalog);

        for (const SettingsKeyDef& definition : SettingsSchema::registeredKeys()) {
            QString id = definition.key;
            if (id.startsWith(QStringLiteral("prefs/v1/")))
                id = id.mid(QStringLiteral("prefs/v1/").length());
            QVERIFY2(catalog->hasEntry(id),
                     qPrintable(QStringLiteral("catalog entry missing for settings key: %1")
                                    .arg(definition.key)));
        }

        // id 既是搜索结果的定位锚点，也是 QML 的 objectName：重名会让跳转
        // 落到错误控件，必须在目录层面就拦住。
        QSet<QString> ids;
        for (const QVariant& value : catalog->entries()) {
            const QVariantMap entry = value.toMap();
            const QString id = entry.value("id").toString();
            QVERIFY2(!id.isEmpty(), "catalog entry without id");
            QVERIFY2(!ids.contains(id), qPrintable(QStringLiteral("duplicate catalog id: %1").arg(id)));
            ids.insert(id);
            // 可见条目必须能被搜索列表完整渲染。
            QVERIFY2(!entry.value("title").toString().isEmpty(), qPrintable(id));
            QVERIFY2(!entry.value("category").toString().isEmpty(), qPrintable(id));
            QVERIFY2(!entry.value("categoryName").toString().isEmpty(), qPrintable(id));
            QCOMPARE(entry.value("anchor").toString(), id);
            QCOMPARE(entry.value("hidden").toBool(), false);
        }
        QVERIFY(!ids.isEmpty());
    }

    // 搜索是字段级的：标题、关键词、键名、分类名任意命中都算；空查询与
    // 无匹配返回空列表；隐藏内部键不出现在结果里。
    void catalogSearchMatchesFieldLevelMetadata() {
        auto* catalog = SettingsCatalog::instance();
        QVERIFY(catalog);

        QVERIFY(catalog->search(QString()).isEmpty());
        QVERIFY(catalog->search(QStringLiteral("   ")).isEmpty());
        QVERIFY(catalog->search(QStringLiteral("没有这个设置-xyz")).isEmpty());

        // 中文标题 / 关键词：联网搜索条目落在「工具与能力」页。
        const QVariantList zhipu = catalog->search(QStringLiteral("智谱"));
        QVERIFY(!zhipu.isEmpty());
        bool foundProvider = false;
        for (const QVariant& value : zhipu) {
            const QVariantMap entry = value.toMap();
            QCOMPARE(entry.value("category").toString(), QStringLiteral("tools"));
            if (entry.value("id").toString() == QStringLiteral("webSearch/provider"))
                foundProvider = true;
            // 隐藏的 provider 密钥桶不能出现在搜索结果里。
            QVERIFY(entry.value("id").toString() != QStringLiteral("webSearch/zhipuApiKey"));
        }
        QVERIFY(foundProvider);

        // 键名命中：autoStart 不在标题里，只能靠偏好键名/关键词命中。
        const QVariantList autoStart = catalog->search(QStringLiteral("autostart"));
        QVERIFY(!autoStart.isEmpty());
        QCOMPARE(autoStart.at(0).toMap().value("id").toString(),
                 QStringLiteral("general.autoStart"));

        // 分类名参与匹配（搜索「界面」应至少命中界面页的偏好键）。
        const QVariantList interface = catalog->search(QStringLiteral("界面与外观"));
        QVERIFY(!interface.isEmpty());
        for (const QVariant& value : interface) {
            QCOMPARE(value.toMap().value("category").toString(), QStringLiteral("interface"));
        }

        // 大小写不敏感 + 去首尾空格。
        const QVariantList upper = catalog->search(QStringLiteral("  PROXY  "));
        QVERIFY(!upper.isEmpty());
        bool foundProxyMode = false;
        for (const QVariant& value : upper) {
            if (value.toMap().value("id").toString() == QStringLiteral("network/proxyMode"))
                foundProxyMode = true;
        }
        QVERIFY(foundProxyMode);
    }

    // 「恢复默认」的可见性/分派来自目录元数据：页面级复位（M1 按域入口）、
    // 声明式偏好复位（EditBuffer）、以及两者都没有的分类。
    void catalogResetModesDriveResetVisibility() {
        auto* catalog = SettingsCatalog::instance();
        QVERIFY(catalog);

        // M1 键由页面 requestResetDefaults() 复位。
        QCOMPARE(catalog->resetMode(QStringLiteral("agent")), QStringLiteral("page"));
        QCOMPARE(catalog->resetMode(QStringLiteral("network")), QStringLiteral("page"));
        QCOMPARE(catalog->resetMode(QStringLiteral("knowledge")), QStringLiteral("page"));
        QCOMPARE(catalog->resetMode(QStringLiteral("memory")), QStringLiteral("page"));
        QCOMPARE(catalog->resetMode(QStringLiteral("logs")), QStringLiteral("page"));
        // 备份策略原先登记在 memory 且不可搜索；现已搬到备份页，页面
        // 通过 Settings.resetBackupToDefaults() 复位。
        QCOMPARE(catalog->resetMode(QStringLiteral("backup")), QStringLiteral("page"));
        QVERIFY(!catalog->search(QStringLiteral("备份保留")).isEmpty());

        // 声明式偏好由 EditBuffer.resetToDefault 复位。
        QCOMPARE(catalog->resetMode(QStringLiteral("interface")), QStringLiteral("preference"));
        QCOMPARE(catalog->resetMode(QStringLiteral("general")), QStringLiteral("preference"));

        // 整条 profile / 独立资产：没有可复位的键。
        QCOMPARE(catalog->resetMode(QStringLiteral("model")), QStringLiteral("none"));
        QCOMPARE(catalog->resetMode(QStringLiteral("mcp")), QStringLiteral("none"));
        QCOMPARE(catalog->resetMode(QStringLiteral("unknown")), QStringLiteral("none"));

        // 安全 / 工具 / 用量页没有 requestResetDefaults，但它们的单选开关、
        // 黑名单、配额都经 SettingsService 单键写入且有默认值（docs/31 S1
        // 遗留修复）：归类为 page，由底栏逐条 SettingsCatalog.resetValue
        // 复位——否则「恢复全部设置」的确认文案承诺了却不执行。
        QCOMPARE(catalog->resetMode(QStringLiteral("tools")), QStringLiteral("page"));
        QCOMPARE(catalog->resetMode(QStringLiteral("security")), QStringLiteral("page"));
        QCOMPARE(catalog->resetMode(QStringLiteral("usage")), QStringLiteral("page"));

        // 单条元数据：可见字段级条目与隐藏内部键。
        const QVariantMap localOnly = catalog->entry(QStringLiteral("security/localOnlyMode"));
        QCOMPARE(localOnly.value("category").toString(), QStringLiteral("security"));
        QCOMPARE(localOnly.value("resetMode").toString(), QStringLiteral("page"));
        QCOMPARE(localOnly.value("hidden").toBool(), false);

        const QVariantMap profiles = catalog->entry(QStringLiteral("modelProfiles"));
        QCOMPARE(profiles.value("hidden").toBool(), true);
        QVERIFY(catalog->entry(QStringLiteral("does-not-exist")).isEmpty());
    }

private:
    static void clearThemeKeys() {
        QSettings s = NexusAI::TestSupport::testSettings();
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
    QStandardPaths::setTestModeEnabled(true);
    TestSettingsService tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_settings.moc"
