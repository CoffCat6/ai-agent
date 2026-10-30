#include <QtTest>

#include "core/mcp/McpConfig.h"
#include "viewmodels/SettingsBackup.h"
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

    // 设置页「编辑服务器」的数据链路：updateServer 必须在原条目上生效
    // （含改名），保留启用状态，并把 env / headers 一起写回磁盘。
    // 旧实现的替代路径是删了重加——每删一次，服务器提供的工具全部掉线。
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
