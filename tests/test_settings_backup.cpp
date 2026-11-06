#include <QtTest>

#include "core/mcp/McpConfig.h"
#include "core/utils/BackupSchedule.h"
#include "core/utils/SettingsSchema.h"
#include "viewmodels/SettingsBackup.h"
#include "viewmodels/SettingsService.h"
#include "viewmodels/McpManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include "TestSettingsIsolation.h"

// 测试进程的 QSettings 重定向到临时 INI（见 TestSettingsIsolation.h），
// 防止测试改写用户真实设置。
namespace {
const NexusAI::TestSupport::SettingsIsolation kSettingsIsolation;
} // namespace

using namespace NexusAI;

class TestSettingsBackup : public QObject {
    Q_OBJECT
private slots:
    void credentialFreeRoundTrip() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        QCoreApplication::setOrganizationName(QStringLiteral("NexusAIBackupTests"));
        QCoreApplication::setApplicationName(
            QStringLiteral("NexusAIBackupTests_%1")
                .arg(QFileInfo(directory.path()).fileName()));
        QStandardPaths::setTestModeEnabled(true);

        const QString mcpPath = McpConfig::configFilePath();
        QFile original(mcpPath);
        const bool existed = original.exists();
        QByteArray previous;
        if (existed) {
            QVERIFY(original.open(QIODevice::ReadOnly));
            previous = original.readAll();
        }

        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("NexusAI"), QStringLiteral("NexusAI"));
        settings.setValue(QStringLiteral("schemaVersion"), 4);
        settings.setValue(QStringLiteral("embedding/model"), QStringLiteral("my-embed"));
        settings.setValue(QStringLiteral("embedding/apiKey"), QStringLiteral("hidden-embed-key"));
        settings.setValue(QStringLiteral("security/allowFullDiskRead"), true);
        settings.setValue(QStringLiteral("webSearch/zhipuApiKey"), QStringLiteral("hidden-web-key"));
        settings.setValue(QStringLiteral("webSearch/deepseekApiKey"), QStringLiteral("hidden-web-key-2"));
        settings.setValue(QStringLiteral("modelProfiles"), QString::fromUtf8(
            QJsonDocument(QJsonArray{QJsonObject{
                {QStringLiteral("id"), QStringLiteral("profile-1")},
                {QStringLiteral("provider"), QStringLiteral("openai")},
                {QStringLiteral("apiProtocol"), QStringLiteral("openai-chat")},
                {QStringLiteral("model"), QStringLiteral("my-model")},
                {QStringLiteral("apiKey"), QStringLiteral("hidden-model-key")}}})
                .toJson(QJsonDocument::Compact)));
        settings.setValue(QStringLiteral("activeProfileId"), QStringLiteral("profile-1"));
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);

        QVERIFY(McpConfig::save({QVariantMap{
            {QStringLiteral("name"), QStringLiteral("example")},
            {QStringLiteral("transport"), QStringLiteral("stdio")},
            {QStringLiteral("command"), QStringLiteral("server")},
            {QStringLiteral("enabled"), true},
            {QStringLiteral("env"), QVariantMap{{QStringLiteral("TOKEN"), QStringLiteral("hidden-mcp-key")}}},
            {QStringLiteral("headers"), QVariantMap{{QStringLiteral("Authorization"), QStringLiteral("hidden-header")}}}}}));

        SettingsBackup backup;
        const QString exportPath = directory.filePath(QStringLiteral("backup.json"));
        QVERIFY2(backup.exportSettings(exportPath), qPrintable(backup.lastError()));
        QFile exported(exportPath);
        QVERIFY(exported.open(QIODevice::ReadOnly));
        const QByteArray bytes = exported.readAll();
        QVERIFY(!bytes.contains("hidden-model-key"));
        QVERIFY(!bytes.contains("hidden-web-key"));
        QVERIFY(!bytes.contains("hidden-web-key-2"));
        QVERIFY(!bytes.contains("hidden-embed-key"));
        QVERIFY(!bytes.contains("hidden-mcp-key"));
        QVERIFY(!bytes.contains("hidden-header"));
        const QVariantMap preview = backup.previewImport(exportPath);
        QVERIFY(preview.value(QStringLiteral("valid")).toBool());

        settings.setValue(QStringLiteral("embedding/model"), QStringLiteral("other"));
        QVERIFY2(backup.importSettings(exportPath), qPrintable(backup.lastError()));
        QVERIFY(backup.restartRequired());
        settings.sync();
        QCOMPARE(settings.value(QStringLiteral("embedding/model")).toString(), QStringLiteral("my-embed"));
        QCOMPARE(settings.value(QStringLiteral("webSearch/zhipuApiKey")).toString(), QStringLiteral("hidden-web-key"));
        QCOMPARE(settings.value(QStringLiteral("webSearch/deepseekApiKey")).toString(), QStringLiteral("hidden-web-key-2"));
        QVERIFY(settings.value(QStringLiteral("security/allowFullDiskRead")).toBool());
        QVERIFY(settings.value(QStringLiteral("modelProfiles")).toString().contains(QStringLiteral("hidden-model-key")));
        const auto restored = McpConfig::load();
        QCOMPARE(restored.size(), 1);
        QVERIFY(!restored.first().value(QStringLiteral("enabled")).toBool());
        QCOMPARE(restored.first().value(QStringLiteral("env")).toMap().value(QStringLiteral("TOKEN")).toString(),
                 QStringLiteral("hidden-mcp-key"));
        QCOMPARE(restored.first().value(QStringLiteral("headers")).toMap().value(QStringLiteral("Authorization")).toString(),
                 QStringLiteral("hidden-header"));
        QCOMPARE(McpManager::instance()->servers().size(), 1);
        QVERIFY(!McpManager::instance()->servers().first().toMap().value(QStringLiteral("enabled")).toBool());
        McpManager::instance()->setEnabled(QStringLiteral("example"), false);
        QCOMPARE(McpConfig::load().first().value(QStringLiteral("env")).toMap().value(QStringLiteral("TOKEN")).toString(),
                 QStringLiteral("hidden-mcp-key"));

        QJsonObject injected = QJsonDocument::fromJson(bytes).object();
        QJsonArray invalidProfiles = injected.value(QStringLiteral("profiles")).toArray();
        QJsonObject invalidProfile = invalidProfiles.first().toObject();
        invalidProfile.insert(QStringLiteral("provider"), QStringLiteral("unknown"));
        invalidProfile.insert(QStringLiteral("apiProtocol"), QStringLiteral("unknown"));
        invalidProfiles.replace(0, invalidProfile);
        QJsonObject invalid = injected;
        invalid.insert(QStringLiteral("profiles"), invalidProfiles);
        const QString invalidPath = directory.filePath(QStringLiteral("invalid.json"));
        QFile invalidFile(invalidPath);
        QVERIFY(invalidFile.open(QIODevice::WriteOnly));
        invalidFile.write(QJsonDocument(invalid).toJson());
        invalidFile.close();
        QVERIFY(!backup.previewImport(invalidPath).value(QStringLiteral("valid")).toBool());

        // Same ID but different endpoint must not receive this machine's credentials.
        QJsonArray changedProfiles = injected.value(QStringLiteral("profiles")).toArray();
        QJsonObject changedProfile = changedProfiles.first().toObject();
        changedProfile.insert(QStringLiteral("baseUrl"), QStringLiteral("https://other.example"));
        changedProfiles.replace(0, changedProfile);
        QJsonObject changed = injected;
        changed.insert(QStringLiteral("profiles"), changedProfiles);
        QJsonArray changedServers = changed.value(QStringLiteral("mcpServers")).toArray();
        QJsonObject changedServer = changedServers.first().toObject();
        changedServer.insert(QStringLiteral("command"), QStringLiteral("other-server"));
        changedServers.replace(0, changedServer);
        changed.insert(QStringLiteral("mcpServers"), changedServers);
        const QString changedPath = directory.filePath(QStringLiteral("changed.json"));
        QFile changedFile(changedPath);
        QVERIFY(changedFile.open(QIODevice::WriteOnly));
        changedFile.write(QJsonDocument(changed).toJson());
        changedFile.close();
        QVERIFY2(backup.importSettings(changedPath), qPrintable(backup.lastError()));
        QVERIFY(!settings.value(QStringLiteral("modelProfiles")).toString().contains(QStringLiteral("hidden-model-key")));
        QVERIFY(McpConfig::load().first().value(QStringLiteral("env")).toMap().isEmpty());

        // A rejected import must leave the current settings intact.
        QJsonObject injectedSettings = injected.value(QStringLiteral("settings")).toObject();
        injectedSettings.insert(QStringLiteral("security/allowProjectWrite"), true);
        injected.insert(QStringLiteral("settings"), injectedSettings);
        const QString unsafePath = directory.filePath(QStringLiteral("unsafe.json"));
        QFile unsafe(unsafePath);
        QVERIFY(unsafe.open(QIODevice::WriteOnly));
        unsafe.write(QJsonDocument(injected).toJson());
        unsafe.close();
        QVERIFY(!backup.previewImport(unsafePath).value(QStringLiteral("valid")).toBool());
        QVERIFY(!backup.importSettings(unsafePath));
        QCOMPARE(settings.value(QStringLiteral("embedding/model")).toString(), QStringLiteral("my-embed"));

        if (existed) {
            QSaveFile saved(mcpPath);
            QVERIFY(saved.open(QIODevice::WriteOnly));
            QCOMPARE(saved.write(previous), previous.size());
            QVERIFY(saved.commit());
        } else {
            QVERIFY(QFile::remove(mcpPath));
        }
    }

    // ── docs/43 W4/AC-7：modelParams 备份链路 ─────────────────────

    // 导出白名单：modelParams（按模型参数覆盖表，模型名 → {参数键: 值}）
    // 必须整 map 进备份；topP（v5 幽灵键，v6 迁移已删存量数据）必须被白名单
    // 摘除；apiKey 永不落盘。种入走 updateProfile 通用合并（docs/43 §3.2：
    // 对未知键无白名单，整 map 提交即落库），顺带覆盖 SettingsService →
    // QSettings → 导出全链路，而不是只测 cleanProfile 的纯函数行为。
    void backupExportsModelParamsAndDropsTopP() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        QCoreApplication::setOrganizationName(QStringLiteral("NexusAIBackupTests"));
        QCoreApplication::setApplicationName(
            QStringLiteral("NexusAIBackupTests_%1")
                .arg(QFileInfo(directory.path()).fileName()));
        QStandardPaths::setTestModeEnabled(true);
        {
            QSettings s = NexusAI::TestSupport::testSettings();
            s.remove(QStringLiteral("modelProfiles"));
            s.remove(QStringLiteral("activeProfileId"));
            s.sync();
        }

        // 覆盖值以 trim 后的字符串落库（与 profile 顶层参数的 QML 提交风格
        // 一致），全部取合法值（sanitizeModelParams 会清洗非法提交）。
        const QVariantMap seedParams{
            {QStringLiteral("glm-4"), QVariantMap{
                {QStringLiteral("temperature"), QStringLiteral("0.5")},
                {QStringLiteral("contextLength"), QStringLiteral("128000")}}},
            {QStringLiteral("gpt-4o"), QVariantMap{
                {QStringLiteral("maxOutputTokens"), QStringLiteral("8192")},
                {QStringLiteral("reasoningEffort"), QStringLiteral("high")}}}};
        SettingsService service(nullptr);
        const QString profileId = service.activeProfileId();
        service.updateProfile(profileId, QVariantMap{
            {QStringLiteral("model"), QStringLiteral("glm-4")},
            {QStringLiteral("models"), QStringList{QStringLiteral("glm-4"),
                                                   QStringLiteral("gpt-4o")}},
            {QStringLiteral("apiKey"), QStringLiteral("hidden-model-key")},
            {QStringLiteral("topP"), 0.9},
            {QStringLiteral("modelParams"), seedParams}});
        // 种入自检：topP 顶层幽灵键照常透传（sanitizeModelParams 只清洗
        // modelParams 键，不碰顶层）；公共出口无密钥。
        const QVariantMap stored = service.profiles().first().toMap();
        QVERIFY2(stored.contains(QStringLiteral("topP")),
                 "种入前置：updateProfile 必须透传存量幽灵键 topP");
        QVERIFY2(stored.contains(QStringLiteral("modelParams")),
                 "种入前置：updateProfile 必须透传 modelParams");
        QVERIFY(!stored.contains(QStringLiteral("apiKey")));

        SettingsBackup backup;
        const QString exportPath = directory.filePath(QStringLiteral("params.json"));
        QVERIFY2(backup.exportSettings(exportPath), qPrintable(backup.lastError()));
        QFile exported(exportPath);
        QVERIFY(exported.open(QIODevice::ReadOnly));
        const QByteArray bytes = exported.readAll();

        const QJsonObject profile = QJsonDocument::fromJson(bytes)
            .object().value(QStringLiteral("profiles")).toArray().at(0).toObject();
        // modelParams 整 map 进备份（QJsonObject 相等为键值集合相等，与顺序无关）。
        QCOMPARE(profile.value(QStringLiteral("modelParams")).toObject(),
                 QJsonObject::fromVariantMap(seedParams));
        // topP 被白名单摘除——导出全文不得再出现幽灵键；apiKey 不落盘。
        QVERIFY2(!profile.contains(QStringLiteral("topP")),
                 "docs/43 §7 #6：导出白名单不得再包含幽灵键 topP");
        QVERIFY(!profile.contains(QStringLiteral("apiKey")));
        QVERIFY(!bytes.contains("topP"));
        QVERIFY(!bytes.contains("hidden-model-key"));
    }

    // 恢复链路：modelParams 经 cleanProfile 透传（导入侧与导出侧共用同一
    // 白名单，一处改动双侧生效），清空存储后恢复能完整找回覆盖表（深比较），
    // 身份字段不变。备份层不做值的 sanitize——那是写入侧
    // SettingsService::sanitizeModelParams 的单一出口职责。
    void restoredProfileKeepsModelParams() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        QCoreApplication::setOrganizationName(QStringLiteral("NexusAIBackupTests"));
        QCoreApplication::setApplicationName(
            QStringLiteral("NexusAIBackupTests_%1")
                .arg(QFileInfo(directory.path()).fileName()));
        QStandardPaths::setTestModeEnabled(true);
        {
            QSettings s = NexusAI::TestSupport::testSettings();
            s.remove(QStringLiteral("modelProfiles"));
            s.remove(QStringLiteral("activeProfileId"));
            s.sync();
        }

        const QVariantMap seedParams{
            {QStringLiteral("glm-4"), QVariantMap{
                {QStringLiteral("temperature"), QStringLiteral("0.5")},
                {QStringLiteral("contextLength"), QStringLiteral("128000")}}},
            {QStringLiteral("gpt-4o"), QVariantMap{
                {QStringLiteral("maxOutputTokens"), QStringLiteral("8192")},
                {QStringLiteral("reasoningEffort"), QStringLiteral("low")}}}};
        SettingsService service(nullptr);
        const QString profileId = service.activeProfileId();
        service.updateProfile(profileId, QVariantMap{
            {QStringLiteral("model"), QStringLiteral("glm-4")},
            {QStringLiteral("models"), QStringList{QStringLiteral("glm-4"),
                                                   QStringLiteral("gpt-4o")}},
            {QStringLiteral("modelParams"), seedParams}});

        SettingsBackup backup;
        const QString exportPath = directory.filePath(QStringLiteral("roundtrip.json"));
        QVERIFY2(backup.exportSettings(exportPath), qPrintable(backup.lastError()));

        // 清空存储，模拟换机/重置后从备份恢复。
        {
            QSettings s = NexusAI::TestSupport::testSettings();
            s.remove(QStringLiteral("modelProfiles"));
            s.remove(QStringLiteral("activeProfileId"));
            s.sync();
        }
        QVERIFY2(backup.importSettings(exportPath), qPrintable(backup.lastError()));
        QVERIFY(backup.restartRequired());

        // 落盘的 modelProfiles JSON：modelParams 完整保留（深比较）。
        QSettings s = NexusAI::TestSupport::testSettings();
        const QJsonArray restored = QJsonDocument::fromJson(
            s.value(QStringLiteral("modelProfiles")).toString().toUtf8()).array();
        QCOMPARE(restored.size(), 1);
        const QJsonObject profile = restored.at(0).toObject();
        QCOMPARE(profile.value(QStringLiteral("id")).toString(), profileId);
        QCOMPARE(profile.value(QStringLiteral("provider")).toString(),
                 QStringLiteral("openai"));
        QCOMPARE(profile.value(QStringLiteral("modelParams")).toObject(),
                 QJsonObject::fromVariantMap(seedParams));
        // importSettings 会落一份空 MCP 配置（McpConfig::save({})），清掉
        // 避免跨用例残留。
        QVERIFY(QFile::remove(McpConfig::configFilePath()));
    }

    // docs/43 §5.3/AC-7：恢复侧强校验维持现状——v6 应用拒绝 v5 备份（双向
    // 兼容边界，与历次 schema 升级行为一致）。既有用例未覆盖该拒绝路径，
    // 此处补回归保护：把合法导出的 schemaVersion 手工改成 5。
    void v5BackupRejectedByVersionCheck() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        QCoreApplication::setOrganizationName(QStringLiteral("NexusAIBackupTests"));
        QCoreApplication::setApplicationName(
            QStringLiteral("NexusAIBackupTests_%1")
                .arg(QFileInfo(directory.path()).fileName()));
        QStandardPaths::setTestModeEnabled(true);
        {
            QSettings s = NexusAI::TestSupport::testSettings();
            s.clear();
            s.setValue(QStringLiteral("modelProfiles"), QString::fromUtf8(
                QJsonDocument(QJsonArray{QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("profile-v5")},
                    {QStringLiteral("provider"), QStringLiteral("openai")},
                    {QStringLiteral("apiProtocol"), QStringLiteral("openai-chat")},
                    {QStringLiteral("model"), QStringLiteral("gpt-4o")}}})
                    .toJson(QJsonDocument::Compact)));
            s.setValue(QStringLiteral("activeProfileId"), QStringLiteral("profile-v5"));
            s.sync();
            QCOMPARE(s.status(), QSettings::NoError);
        }

        SettingsBackup backup;
        const QString exportPath = directory.filePath(QStringLiteral("current.json"));
        QVERIFY2(backup.exportSettings(exportPath), qPrintable(backup.lastError()));
        QFile exported(exportPath);
        QVERIFY(exported.open(QIODevice::ReadOnly));
        QJsonObject root = QJsonDocument::fromJson(exported.readAll()).object();
        exported.close();
        // 手工降级成 v5 备份：其余字段保持合法，只有版本号是旧的。
        QCOMPARE(root.value(QStringLiteral("schemaVersion")).toInt(),
                 SettingsSchema::kSchemaVersion);
        root.insert(QStringLiteral("schemaVersion"), 5);
        const QString v5Path = directory.filePath(QStringLiteral("v5.json"));
        QFile v5File(v5Path);
        QVERIFY(v5File.open(QIODevice::WriteOnly));
        v5File.write(QJsonDocument(root).toJson());
        v5File.close();

        const QVariantMap preview = backup.previewImport(v5Path);
        QVERIFY2(!preview.value(QStringLiteral("valid")).toBool(),
                 "v5 备份必须被版本强校验拒绝");
        QVERIFY(preview.value(QStringLiteral("error")).toString()
                    .contains(QStringLiteral("设置版本不匹配")));
        QVERIFY2(!backup.importSettings(v5Path),
                 "v5 备份不得进入恢复流程");
        QVERIFY(backup.lastError().contains(QStringLiteral("设置版本不匹配")));

        // 拒绝恢复不得动当前存储。
        QSettings s = NexusAI::TestSupport::testSettings();
        QVERIFY(s.value(QStringLiteral("modelProfiles")).toString()
                    .contains(QStringLiteral("profile-v5")));
    }

    // docs/31 O5：按分类导出。只导出 model 时，settings 里只能有模型
    // 分类的键，profiles 保留、mcpServers 为空；产物仍是合法的全量格式，
    // 可直接走现有导入流程。
    void exportByCategoryFiltersKeys() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           directory.path());
        QStandardPaths::setTestModeEnabled(true);

        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("NexusAI"), QStringLiteral("NexusAI"));
        settings.clear();
        settings.setValue(QStringLiteral("embedding/model"),
                          QStringLiteral("keep-out"));
        settings.setValue(QStringLiteral("webSearch/provider"),
                          QStringLiteral("keep-out"));
        settings.setValue(QStringLiteral("activeProfileId"),
                          QStringLiteral("profile-model"));
        settings.setValue(QStringLiteral("modelProfiles"), QString::fromUtf8(
            QJsonDocument(QJsonArray{QJsonObject{
                {QStringLiteral("id"), QStringLiteral("profile-model")},
                {QStringLiteral("provider"), QStringLiteral("openai")},
                {QStringLiteral("apiProtocol"), QStringLiteral("openai-chat")},
                {QStringLiteral("model"), QStringLiteral("model-a")}}})
                .toJson(QJsonDocument::Compact)));
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);

        SettingsBackup backup;
        const QString exportPath = directory.filePath(QStringLiteral("model.json"));
        QVERIFY2(backup.exportSettings(exportPath, {QStringLiteral("model")}),
                 qPrintable(backup.lastError()));
        QFile exported(exportPath);
        QVERIFY(exported.open(QIODevice::ReadOnly));
        const QJsonObject root =
            QJsonDocument::fromJson(exported.readAll()).object();
        const QJsonObject saved = root.value(QStringLiteral("settings")).toObject();
        QVERIFY(saved.contains(QStringLiteral("activeProfileId")));
        QVERIFY(!saved.contains(QStringLiteral("embedding/model")));
        QVERIFY(!saved.contains(QStringLiteral("webSearch/provider")));
        QCOMPARE(root.value(QStringLiteral("profiles")).toArray().size(), 1);
        QCOMPARE(root.value(QStringLiteral("mcpServers")).toArray().size(), 0);

        // 范围选择器只列真正可导出的分类：security/* 与 tools/disabled
        // 被白名单排除，不应出现在列表里。
        const QStringList exportable = backup.exportableCategories();
        QVERIFY(exportable.contains(QStringLiteral("model")));
        QVERIFY(exportable.contains(QStringLiteral("mcp")));
        QVERIFY(exportable.contains(QStringLiteral("knowledge")));
        QVERIFY(!exportable.contains(QStringLiteral("security")));

        // 分类子集仍是合法的导入文件。
        const QVariantMap preview = backup.previewImport(exportPath);
        QVERIFY2(preview.value(QStringLiteral("valid")).toBool(),
                 qPrintable(preview.value(QStringLiteral("error")).toString()));
        QCOMPARE(preview.value(QStringLiteral("changesTotal")).toInt(), 0);
    }

    // docs/31 O4：导入预览逐键 diff。完全一致 → 0 条；改两个键 → 恰好
    // 两条，且带分类、标题与"当前 → 备份"的可读摘要。
    void previewImportReportsPerKeyChanges() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           directory.path());
        QStandardPaths::setTestModeEnabled(true);

        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("NexusAI"), QStringLiteral("NexusAI"));
        settings.clear();
        settings.setValue(QStringLiteral("embedding/model"),
                          QStringLiteral("base-embed"));
        settings.setValue(QStringLiteral("logging/level"), QStringLiteral("info"));
        settings.setValue(QStringLiteral("webSearch/provider"),
                          QStringLiteral("duckduckgo"));
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);

        SettingsBackup backup;
        const QString exportPath = directory.filePath(QStringLiteral("diff.json"));
        QVERIFY2(backup.exportSettings(exportPath), qPrintable(backup.lastError()));

        QVariantMap preview = backup.previewImport(exportPath);
        QVERIFY(preview.value(QStringLiteral("valid")).toBool());
        QCOMPARE(preview.value(QStringLiteral("changes")).toList().size(), 0);
        QCOMPARE(preview.value(QStringLiteral("changesTotal")).toInt(), 0);
        QCOMPARE(preview.value(QStringLiteral("changesTruncated")).toInt(), 0);

        settings.setValue(QStringLiteral("embedding/model"),
                          QStringLiteral("changed-embed"));
        settings.setValue(QStringLiteral("logging/level"), QStringLiteral("debug"));
        settings.sync();

        preview = backup.previewImport(exportPath);
        const QVariantList changes = preview.value(QStringLiteral("changes")).toList();
        QCOMPARE(preview.value(QStringLiteral("changesTotal")).toInt(), 2);
        QCOMPARE(changes.size(), 2);
        QCOMPARE(preview.value(QStringLiteral("changesTruncated")).toInt(), 0);

        bool sawEmbedding = false;
        bool sawLogging = false;
        for (const QVariant& value : changes) {
            const QVariantMap change = value.toMap();
            const QString key = change.value(QStringLiteral("key")).toString();
            QVERIFY(!change.value(QStringLiteral("title")).toString().isEmpty());
            QVERIFY(!change.value(QStringLiteral("categoryName")).toString().isEmpty());
            QVERIFY(!change.value(QStringLiteral("secret")).toBool());
            if (key == QStringLiteral("embedding/model")) {
                sawEmbedding = true;
                QCOMPARE(change.value(QStringLiteral("category")).toString(),
                         QStringLiteral("knowledge"));
                QCOMPARE(change.value(QStringLiteral("currentDisplay")).toString(),
                         QStringLiteral("changed-embed"));
                QCOMPARE(change.value(QStringLiteral("importedDisplay")).toString(),
                         QStringLiteral("base-embed"));
            } else if (key == QStringLiteral("logging/level")) {
                sawLogging = true;
                QCOMPARE(change.value(QStringLiteral("category")).toString(),
                         QStringLiteral("logs"));
                QCOMPARE(change.value(QStringLiteral("currentDisplay")).toString(),
                         QStringLiteral("调试"));
                QCOMPARE(change.value(QStringLiteral("importedDisplay")).toString(),
                         QStringLiteral("信息"));
            }
        }
        QVERIFY(sawEmbedding);
        QVERIFY(sawLogging);
    }

    // 设置页「编辑服务器」的数据链路：updateServer 必须在原条目上生效
    // （含改名），保留启用状态，并把 env / headers 一起写回磁盘。
    // 旧实现的替代路径是删了重加——每删一次，服务器提供的工具全部掉线。
    // docs/31 S4.2 N6：定时备份只开放 0/1/6/24/168 五档。归一取"不超过
    // 输入值的最大档"（备份频率不低于用户写入的意图），负数/0 = 关闭。
    // 这些是纯函数，不需要碰设置仓库，所以直接断言边界。
    void backupIntervalScheduleIsStepLocked() {
        QCOMPARE(normalizeBackupIntervalHours(-5), 0);
        QCOMPARE(normalizeBackupIntervalHours(0), 0);
        QCOMPARE(normalizeBackupIntervalHours(1), 1);
        QCOMPARE(normalizeBackupIntervalHours(2), 1);
        QCOMPARE(normalizeBackupIntervalHours(6), 6);
        QCOMPARE(normalizeBackupIntervalHours(23), 6);
        QCOMPARE(normalizeBackupIntervalHours(24), 24);
        QCOMPARE(normalizeBackupIntervalHours(167), 24);
        QCOMPARE(normalizeBackupIntervalHours(168), 168);
        QCOMPARE(normalizeBackupIntervalHours(9999), 168);

        // 0 必须映射为 0 毫秒——main.cpp 据此停掉定时器。
        QCOMPARE(backupIntervalMs(0), qint64(0));
        QCOMPARE(backupIntervalMs(1), qint64(60 * 60 * 1000));
        QCOMPARE(backupIntervalMs(6), qint64(6 * 60 * 60 * 1000));
        QCOMPARE(backupIntervalMs(24), qint64(24 * 60 * 60 * 1000));
        QCOMPARE(backupIntervalMs(168), qint64(168 * 60 * 60 * 1000));
        // 非档位值先归一：37 → 24 小时。
        QCOMPARE(backupIntervalMs(37), qint64(24 * 60 * 60 * 1000));

        // 档位表与 schema 的范围上限必须对得上，否则 UI 能选到被 clamp
        // 掉的值。
        static_assert(kBackupIntervalHours[kBackupIntervalOptionCount - 1]
                          == kMaxBackupIntervalHours,
                      "last option must match the schema range");
    }

    void mcpServerUpdateRoundTrip() {
        const QString configPath = McpConfig::configFilePath();
        QFile original(configPath);
        const bool existed = original.exists();
        QByteArray previous;
        if (existed) {
            QVERIFY(original.open(QIODevice::ReadOnly));
            previous = original.readAll();
        }
        QFile::remove(configPath);

        auto* manager = McpManager::instance();
        manager->applyRestoredConfigs({});
        QVERIFY(manager->servers().isEmpty());

        // 先建一条禁用条目（不触发进程启动），再改名并补 HTTP 字段。
        QVERIFY(manager->addServer(QVariantMap{
            {QStringLiteral("name"), QStringLiteral("t-edit")},
            {QStringLiteral("transport"), QStringLiteral("stdio")},
            {QStringLiteral("command"), QStringLiteral("cmd")},
            {QStringLiteral("enabled"), false}}));
        QVERIFY(!manager->addServer(QVariantMap{
            {QStringLiteral("name"), QStringLiteral("t-edit")},
            {QStringLiteral("transport"), QStringLiteral("stdio")},
            {QStringLiteral("command"), QStringLiteral("cmd")},
            {QStringLiteral("enabled"), false}}));

        QVERIFY(manager->updateServer(QStringLiteral("t-edit"), QVariantMap{
            {QStringLiteral("name"), QStringLiteral("t-edit-2")},
            {QStringLiteral("transport"), QStringLiteral("http")},
            {QStringLiteral("url"), QStringLiteral("https://example.invalid/mcp")},
            {QStringLiteral("headers"), QVariantMap{
                {QStringLiteral("Authorization"), QStringLiteral("Bearer x")}}}}));

        const QVariantList servers = manager->servers();
        QCOMPARE(servers.size(), 1);
        const QVariantMap updated = servers.first().toMap();
        QCOMPARE(updated.value(QStringLiteral("name")).toString(),
                 QStringLiteral("t-edit-2"));
        QCOMPARE(updated.value(QStringLiteral("transport")).toString(),
                 QStringLiteral("http"));
        QCOMPARE(updated.value(QStringLiteral("url")).toString(),
                 QStringLiteral("https://example.invalid/mcp"));
        QCOMPARE(updated.value(QStringLiteral("headers")).toMap()
                     .value(QStringLiteral("Authorization")).toString(),
                 QStringLiteral("Bearer x"));
        // 编辑不改变启用状态：弹窗里没有这个开关。
        QVERIFY(!updated.value(QStringLiteral("enabled")).toBool());

        const auto persisted = McpConfig::load();
        QCOMPARE(persisted.size(), 1);
        QCOMPARE(persisted.first().value(QStringLiteral("name")).toString(),
                 QStringLiteral("t-edit-2"));

        // 改名撞上已有条目必须失败，且不破坏现有配置。
        QVERIFY(manager->addServer(QVariantMap{
            {QStringLiteral("name"), QStringLiteral("t-other")},
            {QStringLiteral("transport"), QStringLiteral("stdio")},
            {QStringLiteral("command"), QStringLiteral("cmd")},
            {QStringLiteral("enabled"), false}}));
        QVERIFY(!manager->updateServer(QStringLiteral("t-edit-2"), QVariantMap{
            {QStringLiteral("name"), QStringLiteral("t-other")},
            {QStringLiteral("transport"), QStringLiteral("http")},
            {QStringLiteral("url"), QStringLiteral("https://example.invalid/mcp")}}));
        QCOMPARE(manager->servers().size(), 2);
        QCOMPARE(McpConfig::load().size(), 2);

        manager->removeServer(QStringLiteral("t-edit-2"));
        manager->removeServer(QStringLiteral("t-other"));
        QVERIFY(manager->servers().isEmpty());

        if (existed) {
            QSaveFile saved(configPath);
            QVERIFY(saved.open(QIODevice::WriteOnly));
            QCOMPARE(saved.write(previous), previous.size());
            QVERIFY(saved.commit());
        } else {
            QVERIFY(QFile::remove(configPath));
        }
    }
};

QTEST_GUILESS_MAIN(TestSettingsBackup)
#include "test_settings_backup.moc"
