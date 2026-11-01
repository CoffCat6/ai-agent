// docs/31 N1：首次启动向导的触发判定回归。
//
// 只覆盖「要不要弹」与「已看过」标记这一层（Onboarding），不加载 QML：
// 触发条件同时看模型配置表与偏好键，任何一侧漏判都会让老用户每次启动
// 都被向导拦截，或让新用户永远看不到它。三步交互在
// test_settings_page_qml 里用真实 QML 覆盖。
#include <QtTest>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>

#include "core/utils/SettingsSchema.h"
#include "viewmodels/Onboarding.h"
#include "viewmodels/PreferenceRegistry.h"
#include "viewmodels/PreferenceStore.h"
#include "viewmodels/SettingsBackup.h"
#include "viewmodels/SettingsCatalog.h"
#include "viewmodels/SettingsService.h"
#include "TestSettingsIsolation.h"

// 测试进程的 QSettings 重定向到临时 INI（见 TestSettingsIsolation.h），
// 防止测试改写用户真实设置。
namespace {
const NexusAI::TestSupport::SettingsIsolation kSettingsIsolation;
} // namespace

using namespace NexusAI;

namespace {

const QString kPrefKey = QStringLiteral("general.onboarded");

void removeAllProfiles() {
    auto* settings = SettingsService::instance();
    while (!settings->profiles().isEmpty())
        settings->removeProfile(settings->profiles().first().toMap()
                                    .value(QStringLiteral("id")).toString());
}

} // namespace

class TestOnboarding : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(m_settingsDir.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_settingsDir.path());
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("NexusAI"));
        QCoreApplication::setApplicationName(
            QStringLiteral("NexusAIOnboardingTests_%1")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    }

    // 每个用例从「全新安装」开始：没有配置、没有标记。
    void init() {
        removeAllProfiles();
        QVERIFY(PreferenceStore::instance()->reset(kPrefKey));
        QVERIFY(!Onboarding::instance()->isComplete());
    }

    void freshInstallShowsWizard() {
        QVERIFY(SettingsService::instance()->profiles().isEmpty());
        QVERIFY(Onboarding::instance()->shouldShow());
    }

    // 完成与跳过走同一个入口：写标记后不再触发，且重复调用不再写盘。
    void completeMarksOnboardedOnce() {
        QSignalSpy saved(PreferenceStore::instance(), &PreferenceStore::saved);
        QVERIFY(Onboarding::instance()->complete());
        QVERIFY(Onboarding::instance()->isComplete());
        QVERIFY(!Onboarding::instance()->shouldShow());
        QCOMPARE(saved.count(), 1);

        QVERIFY(Onboarding::instance()->complete());
        QCOMPARE(saved.count(), 1);
    }

    // 已有模型配置的用户不触发——标记可能因旧版本/导入缺失，配置本身就是
    // "用过了"的证据。
    void existingProfilesSuppressWizard() {
        const QString id = SettingsService::instance()->addProfile(QStringLiteral("openai"));
        QVERIFY(!id.isEmpty());
        QVERIFY(!Onboarding::instance()->isComplete());
        QVERIFY(!Onboarding::instance()->shouldShow());
    }

    // 标记是「隐藏偏好」：不进导航/搜索，也不能被「恢复全部设置」清掉
    // ——否则复位设置会把向导重新弹出来。
    void preferenceIsRegisteredButHidden() {
        auto* registry = PreferenceRegistry::instance();
        QVERIFY(registry->hasPreference(kPrefKey));
        QCOMPARE(registry->defaultValue(kPrefKey).toBool(), false);
        QCOMPARE(registry->meta(kPrefKey)->categoryId, QString());

        auto* catalog = SettingsCatalog::instance();
        QVERIFY(catalog->hasEntry(kPrefKey));
        const QVariantMap entry = catalog->entry(kPrefKey);
        QCOMPARE(entry.value(QStringLiteral("hidden")).toBool(), true);
        QCOMPARE(entry.value(QStringLiteral("resetMode")).toString(),
                 QStringLiteral("none"));
    }

    // 首次启动标记是机器本地状态，不随设置备份迁移（否则新机器导入备份
    // 后不会提示配置，或旧机器残留标记挡住新用户）。
    void markerStaysOutOfBackups() {
        QVERIFY(PreferenceStore::instance()->save(kPrefKey, true));
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("backup.json"));
        QVERIFY2(SettingsBackup::instance()->exportSettings(path),
                 qPrintable(SettingsBackup::instance()->lastError()));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        const QJsonObject settings = root.value(QStringLiteral("settings")).toObject();
        QVERIFY(!settings.isEmpty());
        QVERIFY(!settings.contains(QStringLiteral("prefs/v1/general.onboarded")));
    }

private:
    QTemporaryDir m_settingsDir;
};

QTEST_MAIN(TestOnboarding)
#include "test_onboarding.moc"
