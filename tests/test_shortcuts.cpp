// docs/31 N2：快捷键映射的数据层回归。
//
// 这里只验证 ShortcutMap / 设置键登记的行为，不加载 QML：动作表默认值、
// 冲突拒绝、安全性校验、落盘与恢复、对外部写入（EditBuffer 复位）的响应。
// 页面级录制流程在 test_settings_page_qml 里用真实 QML 覆盖。
#include <QtTest>

#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>

#include "viewmodels/EditBuffer.h"
#include "viewmodels/PreferenceRegistry.h"
#include "viewmodels/PreferenceStore.h"
#include "viewmodels/SettingsCatalog.h"
#include "viewmodels/ShortcutMap.h"

using namespace NexusAI;

namespace {

const QString kKey = QStringLiteral("ui.shortcuts");

} // namespace

class TestShortcuts : public QObject {
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
            QStringLiteral("NexusAIShortcutTests_%1")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    }

    // 每个用例从“全部默认”开始。save 会触发 ShortcutMap 的 saved 监听，
    // 因此即使上一个用例留下了覆盖项也会被清回。
    void init() {
        QVERIFY(PreferenceStore::instance()->save(kKey, QStringLiteral("{}")));
        QCOMPARE(ShortcutMap::instance()->modifiedCount(), 0);
    }

    // 动作表是 main.qml / 速查表 / 设置页的唯一事实源：默认序列必须与
    // 迁移前的硬编码值一致，否则升级后用户的肌肉记忆会突然失效。
    void defaultsComeFromActionTable() {
        auto* map = ShortcutMap::instance();
        const QVariantMap bindings = map->bindings();
        QCOMPARE(bindings.size(), 12);

        QCOMPARE(bindings.value(QStringLiteral("settings.open")).toString(),
                 QStringLiteral("Ctrl+,"));
        QCOMPARE(bindings.value(QStringLiteral("settings.search")).toString(),
                 QStringLiteral("Ctrl+F"));
        QCOMPARE(bindings.value(QStringLiteral("audit.open")).toString(),
                 QStringLiteral("Ctrl+L"));
        QCOMPARE(bindings.value(QStringLiteral("panel.toggle")).toString(),
                 QStringLiteral("Ctrl+B"));
        QCOMPARE(bindings.value(QStringLiteral("review.open")).toString(),
                 QStringLiteral("Ctrl+R"));
        QCOMPARE(bindings.value(QStringLiteral("todo.open")).toString(),
                 QStringLiteral("Ctrl+T"));
        QCOMPARE(bindings.value(QStringLiteral("sidebar.toggle")).toString(),
                 QStringLiteral("Ctrl+["));
        QCOMPARE(bindings.value(QStringLiteral("conversation.close")).toString(),
                 QStringLiteral("Ctrl+W"));
        QCOMPARE(bindings.value(QStringLiteral("conversation.next")).toString(),
                 QStringLiteral("Ctrl+Tab"));
        QCOMPARE(bindings.value(QStringLiteral("conversation.prev")).toString(),
                 QStringLiteral("Ctrl+Shift+Tab"));
        QCOMPARE(bindings.value(QStringLiteral("theme.toggle")).toString(),
                 QStringLiteral("Ctrl+D"));
        QCOMPARE(bindings.value(QStringLiteral("shortcuts.open")).toString(),
                 QStringLiteral("Ctrl+/"));

        const QVariantList actions = map->actions();
        QCOMPARE(actions.size(), 12);
        for (const QVariant& value : actions) {
            const QVariantMap action = value.toMap();
            QVERIFY(!action.value(QStringLiteral("id")).toString().isEmpty());
            QVERIFY(!action.value(QStringLiteral("title")).toString().isEmpty());
            QCOMPARE(action.value(QStringLiteral("modified")).toBool(), false);
            QCOMPARE(action.value(QStringLiteral("sequence")).toString(),
                     action.value(QStringLiteral("defaultSequence")).toString());
        }
    }

    void assignPersistsAndClearingRestoresDefault() {
        auto* map = ShortcutMap::instance();
        QSignalSpy spy(map, &ShortcutMap::changed);

        const QVariantMap result = map->assign(QStringLiteral("panel.toggle"),
                                               QStringLiteral("Ctrl+Shift+B"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
                 qPrintable(result.value(QStringLiteral("error")).toString()));
        QCOMPARE(result.value(QStringLiteral("sequence")).toString(),
                 QStringLiteral("Ctrl+Shift+B"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(map->bindings().value(QStringLiteral("panel.toggle")).toString(),
                 QStringLiteral("Ctrl+Shift+B"));
        QCOMPARE(map->modifiedCount(), 1);

        // 落盘的是 JSON 对象，而不是某个平台相关的二进制格式。
        const QString stored = PreferenceStore::instance()->load(kKey).toString();
        QVERIFY2(stored.contains(QStringLiteral("\"panel.toggle\":\"Ctrl+Shift+B\"")),
                 qPrintable(stored));

        // 动作列表里的 modified 标记驱动页面上的「恢复」按钮。
        bool marked = false;
        for (const QVariant& value : map->actions()) {
            const QVariantMap action = value.toMap();
            if (action.value(QStringLiteral("id")).toString()
                == QStringLiteral("panel.toggle"))
                marked = action.value(QStringLiteral("modified")).toBool();
        }
        QVERIFY(marked);

        // 改回默认值 = 删除覆盖项（存储键随之为空），而不是存一份等值 JSON。
        const QVariantMap back = map->assign(QStringLiteral("panel.toggle"),
                                             QStringLiteral("Ctrl+B"));
        QVERIFY(back.value(QStringLiteral("ok")).toBool());
        QCOMPARE(map->modifiedCount(), 0);
        QCOMPARE(map->bindings().value(QStringLiteral("panel.toggle")).toString(),
                 QStringLiteral("Ctrl+B"));
        QVERIFY(!PreferenceStore::instance()->hasStoredValue(kKey));
    }

    void conflictAndUnsafeKeysAreRejected() {
        auto* map = ShortcutMap::instance();

        const QVariantMap conflict = map->assign(QStringLiteral("panel.toggle"),
                                                 QStringLiteral("Ctrl+L"));
        QCOMPARE(conflict.value(QStringLiteral("ok")).toBool(), false);
        QCOMPARE(conflict.value(QStringLiteral("conflict")).toString(),
                 QStringLiteral("audit.open"));
        QVERIFY(!conflict.value(QStringLiteral("error")).toString().isEmpty());
        QCOMPARE(map->bindings().value(QStringLiteral("panel.toggle")).toString(),
                 QStringLiteral("Ctrl+B"));
        QCOMPARE(map->modifiedCount(), 0);

        // 裸字母会被全局 Shortcut 截走，聊天输入框就再也打不出来。
        for (const QString& unsafe : {QStringLiteral("B"), QStringLiteral("Shift+B"),
                                      QStringLiteral("1")}) {
            const QVariantMap rejected =
                map->assign(QStringLiteral("panel.toggle"), unsafe);
            QVERIFY2(!rejected.value(QStringLiteral("ok")).toBool(),
                     qPrintable(unsafe));
        }

        const QVariantMap invalid = map->assign(QStringLiteral("panel.toggle"),
                                                QStringLiteral("NotAKey"));
        QCOMPARE(invalid.value(QStringLiteral("ok")).toBool(), false);

        const QVariantMap unknown = map->assign(QStringLiteral("no.such.action"),
                                                QStringLiteral("Ctrl+U"));
        QCOMPARE(unknown.value(QStringLiteral("ok")).toBool(), false);

        // 功能键不需要修饰键（不会影响文字输入）。
        const QVariantMap functionKey = map->assign(QStringLiteral("theme.toggle"),
                                                    QStringLiteral("F5"));
        QVERIFY2(functionKey.value(QStringLiteral("ok")).toBool(),
                 qPrintable(functionKey.value(QStringLiteral("error")).toString()));
        QCOMPARE(map->bindings().value(QStringLiteral("theme.toggle")).toString(),
                 QStringLiteral("F5"));
    }

    void resetActionAndResetAll() {
        auto* map = ShortcutMap::instance();
        QVERIFY(map->assign(QStringLiteral("panel.toggle"),
                            QStringLiteral("Ctrl+Shift+B"))
                    .value(QStringLiteral("ok")).toBool());
        QVERIFY(map->assign(QStringLiteral("todo.open"),
                            QStringLiteral("Ctrl+Shift+K"))
                    .value(QStringLiteral("ok")).toBool());
        QCOMPARE(map->modifiedCount(), 2);

        QVERIFY(map->resetAction(QStringLiteral("panel.toggle")));
        QCOMPARE(map->bindings().value(QStringLiteral("panel.toggle")).toString(),
                 QStringLiteral("Ctrl+B"));
        QCOMPARE(map->modifiedCount(), 1);
        QVERIFY(!map->resetAction(QStringLiteral("panel.toggle")));

        QVERIFY(map->resetAll());
        QCOMPARE(map->modifiedCount(), 0);
        QCOMPARE(map->bindings().value(QStringLiteral("todo.open")).toString(),
                 QStringLiteral("Ctrl+T"));
        QVERIFY(!PreferenceStore::instance()->hasStoredValue(kKey));
    }

    // 外部写入（设置页底部复位 / 全局复位 / 直接改存储）必须回到映射里。
    void externalResetThroughEditBufferIsObserved() {
        auto* map = ShortcutMap::instance();
        QVERIFY(map->assign(QStringLiteral("panel.toggle"),
                            QStringLiteral("Ctrl+Shift+B"))
                    .value(QStringLiteral("ok")).toBool());

        // 与设置页“恢复本页默认值”走同一条路径：EditBuffer 写穿到存储。
        EditBuffer::instance()->stage(kKey, QStringLiteral("{}"));
        QCOMPARE(map->bindings().value(QStringLiteral("panel.toggle")).toString(),
                 QStringLiteral("Ctrl+B"));
        QCOMPARE(map->modifiedCount(), 0);
    }

    // 手改坏的 JSON 不能把全部快捷键拖下水；重复项按动作表顺序先到先得。
    void corruptOrDuplicateStoredValueFallsBackSafely() {
        auto* map = ShortcutMap::instance();

        QVERIFY(PreferenceStore::instance()->save(kKey, QStringLiteral("not-json")));
        QCOMPARE(map->bindings().value(QStringLiteral("panel.toggle")).toString(),
                 QStringLiteral("Ctrl+B"));
        QCOMPARE(map->modifiedCount(), 0);

        QVERIFY(PreferenceStore::instance()->save(
            kKey,
            QStringLiteral("{\"panel.toggle\":\"Ctrl+L\","
                           "\"todo.open\":\"Ctrl+Shift+T\"}")));
        // Ctrl+L 已被 audit.open 占用 → 丢弃该覆盖项，回到默认。
        QCOMPARE(map->bindings().value(QStringLiteral("panel.toggle")).toString(),
                 QStringLiteral("Ctrl+B"));
        QCOMPARE(map->bindings().value(QStringLiteral("todo.open")).toString(),
                 QStringLiteral("Ctrl+Shift+T"));
        QCOMPARE(map->modifiedCount(), 1);
    }

    void sequenceFromKeyBuildsPortableText() {
        auto* map = ShortcutMap::instance();
        QCOMPARE(map->sequenceFromKey(Qt::Key_B, Qt::ControlModifier),
                 QStringLiteral("Ctrl+B"));
        QCOMPARE(map->sequenceFromKey(Qt::Key_B,
                                      Qt::ControlModifier | Qt::ShiftModifier),
                 QStringLiteral("Ctrl+Shift+B"));
        QCOMPARE(map->sequenceFromKey(Qt::Key_F5, Qt::NoModifier),
                 QStringLiteral("F5"));
        // 纯修饰键不是组合键。
        QCOMPARE(map->sequenceFromKey(Qt::Key_Control, Qt::ControlModifier),
                 QString());
        QCOMPARE(map->sequenceFromKey(Qt::Key_Shift, Qt::ShiftModifier), QString());
        QCOMPARE(map->sequenceFromKey(0, Qt::NoModifier), QString());
    }

    // 新键三件套：schema 偏好、分类、目录条目必须同时到位，且目录显示
    // 人类可读的摘要而不是 JSON 原文。
    void preferenceCatalogAndCategoryAreRegistered() {
        auto* registry = PreferenceRegistry::instance();
        QVERIFY(registry->hasPreference(kKey));
        QCOMPARE(registry->defaultValue(kKey).toString(), QStringLiteral("{}"));
        QCOMPARE(registry->meta(kKey)->categoryId, QStringLiteral("shortcuts"));
        QVERIFY(registry->categories().contains(QStringLiteral("shortcuts")));
        QCOMPARE(registry->categoryName(QStringLiteral("shortcuts")),
                 QStringLiteral("快捷键"));
        QCOMPARE(registry->categoryGroup(QStringLiteral("shortcuts")),
                 QStringLiteral("appearance"));

        auto* catalog = SettingsCatalog::instance();
        QVERIFY(catalog->hasEntry(kKey));
        const QVariantMap entry = catalog->entry(kKey);
        QCOMPARE(entry.value(QStringLiteral("category")).toString(),
                 QStringLiteral("shortcuts"));
        QCOMPARE(catalog->resetMode(QStringLiteral("shortcuts")),
                 QStringLiteral("preference"));

        QCOMPARE(catalog->describe(kKey).value(QStringLiteral("display")).toString(),
                 QStringLiteral("全部默认"));
        QVERIFY(ShortcutMap::instance()->assign(QStringLiteral("panel.toggle"),
                                                QStringLiteral("Ctrl+Shift+B"))
                    .value(QStringLiteral("ok")).toBool());
        QCOMPARE(catalog->describe(kKey).value(QStringLiteral("display")).toString(),
                 QStringLiteral("1 项自定义"));
    }

private:
    QTemporaryDir m_settingsDir;
};

QTEST_MAIN(TestShortcuts)
#include "test_shortcuts.moc"
