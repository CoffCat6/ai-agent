// docs/31 S1（O1/O2）：设置值快照、行级复位与搜索权重的回归。
//
// 值层是"行级圆点 / 导航徽标 / 搜索当前值摘要"三处显示的共同数据源，
// 所以这里锁四件事：
//   1. 偏好与 M1 键都能给出 current / default / modified / display；
//   2. 复位只走有默认值且非密钥的条目（红线 1：密钥与代理凭据永不清除）；
//   3. 搜索按标题 > 关键词 > 键名 > 分类名加权，标题命中带 <b> 高亮；
//   4. 搜索摘要对密钥只输出"已配置/未配置"，绝不带出任何明文。
//
// 红线 7：所有落盘都指向临时目录，禁止写真实用户配置。
#include <QtTest>

#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "core/utils/SettingsRepository.h"
#include "core/utils/SettingsSchema.h"
#include "core/tools/WebSearchBackend.h"
#include "viewmodels/EditBuffer.h"
#include "viewmodels/PreferenceStore.h"
#include "viewmodels/SettingsCatalog.h"
#include "viewmodels/SettingsService.h"
#include "viewmodels/SettingsValueFormat.h"
#include "TestSettingsIsolation.h"

// 测试进程的 QSettings 重定向到临时 INI（见 TestSettingsIsolation.h），
// 防止测试改写用户真实设置。
namespace {
const NexusAI::TestSupport::SettingsIsolation kSettingsIsolation;
} // namespace

using namespace NexusAI;

