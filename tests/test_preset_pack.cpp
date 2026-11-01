// docs/31 S4.6 N8：可分享预设包（预设 / 斜杠指令 / 禁用工具 / 默认权限模式）
// 的导出、预览、冲突策略与拒绝路径。设置写盘全部指向 QTemporaryDir 的 INI
// 文件（红线 7），不碰真实用户配置。
#include "core/tools/ToolRegistry.h"
#include "core/utils/SettingsSchema.h"
#include "viewmodels/PresetPackTransfer.h"
#include "viewmodels/SettingsService.h"

#include <QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

namespace {

QVariantMap packRoot()
{
    return QVariantMap{{QStringLiteral("format"), QStringLiteral("nexus-preset-pack")},
                       {QStringLiteral("version"), 1},
                       {QStringLiteral("presets"), QJsonArray{}},
                       {QStringLiteral("commands"), QJsonArray{}},
                       {QStringLiteral("disabledTools"), QJsonArray{}},
                       {QStringLiteral("permissionMode"), QStringLiteral("confirm")}};
}

QString writePack(const QTemporaryDir& dir, const QVariantMap& root,
                  const QString& fileName = QStringLiteral("pack.json"))
{
    const QString path = dir.filePath(fileName);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return QString();
    file.write(QJsonDocument(QJsonObject::fromVariantMap(root)).toJson());
    file.close();
    return path;
}

QVariantMap entryNamed(const QVariantList& list, const QString& name)
{
    for (const QVariant& value : list) {
        const QVariantMap map = value.toMap();
        if (map.value(QStringLiteral("name")).toString() == name) return map;
    }
    return {};
}

} // namespace

class TestPresetPack : public QObject {
    Q_OBJECT

private:
    static void resetLocalState()
    {
        auto* settings = SettingsService::instance();
        QVERIFY(settings->applyPromptAssets({}, {}));
        QVERIFY(ToolRegistry::instance().setDisabledTools({}));
        settings->setDefaultPermissionMode(QStringLiteral("confirm"));
    }

    static void seedLocalAssets()
    {
        auto* settings = SettingsService::instance();
        QVERIFY(settings->applyPromptAssets(
            {QVariantMap{{QStringLiteral("id"), QStringLiteral("local-preset")},
                         {QStringLiteral("name"), QStringLiteral("Shared")},
                         {QStringLiteral("prompt"), QStringLiteral("local prompt")}}},
            {QVariantMap{{QStringLiteral("id"), QStringLiteral("local-command")},
                         {QStringLiteral("name"), QStringLiteral("review")},
                         {QStringLiteral("templ"), QStringLiteral("local {text}")},
                         {QStringLiteral("note"), QStringLiteral("local note")}}}));
    }

private slots:
    void init() { resetLocalState(); }
    void cleanup() { resetLocalState(); }