class TestSettingsValues : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(m_settingsDir.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_settingsDir.path());
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("NexusAI"));
        QCoreApplication::setApplicationName(QStringLiteral("NexusAI"));

        // 基线 schema 版本：否则第一次构造 SettingsService 会跑迁移。
        QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                           QStringLiteral("NexusAI"), QStringLiteral("NexusAI"));
        settings.setValue(QStringLiteral("schemaVersion"),
                          SettingsSchema::kSchemaVersion);
        settings.sync();
    }

    // 值快照：偏好未落盘时 modified=false、current 回退默认值；布尔/整型/
    // 枚举/列表/字符串各有稳定摘要。
    void snapshotsReportCurrentAndDefaults() {
        const SettingValueSnapshot autoStart =
            SettingsValue::inspect(QStringLiteral("general.autoStart"));
        QVERIFY(autoStart.known);
        QVERIFY(!autoStart.secret);
        QVERIFY(!autoStart.stored);
        QVERIFY(!autoStart.modified);
        QCOMPARE(autoStart.current.toBool(), false);
        QCOMPARE(autoStart.defaultValue.toBool(), false);
        QCOMPARE(SettingsValue::displayOf(autoStart, QStringLiteral("general.autoStart")),
                 QStringLiteral("关"));

        auto* catalog = SettingsCatalog::instance();
        const QVariantMap fresh = catalog->describe(QStringLiteral("general.autoStart"));
        QCOMPARE(fresh.value(QStringLiteral("known")).toBool(), true);
        QCOMPARE(fresh.value(QStringLiteral("secret")).toBool(), false);
        QCOMPARE(fresh.value(QStringLiteral("modified")).toBool(), false);
        QCOMPARE(fresh.value(QStringLiteral("display")).toString(), QStringLiteral("关"));
        QCOMPARE(fresh.value(QStringLiteral("category")).toString(),
                 QStringLiteral("general"));
        QVERIFY(fresh.value(QStringLiteral("revertable")).toBool());

        // 枚举标签、整型、布尔直出。
        QCOMPARE(catalog->describe(QStringLiteral("ui.themeMode"))
                     .value(QStringLiteral("display")).toString(),
                 QStringLiteral("跟随系统"));
        QCOMPARE(catalog->describe(QStringLiteral("ui.baseFontSize"))
                     .value(QStringLiteral("display")).toString(),
                 QStringLiteral("14"));
        QCOMPARE(catalog->describe(QStringLiteral("notice.enabled"))
                     .value(QStringLiteral("display")).toString(),
                 QStringLiteral("开"));

        // 未知键不臆造值。
        const SettingValueSnapshot unknown = SettingsValue::inspect(QStringLiteral("no-such/key"));
        QVERIFY(!unknown.known);
        QVERIFY(SettingsValue::displayOf(unknown, QStringLiteral("no-such/key")).isEmpty());
        QVERIFY(catalog->describe(QStringLiteral("no-such/key"))
                    .value(QStringLiteral("known")).toBool() == false);

        // 默认值尚未探测到的行（字体族）没有可靠的复原目标。
        QVERIFY(!catalog->describe(QStringLiteral("ui.fontSans"))
                     .value(QStringLiteral("revertable")).toBool());
    }

    // 行级复位：改值 → modified/徽标计数变化 → 复位回默认值。
    void rowLevelResetRestoresPreferenceDefault() {
        auto* catalog = SettingsCatalog::instance();
        auto* buffer = EditBuffer::instance();

        QSignalSpy revisionSpy(catalog, &SettingsCatalog::revisionChanged);
        buffer->stage(QStringLiteral("general.autoStart"), true);

        const QVariantMap changed = catalog->describe(QStringLiteral("general.autoStart"));
        QCOMPARE(changed.value(QStringLiteral("modified")).toBool(), true);
        QCOMPARE(changed.value(QStringLiteral("display")).toString(), QStringLiteral("开"));
        QVERIFY(catalog->isModified(QStringLiteral("general.autoStart")));
        QVERIFY(catalog->modifiedCount(QStringLiteral("general")) >= 1);
        QVERIFY(revisionSpy.count() >= 1);

        QVERIFY(catalog->resetValue(QStringLiteral("general.autoStart")));
        QCOMPARE(catalog->describe(QStringLiteral("general.autoStart"))
                     .value(QStringLiteral("modified")).toBool(),
                 false);
        QCOMPARE(catalog->describe(QStringLiteral("general.autoStart"))
                     .value(QStringLiteral("display")).toString(),
                 QStringLiteral("关"));
        QCOMPARE(PreferenceStore::instance()->load(QStringLiteral("general.autoStart")).toBool(),
                 false);
        QCOMPARE(catalog->modifiedCount(QStringLiteral("general")), 0);
    }

    // M1 键（SettingsService 属性）同样可复位，且复位后 getter 与快照一致。
    void pageKeyResetGoesThroughSettingsService() {
        auto* catalog = SettingsCatalog::instance();
        auto* service = SettingsService::instance();

        const SettingsKeyDef* definition =
            SettingsSchema::definition(QStringLiteral("agent/maxSteps"));
        QVERIFY(definition);
        const int defaultValue = definition->defaultValue.toInt();
        const int custom = defaultValue + 1;

        service->setMaxSteps(custom);
        QCOMPARE(service->maxSteps(), custom);
        const QVariantMap changed = catalog->describe(QStringLiteral("agent/maxSteps"));
        QCOMPARE(changed.value(QStringLiteral("known")).toBool(), true);
        QCOMPARE(changed.value(QStringLiteral("modified")).toBool(), true);
        QCOMPARE(changed.value(QStringLiteral("revertable")).toBool(), true);
        QCOMPARE(changed.value(QStringLiteral("display")).toString(),
                 QString::number(custom));

        QVERIFY(catalog->resetValue(QStringLiteral("agent/maxSteps")));
        QCOMPARE(service->maxSteps(), defaultValue);
        QCOMPARE(catalog->describe(QStringLiteral("agent/maxSteps"))
                     .value(QStringLiteral("modified")).toBool(),
                 false);
        QCOMPARE(catalog->describe(QStringLiteral("agent/maxSteps"))
                     .value(QStringLiteral("display")).toString(),
                 QString::number(defaultValue));
    }

    // 红线 1：密钥/代理凭据只回答"已配置"，不参与复位，也不泄露明文。
    void secretsAreMaskedAndNeverReset() {
        auto* catalog = SettingsCatalog::instance();
        auto* service = SettingsService::instance();

        service->setWebSearchProvider(QStringLiteral("zhipu"));
        service->setWebSearchApiKey(QStringLiteral("sk-test-canary-value"));

        const QVariantMap secret =
            catalog->describe(QStringLiteral("webSearch/zhipuApiKey"));
        QCOMPARE(secret.value(QStringLiteral("known")).toBool(), true);
        QCOMPARE(secret.value(QStringLiteral("secret")).toBool(), true);
        QCOMPARE(secret.value(QStringLiteral("configured")).toBool(), true);
        QCOMPARE(secret.value(QStringLiteral("display")).toString(),
                 QStringLiteral("已配置"));
        QVERIFY(!secret.value(QStringLiteral("display")).toString()
                     .contains(QStringLiteral("canary")));
        QCOMPARE(secret.value(QStringLiteral("revertable")).toBool(), false);
        QVERIFY(!catalog->resetValue(QStringLiteral("webSearch/zhipuApiKey")));
        QCOMPARE(service->webSearchApiKeyValue(),
                 QStringLiteral("sk-test-canary-value"));

        // 代理用户名/密码没有独立存储：整体在加密 blob 里，同样不可复位。
        for (const QString& key : {QStringLiteral("network/proxyUsername"),
                                   QStringLiteral("network/proxyPassword")}) {
            const QVariantMap anchor = catalog->describe(key);
            QCOMPARE(anchor.value(QStringLiteral("secret")).toBool(), true);
            QCOMPARE(anchor.value(QStringLiteral("revertable")).toBool(), false);
            QVERIFY(!catalog->resetValue(key));
            const QString display = anchor.value(QStringLiteral("display")).toString();
            QVERIFY2(display == QStringLiteral("已配置")
                         || display == QStringLiteral("未配置"),
                     qPrintable(display));
        }

        service->setWebSearchApiKey(QString());
        service->setWebSearchProvider(WebSearchProviderRegistry::defaultProviderKey());
        QCOMPARE(catalog->describe(QStringLiteral("webSearch/zhipuApiKey"))
                     .value(QStringLiteral("display")).toString(),
                 QStringLiteral("未配置"));
    }

    // S1 遗留：安全 / 用量 / 工具页只在自绘控件上写 SettingsService，没有
    // requestResetDefaults。它们此前在目录里登记为不可复位——行内只有圆点、
    // 分类徽标不计数、底栏「恢复本页默认值」不出现，而「恢复全部设置」的
    // 确认文案却承诺这些分类会复位。现在统一走 setValueForSetting。
    void serviceOnlyPagesAreRowResettable() {
        auto* catalog = SettingsCatalog::instance();
        auto* service = SettingsService::instance();

        for (const char* category : {"security", "usage", "tools"})
            QCOMPARE(catalog->resetMode(QString::fromLatin1(category)),
                     QStringLiteral("page"));

        service->setLocalOnlyMode(true);
        const QVariantMap local = catalog->describe(QStringLiteral("security/localOnlyMode"));
        QCOMPARE(local.value(QStringLiteral("revertable")).toBool(), true);
        QCOMPARE(local.value(QStringLiteral("modified")).toBool(), true);
        QVERIFY(catalog->modifiedCount(QStringLiteral("security")) >= 1);
        QVERIFY(catalog->resetValue(QStringLiteral("security/localOnlyMode")));
        QVERIFY(!service->localOnlyMode());
        QCOMPARE(catalog->modifiedCount(QStringLiteral("security")), 0);

        service->setDailyTokenQuota(123456);
        QCOMPARE(catalog->describe(QStringLiteral("dailyTokenQuota"))
                     .value(QStringLiteral("display")).toString(),
                 QStringLiteral("123456"));
        QVERIFY(catalog->resetValue(QStringLiteral("dailyTokenQuota")));
        QCOMPARE(service->dailyTokenQuota(), 0);

        service->setDefaultPermissionMode(QStringLiteral("full_access"));
        QVERIFY(catalog->modifiedCount(QStringLiteral("tools")) >= 1);
        QVERIFY(catalog->resetValue(QStringLiteral("agent/defaultPermissionMode")));
        QCOMPARE(service->defaultPermissionMode(), QStringLiteral("confirm"));

        // Any 类型的列表值（黑名单）也必须能判定"已修改"，否则永远显示未改。
        const QVariantList deny{QStringLiteral("/canary-denied")};
        service->setReadDenyList(deny);
        QCOMPARE(catalog->describe(QStringLiteral("security/readDenyList"))
                     .value(QStringLiteral("modified")).toBool(),
                 true);
        QVERIFY(catalog->resetValue(QStringLiteral("security/readDenyList")));
        QVERIFY(service->readDenyList().isEmpty());
    }

    // 没有复位通道的条目（整条 profile / 未知键）一律拒绝。
    void resetRefusesAssetsAndUnknownKeys() {
        auto* catalog = SettingsCatalog::instance();
        QVERIFY(!catalog->resetValue(QStringLiteral("modelProfiles")));
        QVERIFY(!catalog->resetValue(QStringLiteral("mcp/servers")));
        QVERIFY(!catalog->resetValue(QStringLiteral("no-such/key")));
        QVERIFY(!catalog->resetValue(QString()));
    }

    // 搜索权重：标题命中 100 > 关键词 60 > 键名 40 > 分类名 20，且标题高亮。
    // docs/31 S4.2 N6：定时备份间隔。任何入口（下拉框、导入的旧配置、手工
    // 改注册表）最终都要落到 0/1/6/24/168 五档之一——写盘的也是归一后的
    // 值，否则重新加载后下拉框会显示一个选不中的档位。
    void backupIntervalNormalizesBeforePersisting() {
        auto* service = SettingsService::instance();
        const QString key = QStringLiteral("memory/backupIntervalHours");
        QSignalSpy changedSpy(service, &SettingsService::backupIntervalHoursChanged);

        QCOMPARE(service->backupIntervalHours(), 0);

        service->setBackupIntervalHours(37);
        QCOMPARE(service->backupIntervalHours(), 24);
        QCOMPARE(SettingsRepository::value(key).toInt(), 24);

        service->setBackupIntervalHours(-5);
        QCOMPARE(service->backupIntervalHours(), 0);
        QCOMPARE(SettingsRepository::value(key).toInt(), 0);

        service->setBackupIntervalHours(9999);
        QCOMPARE(service->backupIntervalHours(), 168);
        QCOMPARE(SettingsRepository::value(key).toInt(), 168);

        service->setBackupIntervalHours(6);
        QCOMPARE(service->backupIntervalHours(), 6);
        QCOMPARE(SettingsRepository::value(key).toInt(), 6);

        // 相同值不重复发信号（否则每次页面刷新都会重启备份定时器）。
        changedSpy.clear();
        service->setBackupIntervalHours(6);
        QCOMPARE(changedSpy.count(), 0);

        // 页面复位回到「关闭」。
        service->resetBackupToDefaults();
        QCOMPARE(service->backupIntervalHours(), 0);
        QCOMPARE(SettingsRepository::value(key).toInt(), 0);
    }

    // docs/31 S4.3 N3：撤销最近一次写穿——有旧落盘值就写回旧值，并且只
    // 回写一次（不链式，第二次撤销必须失败）。
    void undoRestoresPreviousStoredValue() {
        auto* store = PreferenceStore::instance();
        auto* buffer = EditBuffer::instance();
        const QString key = QStringLiteral("ui.baseFontSize");
        QVERIFY(store->save(key, 15));
        buffer->stage(key, 18);
        QCOMPARE(store->load(key).toInt(), 18);
        QVERIFY(buffer->canUndo());
        QCOMPARE(buffer->undoKey(), key);

        QVERIFY(buffer->undoLast());
        QCOMPARE(store->load(key).toInt(), 15);
        QVERIFY(!buffer->canUndo());
        QVERIFY(buffer->undoKey().isEmpty());
        QVERIFY(!buffer->undoLast());
        QCOMPARE(store->load(key).toInt(), 15);

        QVERIFY(store->reset(key));
    }

    // 之前从未落过盘：撤销应删除物理键回到默认值，而不是把默认值写成
    // "已修改"（否则行级徽标会永远亮着）。
    void undoRemovesKeyThatWasNotStored() {
        auto* store = PreferenceStore::instance();
        auto* buffer = EditBuffer::instance();
        const QString key = QStringLiteral("ui.baseFontSize");
        QVERIFY(store->reset(key));
        const QVariant defaultValue = store->load(key);

        buffer->stage(key, 16);
        QVERIFY(store->hasStoredValue(key));
        QVERIFY(buffer->undoLast());
        QVERIFY(!store->hasStoredValue(key));
        QCOMPARE(store->load(key), defaultValue);
        QVERIFY(!buffer->canUndo());
    }

    // 复位入口清空撤销记录：一次复位覆盖此前所有改动，再撤销只会回到
    // 半复位的中间态。
    void resetClearsUndoRecord() {
        auto* store = PreferenceStore::instance();
        auto* buffer = EditBuffer::instance();
        const QString key = QStringLiteral("ui.baseFontSize");

        buffer->stage(key, 16);
        QVERIFY(buffer->canUndo());
        buffer->resetToDefault({key});
        QVERIFY(!buffer->canUndo());
        QVERIFY(!buffer->undoLast());
        QCOMPARE(store->load(key).toInt(), 14);
        QVERIFY(store->reset(key));
    }

    // 密钥类永远不提供撤销：它们不是偏好键，stage 会在校验阶段被拒，因此
    // 连撤销点都建立不起来；undoLast 对没有记录的调用也必须返回 false。
    void undoRejectsSecretOrUnknownKeys() {
        auto* store = PreferenceStore::instance();
        auto* buffer = EditBuffer::instance();
        QVERIFY(SettingsCatalog::instance()
                    ->describe(QStringLiteral("webSearch/apiKey"))
                    .value(QStringLiteral("secret")).toBool());

        buffer->stage(QStringLiteral("webSearch/apiKey"),
                      QStringLiteral("sk-undo-canary"));
        QVERIFY(!buffer->canUndo());
        QVERIFY(!buffer->undoLast());
        QVERIFY(!SettingsRepository::contains(
            QStringLiteral("prefs/v1/webSearch/apiKey")));
        QVERIFY(!store->hasStoredValue(QStringLiteral("webSearch/apiKey")));
        buffer->rollback();

        buffer->stage(QStringLiteral("not.registered"), true);
        QVERIFY(!buffer->canUndo());
        QVERIFY(!buffer->undoLast());
        buffer->rollback();
    }

    void searchRanksAndHighlightsMatches() {
        auto* catalog = SettingsCatalog::instance();

        const QVariantList titleHits = catalog->search(QStringLiteral("开机自启"));
        QVERIFY(!titleHits.isEmpty());
        const QVariantMap first = titleHits.first().toMap();
        QCOMPARE(first.value(QStringLiteral("id")).toString(),
                 QStringLiteral("general.autoStart"));
        QCOMPARE(first.value(QStringLiteral("score")).toInt(), 100);
        QVERIFY(first.value(QStringLiteral("titleHtml")).toString()
                    .contains(QStringLiteral("<b>")));
        QVERIFY(first.value(QStringLiteral("highlightStart")).toInt() >= 0);
        QVERIFY(first.value(QStringLiteral("highlightLength")).toInt() > 0);
        QCOMPARE(first.value(QStringLiteral("currentDisplay")).toString(),
                 QStringLiteral("关"));

        // 结果按分数单调不增，且同分排序稳定。
        int previous = 101;
        for (const QVariant& value : titleHits) {
            const int score = value.toMap().value(QStringLiteral("score")).toInt();
            QVERIFY(score <= previous);
            previous = score;
        }
        QCOMPARE(catalog->search(QStringLiteral("  开机自启  ")).first().toMap()
                     .value(QStringLiteral("id")).toString(),
                 QStringLiteral("general.autoStart"));

        // 键名命中（autoStart 不在翻译标题里）。
        const QVariantList keyHits = catalog->search(QStringLiteral("autostart"));
        QVERIFY(!keyHits.isEmpty());
        QCOMPARE(keyHits.first().toMap().value(QStringLiteral("id")).toString(),
                 QStringLiteral("general.autoStart"));
        QVERIFY(keyHits.first().toMap().value(QStringLiteral("score")).toInt() >= 40);

        // 只命中分类名的条目拿 20 分——这证明分类名是独立的一档权重。
        const QVariantList categoryHits = catalog->search(QStringLiteral("界面与外观"));
        QVERIFY(!categoryHits.isEmpty());
        bool foundCategoryOnly = false;
        for (const QVariant& value : categoryHits) {
            const QVariantMap entry = value.toMap();
            QCOMPARE(entry.value(QStringLiteral("category")).toString(),
                     QStringLiteral("interface"));
            if (entry.value(QStringLiteral("score")).toInt() == 20)
                foundCategoryOnly = true;
        }
        QVERIFY(foundCategoryOnly);

        QVERIFY(catalog->search(QString()).isEmpty());
        QVERIFY(catalog->search(QStringLiteral("没有这个设置-xyz")).isEmpty());
    }

    // 搜索结果摘要同样走值层：密钥只显示配置状态，不泄露明文。
    void searchSummariesMaskSecrets() {
        auto* catalog = SettingsCatalog::instance();
        auto* service = SettingsService::instance();

        service->setWebSearchProvider(QStringLiteral("zhipu"));
        service->setWebSearchApiKey(QStringLiteral("sk-search-canary"));

        bool foundSummary = false;
        for (const QVariant& value : catalog->search(QStringLiteral("密钥"))) {
            const QVariantMap entry = value.toMap();
            const QString id = entry.value(QStringLiteral("id")).toString();
            const QString display = entry.value(QStringLiteral("currentDisplay")).toString();
            QVERIFY2(!display.contains(QStringLiteral("sk-search-canary")),
                     qPrintable(QStringLiteral("%1 leaked the key: %2").arg(id, display)));
            // 隐藏的 provider 密钥桶永远不进搜索结果。
            QVERIFY(id != QStringLiteral("webSearch/zhipuApiKey"));
            if (id == QStringLiteral("webSearch/apiKey")) {
                foundSummary = true;
                QCOMPARE(entry.value(QStringLiteral("secret")).toBool(), true);
                QCOMPARE(display, QStringLiteral("已配置"));
            }
        }
        QVERIFY(foundSummary);

        service->setWebSearchApiKey(QString());
        service->setWebSearchProvider(WebSearchProviderRegistry::defaultProviderKey());
    }

private:
    QTemporaryDir m_settingsDir;
};

QTEST_GUILESS_MAIN(TestSettingsValues)
#include "test_settings_values.moc"