    void emptyConfigurationImportRestoresAllGroups()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QVariantMap root = packRoot();
        root.insert(QStringLiteral("presets"),
                    QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("Reviewer")},
                                           {QStringLiteral("prompt"), QStringLiteral("Be strict.")}},
                               QJsonObject{{QStringLiteral("name"), QStringLiteral("Mentor")},
                                           {QStringLiteral("prompt"), QStringLiteral("Explain.")}}});
        root.insert(QStringLiteral("commands"),
                    QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("/Review2")},
                                           {QStringLiteral("templ"), QStringLiteral("Review {text}")},
                                           {QStringLiteral("note"), QStringLiteral("Review code")}},
                               QJsonObject{{QStringLiteral("name"), QStringLiteral("ship")},
                                           {QStringLiteral("templ"), QStringLiteral("Ship {text}")}}});
        root.insert(QStringLiteral("disabledTools"),
                    QJsonArray{QStringLiteral("shell_exec"),
                               QStringLiteral("not_a_registered_tool")});
        root.insert(QStringLiteral("permissionMode"), QStringLiteral("plan_only"));
        const QString path = writePack(dir, root);
        QVERIFY(!path.isEmpty());

        auto* transfer = PresetPackTransfer::instance();
        const QVariantMap preview = transfer->previewPack(path);
        QVERIFY2(preview.value(QStringLiteral("valid")).toBool(),
                 qPrintable(preview.value(QStringLiteral("error")).toString()));
        QCOMPARE(preview.value(QStringLiteral("presets")).toInt(), 2);
        QCOMPARE(preview.value(QStringLiteral("commands")).toInt(), 2);
        QCOMPARE(preview.value(QStringLiteral("disabledTools")).toInt(), 1);
        QCOMPARE(preview.value(QStringLiteral("conflictsTotal")).toInt(), 0);
        QCOMPARE(preview.value(QStringLiteral("permissionMode")).toString(),
                 QStringLiteral("plan_only"));
        QVERIFY(preview.value(QStringLiteral("warnings")).toStringList().join(
                    QLatin1Char(' ')).contains(QStringLiteral("未在本机注册")));

        QVERIFY2(transfer->importPack(path, QStringLiteral("rename")),
                 qPrintable(transfer->lastError()));

        auto* settings = SettingsService::instance();
        QCOMPARE(int(settings->promptPresets().size()), 2);
        QCOMPARE(int(settings->slashCommands().size()), 2);
        QCOMPARE(entryNamed(settings->promptPresets(), QStringLiteral("Reviewer"))
                     .value(QStringLiteral("prompt")).toString(),
                 QStringLiteral("Be strict."));
        // 指令词像 addSlashCommand 一样去掉前导 '/' 并转小写。
        QCOMPARE(settings->slashCommands().at(0).toMap()
                     .value(QStringLiteral("name")).toString(),
                 QStringLiteral("review2"));

        auto& registry = ToolRegistry::instance();
        QCOMPARE(registry.disabledToolNames(), QStringList{QStringLiteral("shell_exec")});
        QVERIFY(!registry.isToolEnabled(QStringLiteral("shell_exec")));
        QVERIFY(registry.isToolEnabled(QStringLiteral("write_file")));
        QCOMPARE(settings->defaultPermissionMode(), QStringLiteral("plan_only"));
    }

    void conflictPoliciesRespectLocalEntries()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QVariantMap root = packRoot();
        root.insert(QStringLiteral("presets"),
                    QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("Shared")},
                                           {QStringLiteral("prompt"), QStringLiteral("imported prompt")}},
                               QJsonObject{{QStringLiteral("name"), QStringLiteral("Fresh")},
                                           {QStringLiteral("prompt"), QStringLiteral("new")}}});
        root.insert(QStringLiteral("commands"),
                    QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("review")},
                                           {QStringLiteral("templ"), QStringLiteral("imported {text}")}},
                               QJsonObject{{QStringLiteral("name"), QStringLiteral("freshcmd")},
                                           {QStringLiteral("templ"), QStringLiteral("fresh {text}")}}});
        const QString path = writePack(dir, root);
        QVERIFY(!path.isEmpty());
        auto* transfer = PresetPackTransfer::instance();

        // 跳过：本机同名条目保持原样，新条目照常追加。
        seedLocalAssets();
        const QVariantMap preview = transfer->previewPack(path);
        QCOMPARE(preview.value(QStringLiteral("conflictsTotal")).toInt(), 2);
        QVERIFY2(transfer->importPack(path, QStringLiteral("skip")),
                 qPrintable(transfer->lastError()));
        auto* settings = SettingsService::instance();
        QCOMPARE(int(settings->promptPresets().size()), 2);
        QCOMPARE(entryNamed(settings->promptPresets(), QStringLiteral("Shared"))
                     .value(QStringLiteral("prompt")).toString(),
                 QStringLiteral("local prompt"));
        QVERIFY(!entryNamed(settings->promptPresets(), QStringLiteral("Fresh")).isEmpty());
        QCOMPARE(int(settings->slashCommands().size()), 2);
        QCOMPARE(entryNamed(settings->slashCommands(), QStringLiteral("review"))
                     .value(QStringLiteral("templ")).toString(),
                 QStringLiteral("local {text}"));

        // 覆盖：保留本机 id、替换内容。
        resetLocalState();
        seedLocalAssets();
        QVERIFY2(transfer->importPack(path, QStringLiteral("overwrite")),
                 qPrintable(transfer->lastError()));
        QCOMPARE(int(settings->promptPresets().size()), 2);
        const QVariantMap overwritten =
            entryNamed(settings->promptPresets(), QStringLiteral("Shared"));
        QCOMPARE(overwritten.value(QStringLiteral("id")).toString(),
                 QStringLiteral("local-preset"));
        QCOMPARE(overwritten.value(QStringLiteral("prompt")).toString(),
                 QStringLiteral("imported prompt"));
        QCOMPARE(entryNamed(settings->slashCommands(), QStringLiteral("review"))
                     .value(QStringLiteral("templ")).toString(),
                 QStringLiteral("imported {text}"));

        // 重命名：两边都保留，冲突条目追加后缀（指令用 -2，避免空格）。
        resetLocalState();
        seedLocalAssets();
        QVERIFY2(transfer->importPack(path, QStringLiteral("rename")),
                 qPrintable(transfer->lastError()));
        QCOMPARE(int(settings->promptPresets().size()), 3);
        QVERIFY(!entryNamed(settings->promptPresets(),
                            QStringLiteral("Shared (2)")).isEmpty());
        QCOMPARE(entryNamed(settings->promptPresets(), QStringLiteral("Shared"))
                     .value(QStringLiteral("prompt")).toString(),
                 QStringLiteral("local prompt"));
        QCOMPARE(int(settings->slashCommands().size()), 3);
        QVERIFY(!entryNamed(settings->slashCommands(),
                            QStringLiteral("review-2")).isEmpty());
    }

    void rejectsMalformedOrUnknownPacks()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto* transfer = PresetPackTransfer::instance();
        auto* settings = SettingsService::instance();

        const QString broken = dir.filePath(QStringLiteral("broken.json"));
        QFile brokenFile(broken);
        QVERIFY(brokenFile.open(QIODevice::WriteOnly));
        brokenFile.write("{ not json");
        brokenFile.close();
        QVERIFY(!transfer->previewPack(broken).value(QStringLiteral("valid")).toBool());
        QVERIFY(!transfer->importPack(broken, QStringLiteral("rename")));
        QVERIFY(!transfer->lastError().isEmpty());

        QVariantMap root = packRoot();
        root.insert(QStringLiteral("format"), QStringLiteral("something-else"));
        QVERIFY(!transfer->previewPack(writePack(dir, root, QStringLiteral("format.json")))
                     .value(QStringLiteral("valid")).toBool());

        root = packRoot();
        root.insert(QStringLiteral("version"), 2);
        QVERIFY(!transfer->previewPack(writePack(dir, root, QStringLiteral("version.json")))
                     .value(QStringLiteral("valid")).toBool());

        root = packRoot();
        root.insert(QStringLiteral("presets"),
                    QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("Dup")},
                                           {QStringLiteral("prompt"), QStringLiteral("a")}},
                               QJsonObject{{QStringLiteral("name"), QStringLiteral("dup")},
                                           {QStringLiteral("prompt"), QStringLiteral("b")}}});
        QVERIFY(!transfer->previewPack(writePack(dir, root, QStringLiteral("dups.json")))
                     .value(QStringLiteral("valid")).toBool());

        root = packRoot();
        root.insert(QStringLiteral("permissionMode"), QStringLiteral("god_mode"));
        QVERIFY(!transfer->previewPack(writePack(dir, root, QStringLiteral("mode.json")))
                     .value(QStringLiteral("valid")).toBool());

        // 失败的导入不得留下半套数据。
        QCOMPARE(int(settings->promptPresets().size()), 0);
        QCOMPARE(int(settings->slashCommands().size()), 0);
        QVERIFY(ToolRegistry::instance().disabledToolNames().isEmpty());
        QCOMPARE(settings->defaultPermissionMode(), QStringLiteral("confirm"));
    }

    void exportIsCredentialFreeAndRoundTrips()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto* settings = SettingsService::instance();
        QVERIFY(settings->applyPromptAssets(
            {QVariantMap{{QStringLiteral("id"), QStringLiteral("p1")},
                         {QStringLiteral("name"), QStringLiteral("Export me")},
                         {QStringLiteral("prompt"), QStringLiteral("Prompt body")}}},
            {QVariantMap{{QStringLiteral("id"), QStringLiteral("c1")},
                         {QStringLiteral("name"), QStringLiteral("ship")},
                         {QStringLiteral("templ"), QStringLiteral("Ship {text}")},
                         {QStringLiteral("note"), QStringLiteral("note")}}}));
        QVERIFY(ToolRegistry::instance().setDisabledTools({QStringLiteral("write_file")}));
        settings->setDefaultPermissionMode(QStringLiteral("auto_edit"));

        auto* transfer = PresetPackTransfer::instance();
        const QString path = dir.filePath(QStringLiteral("export.json"));
        QVERIFY2(transfer->exportPack(path), qPrintable(transfer->lastError()));

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        file.close();
        QVERIFY(!bytes.contains("apiKey"));
        QVERIFY(!bytes.contains("modelProfiles"));
        QVERIFY(!bytes.contains("mcpServers"));
        QVERIFY(!bytes.contains("\"id\""));

        const QJsonObject root = QJsonDocument::fromJson(bytes).object();
        QCOMPARE(root.value(QStringLiteral("format")).toString(),
                 QStringLiteral("nexus-preset-pack"));
        QCOMPARE(root.value(QStringLiteral("version")).toInt(), 1);
        QCOMPARE(root.value(QStringLiteral("presets")).toArray().size(), 1);
        QCOMPARE(root.value(QStringLiteral("commands")).toArray().size(), 1);
        QCOMPARE(root.value(QStringLiteral("disabledTools")).toArray().size(), 1);
        QCOMPARE(root.value(QStringLiteral("permissionMode")).toString(),
                 QStringLiteral("auto_edit"));
        QVERIFY(!root.contains(QStringLiteral("profiles")));
        QVERIFY(!root.contains(QStringLiteral("settings")));

        // 清空后原样导回，三组资产与权限模式都回来。
        resetLocalState();
        QVERIFY2(transfer->importPack(path, QStringLiteral("rename")),
                 qPrintable(transfer->lastError()));
        QCOMPARE(int(settings->promptPresets().size()), 1);
        QCOMPARE(entryNamed(settings->promptPresets(), QStringLiteral("Export me"))
                     .value(QStringLiteral("prompt")).toString(),
                 QStringLiteral("Prompt body"));
        QCOMPARE(int(settings->slashCommands().size()), 1);
        QCOMPARE(ToolRegistry::instance().disabledToolNames(),
                 QStringList{QStringLiteral("write_file")});
        QCOMPARE(settings->defaultPermissionMode(), QStringLiteral("auto_edit"));
    }
};

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QTemporaryDir settingsDir;
    if (!settingsDir.isValid()) return 2;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settingsDir.path());
    QCoreApplication::setOrganizationName(QStringLiteral("NexusAIPresetPackTests"));
    QCoreApplication::setApplicationName(QStringLiteral("NexusAIPresetPackTests"));

    // 先写空列表：否则 SettingsService 首次构造会把示例资产种进去。
    {
        QSettings seeded(QSettings::IniFormat, QSettings::UserScope,
                         QStringLiteral("NexusAI"), QStringLiteral("NexusAI"));
        seeded.setValue(QStringLiteral("schemaVersion"), SettingsSchema::kSchemaVersion);
        seeded.setValue(QStringLiteral("promptPresets"), QStringLiteral("[]"));
        seeded.setValue(QStringLiteral("slashCommands"), QStringLiteral("[]"));
        seeded.setValue(QStringLiteral("tools/disabled"), QStringList{});
        seeded.setValue(QStringLiteral("agent/defaultPermissionMode"),
                        QStringLiteral("confirm"));
        seeded.sync();
    }

    TestPresetPack tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_preset_pack.moc"
