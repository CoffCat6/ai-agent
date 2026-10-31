// docs/31 S1（O1）/ S2（O4、O11）：设置页交互级 QML 回归。
//
// 纯 C++ 用例只能证明 SettingsCatalog / parseImport 的返回值正确；用户
// 看到的是 SettingRow 里的圆点、恢复按钮和导入预览的差异行。若绑定漏读
// revision 或 Repeater 的 model 没接上，数据变了界面也不动——这类"逻辑
// 通了、界面不动"的退化只有真加载组件才能发现。
#include <QtTest>

#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QFontDatabase>

#include <QQmlAbstractUrlInterceptor>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QDir>
#include <QFile>
#include <QQuickItem>
#include <QQuickWindow>
#include <QUuid>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

#include "viewmodels/AppInfo.h"
#include "viewmodels/ChangeBus.h"
#include "viewmodels/EditBuffer.h"
#include "viewmodels/FontProbe.h"
#include "viewmodels/Onboarding.h"
#include "viewmodels/PreferenceStore.h"
#include "viewmodels/SettingsService.h"
#include "viewmodels/ShortcutMap.h"
#include "viewmodels/ThemeProvider.h"

using namespace NexusAI;

namespace {

// build/NexusAI 的 qmldir 指向 qrc：测试进程访问不到应用 qrc，重写到
// 构建树里的文件系统副本（与 test_icon_flip / test_todo_qml 同一套方案）。
class NexusQrcFallback : public QQmlAbstractUrlInterceptor {
public:
    explicit NexusQrcFallback(QString baseDir) : m_baseDir(std::move(baseDir)) {}
    QUrl intercept(const QUrl& url, DataType) override {
        static const QString prefix = QStringLiteral("/qt/qml/NexusAI");
        if (url.scheme() == QStringLiteral("qrc")) {
            const QString path = url.path();
            if (path.startsWith(prefix)) {
                const QString fsPath = m_baseDir + path.mid(prefix.length());
                if (QFile::exists(fsPath)) return QUrl::fromLocalFile(fsPath);
            }
        }
        return url;
    }

private:
    QString m_baseDir;
};

// SettingRow 的状态槽里只有一个 IconButton icon="undo"；它同时有 clicked()
// 信号，用来把手动挑选误差降到零（AppIcon 子项也带 icon 属性但没有信号）。
// Repeater 生成的 delegate 只有 setParentItem()，没有 QObject 父链，
// findChildren 找不到它们；渲染断言必须沿可视树（childItems）遍历。
void collectItemTexts(QQuickItem* item, QStringList* texts) {
    if (!item) return;
    const QVariant text = item->property("text");
    if (text.isValid() && text.canConvert<QString>() && !text.toString().isEmpty())
        texts->append(text.toString());
    for (QQuickItem* child : item->childItems())
        collectItemTexts(child, texts);
}

QObject* findUndoButton(QObject* row) {
    if (!row) return nullptr;
    for (QObject* child : row->findChildren<QObject*>()) {
        const QVariant icon = child->property("icon");
        if (icon.isValid() && icon.toString() == QStringLiteral("undo")
            && child->metaObject()->indexOfSignal("clicked()") >= 0)
            return child;
    }
    return nullptr;
}

// Repeater 的 delegate 只有可视父项，QObject 父链不可靠；按 obj 名字沿
// childItems 找（与 collectItemTexts 同一套遍历方式）。
QQuickItem* findVisualItem(QQuickItem* root, const QString& name) {
    if (!root) return nullptr;
    if (root->objectName() == name) return root;
    for (QQuickItem* child : root->childItems())
        if (QQuickItem* hit = findVisualItem(child, name)) return hit;
    return nullptr;
}

// 按按钮文案找可点击控件（AppButton 没有 objectName，文案是唯一稳定标识）。
QQuickItem* findClickableWithText(QQuickItem* root, const QString& text) {
    if (!root) return nullptr;
    const QVariant label = root->property("text");
    if (label.isValid() && label.toString() == text
        && root->metaObject()->indexOfSignal("clicked()") >= 0)
        return root;
    for (QQuickItem* child : root->childItems())
        if (QQuickItem* hit = findClickableWithText(child, text)) return hit;
    return nullptr;
}

// docs/31 N1：向导测试要能从「全新安装」开始，且结束后不把临时配置
// 留给同进程的后续用例。
void clearOnboardingState() {
    auto* settings = SettingsService::instance();
    while (!settings->profiles().isEmpty())
        settings->removeProfile(settings->profiles().first().toMap()
                                    .value(QStringLiteral("id")).toString());
    PreferenceStore::instance()->reset(QStringLiteral("general.onboarded"));
}

} // namespace

class TestSettingsPageQml : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        // 与 test_chat_workflows 一致：Basic 样式支持控件定制，避免
        // AppScrollBar 在默认样式下刷 "does not support customization" 警告。
        qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
        QVERIFY(m_settingsDir.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_settingsDir.path());
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("NexusAI"));
        QCoreApplication::setApplicationName(
            QStringLiteral("NexusAISettingsPageTests_%1")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    }

    // 修改后按钮出现、点击后值复位且按钮消失——绑定与动作链路都覆盖。
    void resetButtonFollowsModifiedState() {
        NexusQrcFallback fallback(QStringLiteral(NEXUS_BUILD_DIR "/NexusAI"));
        QQmlApplicationEngine engine;
        engine.addUrlInterceptor(&fallback);
        engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));
        QQmlComponent component(
            &engine,
            QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR
                                               "/SettingsHarness.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        auto* win = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(win);
        win->resize(760, 480);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));

        QObject* row = win->findChild<QObject*>(QStringLiteral("autoStartRow"));
        QVERIFY(row);
        QObject* undo = findUndoButton(row);
        QVERIFY(undo);

        // 默认值状态：没有圆点、没有按钮。
        QVERIFY(!undo->property("visible").toBool());
        QCOMPARE(row->property("valueState").toMap()
                     .value(QStringLiteral("modified")).toBool(),
                 false);

        // 改值 → revision 变化 → 绑定重算 → 按钮出现。
        EditBuffer::instance()->stage(QStringLiteral("general.autoStart"), true);
        QTRY_VERIFY(undo->property("visible").toBool());
        const QVariantMap changed = row->property("valueState").toMap();
        QCOMPARE(changed.value(QStringLiteral("modified")).toBool(), true);
        QCOMPARE(changed.value(QStringLiteral("revertable")).toBool(), true);
        QCOMPARE(changed.value(QStringLiteral("display")).toString(),
                 QStringLiteral("开"));

        // 点按钮本身（走 QML onClicked → SettingsCatalog.resetValue）。
        QVERIFY(QMetaObject::invokeMethod(undo, "clicked"));
        QTRY_VERIFY(!undo->property("visible").toBool());
        QCOMPARE(PreferenceStore::instance()->load(QStringLiteral("general.autoStart"))
                     .toBool(),
                 false);
        QCOMPARE(row->property("valueState").toMap()
                     .value(QStringLiteral("modified")).toBool(),
                 false);
    }

    // 密钥行没有复位按钮：secret=true 且无有效默认值 → revertable=false。
    void secretRowNeverOffersReset() {
        NexusQrcFallback fallback(QStringLiteral(NEXUS_BUILD_DIR "/NexusAI"));
        QQmlApplicationEngine engine;
        engine.addUrlInterceptor(&fallback);
        engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));
        QQmlComponent component(
            &engine,
            QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR
                                               "/SettingsHarness.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        auto* win = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(win);
        win->resize(760, 480);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));

        SettingsService::instance()->setWebSearchProvider(QStringLiteral("zhipu"));
        SettingsService::instance()->setWebSearchApiKey(
            QStringLiteral("sk-qml-canary-value"));

        QObject* row = win->findChild<QObject*>(QStringLiteral("secretRow"));
        QVERIFY(row);
        QObject* undo = findUndoButton(row);
        QVERIFY(undo);
        QTRY_VERIFY(!undo->property("visible").toBool());

        const QVariantMap state = row->property("valueState").toMap();
        QCOMPARE(state.value(QStringLiteral("secret")).toBool(), true);
        QCOMPARE(state.value(QStringLiteral("revertable")).toBool(), false);
        QCOMPARE(state.value(QStringLiteral("display")).toString(),
                 QStringLiteral("已配置"));
        QVERIFY(!state.value(QStringLiteral("display")).toString()
                     .contains(QStringLiteral("canary")));

        SettingsService::instance()->setWebSearchApiKey(QString());
        SettingsService::instance()->setWebSearchProvider(QStringLiteral("duckduckgo"));
    }

    // docs/31 O4 / O11：导入预览的逐键差异必须真的渲染出来。
    // C++ 只喂 valid + changes，弹窗里的 Repeater 应生成对应行数；
    // 空 changes 时一条都不该有（"完全一致"提示走另一条文本）。
    void importPreviewRendersPerKeyChanges() {
        NexusQrcFallback fallback(QStringLiteral(NEXUS_BUILD_DIR "/NexusAI"));
        QQmlApplicationEngine engine;
        engine.addUrlInterceptor(&fallback);
        engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));
        QQmlComponent component(
            &engine,
            QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR
                                               "/BackupPageHarness.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        auto* win = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(win);
        win->resize(1000, 760);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));

        const QVariantList changes{
            QVariantMap{{QStringLiteral("key"), QStringLiteral("embedding/model")},
                        {QStringLiteral("title"), QStringLiteral("嵌入模型")},
                        {QStringLiteral("category"), QStringLiteral("knowledge")},
                        {QStringLiteral("categoryName"), QStringLiteral("知识库")},
                        {QStringLiteral("currentDisplay"), QStringLiteral("changed-embed")},
                        {QStringLiteral("importedDisplay"), QStringLiteral("base-embed")},
                        {QStringLiteral("secret"), false}},
            QVariantMap{{QStringLiteral("key"), QStringLiteral("logging/level")},
                        {QStringLiteral("title"), QStringLiteral("日志级别")},
                        {QStringLiteral("category"), QStringLiteral("logs")},
                        {QStringLiteral("categoryName"), QStringLiteral("日志")},
                        {QStringLiteral("currentDisplay"), QStringLiteral("调试")},
                        {QStringLiteral("importedDisplay"), QStringLiteral("信息")},
                        {QStringLiteral("secret"), false}}};
        const QVariantMap preview{
            {QStringLiteral("valid"), true},
            {QStringLiteral("error"), QString()},
            {QStringLiteral("sections"), QStringList{QStringLiteral("应用设置（3 项）")}},
            {QStringLiteral("skippedSecrets"), QStringList{}},
            {QStringLiteral("warnings"), QStringList{}},
            {QStringLiteral("changes"), changes},
            {QStringLiteral("changesTotal"), 2},
            {QStringLiteral("changesTruncated"), 0}};
        QVERIFY(QMetaObject::invokeMethod(root.get(), "setPreview",
                                          Q_ARG(QVariant, QVariant(preview))));

        QObject* dialog =
            win->findChild<QObject*>(QStringLiteral("backup/importConfirmDialog"));
        QVERIFY(dialog);
        // 弹窗内容（含 Repeater 的 delegate）在打开时才真正进入可视树，
        // 所以先打开再断言行数。
        dialog->setProperty("visible", true);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QObject* repeater =
            dialog->findChild<QObject*>(QStringLiteral("backup/importChanges"));
        QVERIFY(repeater);
        QTRY_COMPARE(repeater->property("count").toInt(), 2);
        QQuickItem* firstRow = nullptr;
        QVERIFY(QMetaObject::invokeMethod(
            repeater, "itemAt", Q_RETURN_ARG(QQuickItem*, firstRow),
            Q_ARG(int, 0)));
        QVERIFY(firstRow);
        QCOMPARE(firstRow->objectName(),
                 QStringLiteral("backup/importChangeRow"));
        QStringList texts;
        collectItemTexts(firstRow, &texts);
        QVERIFY(texts.contains(QStringLiteral("嵌入模型")));
        QVERIFY(texts.contains(QStringLiteral("知识库")));
        QVERIFY(texts.contains(QStringLiteral("changed-embed")));
        QVERIFY(texts.contains(QStringLiteral("base-embed")));

        QQuickItem* secondRow = nullptr;
        QVERIFY(QMetaObject::invokeMethod(
            repeater, "itemAt", Q_RETURN_ARG(QQuickItem*, secondRow),
            Q_ARG(int, 1)));
        QVERIFY(secondRow);
        texts.clear();
        collectItemTexts(secondRow, &texts);
        QVERIFY(texts.contains(QStringLiteral("调试")));
        QVERIFY(texts.contains(QStringLiteral("信息")));

        // 恢复一份与当前完全一致的备份：差异列表清空。
        QVariantMap emptyPreview = preview;
        emptyPreview.insert(QStringLiteral("changes"), QVariantList{});
        emptyPreview.insert(QStringLiteral("changesTotal"), 0);
        QVERIFY(QMetaObject::invokeMethod(root.get(), "setPreview",
                                          Q_ARG(QVariant, QVariant(emptyPreview))));
        QTRY_COMPARE(repeater->property("count").toInt(), 0);
    }

    // docs/31 O9：字体下拉真的按搜索词过滤，等宽下拉只列等宽字体。
    // 旧配置里手选的非等宽字体是唯一例外（保持在列表顶部，避免空白）。
    //
    // offscreen 平台下 Qt 的 basic font database 没有任何系统字体
    // （"Cannot find font directory .../lib/fonts"），因此先注册两套
    // Windows 自带字体：一个等宽（Consolas）、一个非等宽（Arial）。
    void interfaceFontSearchFiltersAndMonoOnlyFixedPitch() {
        auto* probe = FontProbe::instance();
        if (probe->availableFamilies().isEmpty()) {
            for (const QString& path : {QStringLiteral("C:/Windows/Fonts/consola.ttf"),
                                        QStringLiteral("C:/Windows/Fonts/arial.ttf")}) {
                if (QFile::exists(path)) QFontDatabase::addApplicationFont(path);
            }
        }
        if (probe->availableFamilies().isEmpty())
            QSKIP("no font database available in this environment");

        NexusQrcFallback fallback(QStringLiteral(NEXUS_BUILD_DIR "/NexusAI"));
        QQmlApplicationEngine engine;
        engine.addUrlInterceptor(&fallback);
        engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));
        QQmlComponent component(
            &engine,
            QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR
                                               "/FontPageHarness.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        auto* win = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(win);
        win->resize(900, 720);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));

        const QStringList allFamilies = probe->availableFamilies();
        QVERIFY(!allFamilies.isEmpty());

        QObject* search = win->findChild<QObject*>(QStringLiteral("ui/fontSearchField"));
        QObject* sansCombo = win->findChild<QObject*>(QStringLiteral("ui/fontSansCombo"));
        QObject* monoCombo = win->findChild<QObject*>(QStringLiteral("ui/fontMonoCombo"));
        QObject* emptyHint = win->findChild<QObject*>(QStringLiteral("ui/fontSearchEmpty"));
        QVERIFY(search);
        QVERIFY(sansCombo);
        QVERIFY(monoCombo);
        QVERIFY(emptyHint);
        QTRY_COMPARE(sansCombo->property("count").toInt(), allFamilies.size());

        // 等宽下拉 ⊆ fixedPitch ∪ {当前配置值}。
        const QStringList monoFamilies = probe->fixedPitchFamilies();
        QVERIFY2(!monoFamilies.isEmpty(), "no fixed-pitch font families available");
        const QString currentMono =
            PreferenceStore::instance()->load(QStringLiteral("ui.fontMono")).toString();
        const int expectedMono = monoFamilies.size()
            + (currentMono.isEmpty() || monoFamilies.contains(currentMono) ? 0 : 1);
        QTRY_COMPARE(monoCombo->property("count").toInt(), expectedMono);
        for (const QVariant& value : monoCombo->property("model").toList()) {
            const QString family = value.toString();
            QVERIFY2(probe->isFixedPitch(family) || family == currentMono,
                     qPrintable(QStringLiteral("non-fixed-pitch family in mono list: %1")
                                    .arg(family)));
        }

        // 取一个纯 ASCII 字体名的前两个字符做搜索词（中文名不会参与
        // 大小写不敏感匹配的验证）。
        QString needle;
        for (const QString& family : allFamilies) {
            bool ascii = true;
            for (const QChar ch : family) {
                if (ch.unicode() > 127) { ascii = false; break; }
            }
            if (ascii && family.size() >= 2) { needle = family.left(2); break; }
        }
        QVERIFY(!needle.isEmpty());
        search->setProperty("text", needle);
        QTRY_VERIFY(sansCombo->property("count").toInt() > 0);
        QVERIFY(sansCombo->property("count").toInt() < allFamilies.size());
        for (const QVariant& value : sansCombo->property("model").toList())
            QVERIFY(value.toString().contains(needle, Qt::CaseInsensitive));

        // 无匹配：两个下拉都空，并给出提示。
        search->setProperty("text", QStringLiteral("zzzz-no-such-font"));
        QTRY_COMPARE(sansCombo->property("count").toInt(), 0);
        QTRY_COMPARE(monoCombo->property("count").toInt(), 0);
        QVERIFY(emptyHint->property("visible").toBool());

        // 清空搜索恢复全量。
        search->setProperty("text", QString());
        QTRY_COMPARE(sansCombo->property("count").toInt(), allFamilies.size());
        QVERIFY(!emptyHint->property("visible").toBool());
    }

    // docs/31 N10：关于页的构建信息与开源许可列表。
    // AppInfo 的值必须与编译期/运行期事实一致，页面必须绑定 AppInfo 而不是
    // 自己再算一份（否则换工具链后界面还在报旧数据）。
    void aboutPageShowsBuildInfoAndLicenses() {
        auto* info = AppInfo::instance();
        QCOMPARE(info->qtVersion(), QStringLiteral(QT_VERSION_STR));
        QVERIFY(!info->compiler().isEmpty());
        QVERIFY(info->compiler() != QStringLiteral("unknown"));
        QVERIFY(!info->architecture().isEmpty());
        QVERIFY(!info->osVersion().isEmpty());
        QVERIFY(!info->dataDir().isEmpty());
        QVERIFY(!info->licenses().isEmpty());
        QVERIFY(info->licenses().join(QLatin1Char('\n')).contains(QStringLiteral("Qt")));

        NexusQrcFallback fallback(QStringLiteral(NEXUS_BUILD_DIR "/NexusAI"));
        QQmlApplicationEngine engine;
        engine.addUrlInterceptor(&fallback);
        engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));
        QQmlComponent component(
            &engine,
            QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR
                                               "/GeneralPageHarness.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        auto* win = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(win);
        win->resize(900, 720);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));

        auto valueText = [win](const QString& name) {
            QObject* item = win->findChild<QObject*>(name);
            return item ? item->property("text").toString() : QString();
        };
        QCOMPARE(valueText(QStringLiteral("about/versionValue")), info->appVersion());
        QCOMPARE(valueText(QStringLiteral("about/qtVersionValue")), info->qtVersion());
        QCOMPARE(valueText(QStringLiteral("about/compilerValue")), info->compiler());
        QCOMPARE(valueText(QStringLiteral("about/architectureValue")), info->architecture());
        QCOMPARE(valueText(QStringLiteral("about/dataDirValue")), info->dataDir());

        QObject* dialog = win->findChild<QObject*>(QStringLiteral("about/licensesDialog"));
        QVERIFY(dialog);
        dialog->setProperty("visible", true);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QObject* list = dialog->findChild<QObject*>(QStringLiteral("about/licensesList"));
        QVERIFY(list);
        QTRY_COMPARE(list->property("count").toInt(), info->licenses().size());
        dialog->setProperty("visible", false);
    }

    // docs/31 N2：快捷键页必须真的录制按键、拒绝冲突、行级恢复。
    // 只测 ShortcutMap 的返回值证明不了 QML 的 Keys 处理与 Repeater 绑定；
    // 这里用真实 QTest 按键走完整链路。
    void shortcutPageRecordsConflictsAndResets() {
        auto* map = ShortcutMap::instance();
        map->resetAll();
        QVERIFY(PreferenceStore::instance()->save(QStringLiteral("ui.shortcuts"),
                                                  QStringLiteral("{}")));

        NexusQrcFallback fallback(QStringLiteral(NEXUS_BUILD_DIR "/NexusAI"));
        QQmlApplicationEngine engine;
        engine.addUrlInterceptor(&fallback);
        engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));
        QQmlComponent component(
            &engine,
            QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR
                                               "/ShortcutsPageHarness.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        auto* win = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(win);
        win->resize(900, 720);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));

        QObject* page = win->findChild<QObject*>(QStringLiteral("shortcutsPage"));
        QVERIFY(page);
        auto* pageItem = qobject_cast<QQuickItem*>(page);
        QVERIFY(pageItem);

        // 动作表里的每个动作都要有一行（Repeater 绑定生效）。
        for (const QVariant& value : map->actions()) {
            const QString id = value.toMap().value(QStringLiteral("id")).toString();
            QVERIFY2(findVisualItem(pageItem, QStringLiteral("shortcut/") + id),
                     qPrintable(id));
        }

        // 录制 Ctrl+Shift+B：真实按键 → sequenceFromKey → assign → 行文本。
        QVERIFY(QMetaObject::invokeMethod(page, "beginRecording",
                                          Q_ARG(QVariant,
                                                QStringLiteral("panel.toggle"))));
        QTRY_VERIFY(map->recording());
        QCOMPARE(page->property("recordingId").toString(),
                 QStringLiteral("panel.toggle"));
        QTest::keyClick(win, Qt::Key_B, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(!map->recording());
        QCOMPARE(map->bindings().value(QStringLiteral("panel.toggle")).toString(),
                 QStringLiteral("Ctrl+Shift+B"));
        QQuickItem* valueItem =
            findVisualItem(pageItem, QStringLiteral("shortcut/value/panel.toggle"));
        QVERIFY(valueItem);
        QTRY_COMPARE(valueItem->property("text").toString(),
                     QStringLiteral("Ctrl+Shift+B"));

        // 冲突：Ctrl+L 属于审计日志 → 不写入，并把原因显示出来。
        QVERIFY(QMetaObject::invokeMethod(page, "beginRecording",
                                          Q_ARG(QVariant, QStringLiteral("todo.open"))));
        QTRY_VERIFY(map->recording());
        QTest::keyClick(win, Qt::Key_L, Qt::ControlModifier);
        QTRY_VERIFY(!map->recording());
        QCOMPARE(map->bindings().value(QStringLiteral("todo.open")).toString(),
                 QStringLiteral("Ctrl+T"));
        QCOMPARE(page->property("messageIsError").toBool(), true);
        QQuickItem* message =
            findVisualItem(pageItem, QStringLiteral("shortcut/message"));
        QVERIFY(message);
        QTRY_VERIFY(message->property("visible").toBool());
        QVERIFY(!message->property("text").toString().isEmpty());

        // Esc 取消录制且不改变映射。
        QVERIFY(QMetaObject::invokeMethod(page, "beginRecording",
                                          Q_ARG(QVariant, QStringLiteral("review.open"))));
        QTRY_VERIFY(map->recording());
        QTest::keyClick(win, Qt::Key_Escape);
        QTRY_VERIFY(!map->recording());
        QCOMPARE(page->property("recordingId").toString(), QString());
        QCOMPARE(map->bindings().value(QStringLiteral("review.open")).toString(),
                 QStringLiteral("Ctrl+R"));

        // 行级「恢复」按钮只撤销这一项。
        QQuickItem* resetButton =
            findVisualItem(pageItem, QStringLiteral("shortcut/reset/panel.toggle"));
        QVERIFY(resetButton);
        QTRY_VERIFY(resetButton->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(resetButton, "clicked"));
        QTRY_COMPARE(map->bindings().value(QStringLiteral("panel.toggle")).toString(),
                     QStringLiteral("Ctrl+B"));
        QTRY_VERIFY(!resetButton->property("visible").toBool());

        map->resetAll();
    }

    // docs/31 N2：速查表由动作表生成，改键后立即显示新序列。
    void shortcutDialogReflectsCustomBindings() {
        auto* map = ShortcutMap::instance();
        map->resetAll();
        QVERIFY(map->assign(QStringLiteral("panel.toggle"),
                            QStringLiteral("Ctrl+Shift+B"))
                    .value(QStringLiteral("ok")).toBool());

        NexusQrcFallback fallback(QStringLiteral(NEXUS_BUILD_DIR "/NexusAI"));
        QQmlApplicationEngine engine;
        engine.addUrlInterceptor(&fallback);
        engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));
        QQmlComponent component(
            &engine,
            QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR
                                               "/ShortcutsDialogHarness.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        auto* win = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(win);
        win->resize(700, 760);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));

        QObject* dialog = win->findChild<QObject*>(QStringLiteral("shortcutsDialog"));
        QVERIFY(dialog);
        QObject* globalList =
            dialog->findChild<QObject*>(QStringLiteral("shortcuts/globalList"));
        QObject* fixedList =
            dialog->findChild<QObject*>(QStringLiteral("shortcuts/fixedList"));
        QVERIFY(globalList);
        QVERIFY(fixedList);
        QTRY_COMPARE(globalList->property("count").toInt(), map->actions().size());
        QTRY_COMPARE(fixedList->property("count").toInt(), 5);

        // 第 4 个动作是 panel.toggle（动作表顺序）。
        auto rowTexts = [globalList](int index) {
            QQuickItem* row = nullptr;
            QMetaObject::invokeMethod(globalList, "itemAt",
                                      Q_RETURN_ARG(QQuickItem*, row),
                                      Q_ARG(int, index));
            QStringList texts;
            collectItemTexts(row, &texts);
            return texts;
        };
        QStringList texts = rowTexts(3);
        QVERIFY(texts.contains(QStringLiteral("Ctrl+Shift+B")));
        QVERIFY(texts.contains(
            map->actions().at(3).toMap().value(QStringLiteral("title")).toString()));

        // 改键 → model 变化 → 速查表更新。
        QVERIFY(map->assign(QStringLiteral("panel.toggle"),
                            QStringLiteral("Ctrl+Alt+B"))
                    .value(QStringLiteral("ok")).toBool());
        QTRY_VERIFY(rowTexts(3).contains(QStringLiteral("Ctrl+Alt+B")));

        map->resetAll();
    }

    // docs/31 N1：三步走完 → 创建一条可用的模型配置 + 写「已看过」标记。
    void onboardingWizardCreatesProfileAndMarksDone() {
        clearOnboardingState();
        QVERIFY(Onboarding::instance()->shouldShow());

        NexusQrcFallback fallback(QStringLiteral(NEXUS_BUILD_DIR "/NexusAI"));
        QQmlApplicationEngine engine;
        engine.addUrlInterceptor(&fallback);
        engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));
        QQmlComponent component(
            &engine,
            QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR
                                               "/OnboardingDialogHarness.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        auto* win = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(win);
        win->resize(760, 620);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));

        QObject* page = win->findChild<QObject*>(QStringLiteral("onboarding"));
        QVERIFY(page);
        QVERIFY(QMetaObject::invokeMethod(page, "openOnboarding"));
        QTRY_VERIFY(page->property("visible").toBool());
        QCOMPARE(page->property("step").toInt(), 0);

        // 第一步只有语言/主题，下一步才懒创建配置——跳过时不留空配置。
        QCOMPARE(SettingsService::instance()->profiles().size(), 0);
        QVERIFY(QMetaObject::invokeMethod(page, "next"));
        QCOMPARE(page->property("step").toInt(), 1);
        const QString profileId = page->property("profileId").toString();
        QVERIFY(!profileId.isEmpty());
        QCOMPARE(SettingsService::instance()->profiles().size(), 1);

        // 换来源：地址跟着预设重填，模型清空；没有模型时不能完成。
        QVERIFY(QMetaObject::invokeMethod(page, "selectKind",
                                          Q_ARG(QVariant, QStringLiteral("anthropic"))));
        QCOMPARE(page->property("kind").toString(), QStringLiteral("anthropic"));
        QQuickItem* baseUrl = findVisualItem(win->contentItem(),
                                             QStringLiteral("onboarding/baseUrl"));
        QQuickItem* model = findVisualItem(win->contentItem(),
                                           QStringLiteral("onboarding/model"));
        QQuickItem* key = findVisualItem(win->contentItem(),
                                         QStringLiteral("onboarding/apiKey"));
        QVERIFY(baseUrl);
        QVERIFY(model);
        QVERIFY(key);
        QVERIFY(!baseUrl->property("text").toString().isEmpty());
        QVERIFY(model->property("text").toString().isEmpty());
        QVERIFY(!page->property("canFinish").toBool());

        model->setProperty("text", QStringLiteral("claude-3-5-haiku-latest"));
        key->setProperty("text", QStringLiteral("sk-onboarding-canary"));
        QVERIFY(page->property("canFinish").toBool());

        // 下一步提交字段并进入导入步骤；完成即写标记。
        QVERIFY(QMetaObject::invokeMethod(page, "next"));
        QCOMPARE(page->property("step").toInt(), 2);
        const QVariantMap saved = SettingsService::instance()->profiles()
                                      .at(0).toMap();
        QCOMPARE(saved.value(QStringLiteral("model")).toString(),
                 QStringLiteral("claude-3-5-haiku-latest"));
        QCOMPARE(saved.value(QStringLiteral("kind")).toString(),
                 QStringLiteral("anthropic"));

        QVERIFY(QMetaObject::invokeMethod(page, "finish"));
        QTRY_VERIFY(!page->property("visible").toBool());
        QVERIFY(Onboarding::instance()->isComplete());
        QVERIFY(!Onboarding::instance()->shouldShow());

        clearOnboardingState();
    }

    // 跳过也是「已看过」：不能因为用户没配置就每次启动再弹；向导自己建
    // 的空配置要顺手删掉，别把半成品留给用户。
    void onboardingSkipRemovesUntouchedProfile() {
        clearOnboardingState();

        NexusQrcFallback fallback(QStringLiteral(NEXUS_BUILD_DIR "/NexusAI"));
        QQmlApplicationEngine engine;
        engine.addUrlInterceptor(&fallback);
        engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));
        QQmlComponent component(
            &engine,
            QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR
                                               "/OnboardingDialogHarness.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        auto* win = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(win);
        win->resize(760, 620);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));

        QObject* page = win->findChild<QObject*>(QStringLiteral("onboarding"));
        QVERIFY(page);
        QVERIFY(QMetaObject::invokeMethod(page, "openOnboarding"));
        QTRY_VERIFY(page->property("visible").toBool());

        QVERIFY(QMetaObject::invokeMethod(page, "next"));
        QCOMPARE(page->property("step").toInt(), 1);
        QCOMPARE(SettingsService::instance()->profiles().size(), 1);

        QVERIFY(QMetaObject::invokeMethod(page, "skip"));
        QTRY_VERIFY(!page->property("visible").toBool());
        QVERIFY(Onboarding::instance()->isComplete());
        QCOMPARE(SettingsService::instance()->profiles().size(), 0);

        clearOnboardingState();
    }

    // docs/31 S4.1 O8：设置中心的尺寸记忆。
    // 尺寸是 QML 侧直接读写的隐藏偏好键，只能靠真实拖动来证明"拖完关闭、
    // 下次打开还是这个大小"。同时覆盖两个易漏点：拖动过程不落盘（只在
    // 关闭时写一次），以及父窗口比记住的尺寸小时按父窗口 clamp、但关闭时
    // 不能把 clamp 后的值当成用户尺寸存回去。
    void dialogSizeIsRememberedAndClamped() {
        auto* store = PreferenceStore::instance();
        QVERIFY(store->reset(QStringLiteral("ui.settingsDialogWidth")));
        QVERIFY(store->reset(QStringLiteral("ui.settingsDialogHeight")));
        // 清掉上次停靠的分类，避免 onOpened 把初始分类改回其它页。
        store->reset(QStringLiteral("settings.lastCategory"));

        NexusQrcFallback fallback(QStringLiteral(NEXUS_BUILD_DIR "/NexusAI"));
        QQmlApplicationEngine engine;
        engine.addUrlInterceptor(&fallback);
        engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));
        QQmlComponent component(
            &engine,
            QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR
                                               "/SettingsCenterDialogHarness.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        auto* win = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(win);
        win->resize(1000, 800);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));

        QObject* dialog = win->findChild<QObject*>(QStringLiteral("settingsCenterDialog"));
        QVERIFY(dialog);

        // 从未保存过：默认 900×680 且不越出父窗口。
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QCOMPARE(dialog->property("preferredWidth").toInt(), 900);
        QCOMPARE(dialog->property("preferredHeight").toInt(), 680);
        QCOMPARE(dialog->property("width").toInt(), 900);
        QCOMPARE(dialog->property("height").toInt(), 680);

        // 拖动右下角把手：尺寸立即变化，但拖动过程不写设置。
        auto* handle = win->findChild<QQuickItem*>(QStringLiteral("settingsResizeHandle"));
        QVERIFY(handle);
        QVERIFY(handle->isVisible());
        const QPoint start =
            handle->mapToScene(QPointF(handle->width() / 2, handle->height() / 2)).toPoint();
        const QPoint end = start + QPoint(40, 30);
        QTest::mousePress(win, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(win, end);
        QTest::mouseRelease(win, Qt::LeftButton, Qt::NoModifier, end);
        QCOMPARE(dialog->property("preferredWidth").toInt(), 940);
        QCOMPARE(dialog->property("preferredHeight").toInt(), 710);
        QCOMPARE(dialog->property("width").toInt(), 940);
        QVERIFY(!store->hasStoredValue(QStringLiteral("ui.settingsDialogWidth")));
        QVERIFY(!store->hasStoredValue(QStringLiteral("ui.settingsDialogHeight")));

        // 关闭才落盘。
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QTRY_VERIFY(!dialog->property("opened").toBool());
        QCOMPARE(store->load(QStringLiteral("ui.settingsDialogWidth")).toInt(), 940);
        QCOMPARE(store->load(QStringLiteral("ui.settingsDialogHeight")).toInt(), 710);

        // 重开：先把内存里的尺寸改回默认，证明是被持久化的值恢复的。
        dialog->setProperty("preferredWidth", 900);
        dialog->setProperty("preferredHeight", 680);
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTRY_COMPARE(dialog->property("preferredWidth").toInt(), 940);
        QTRY_COMPARE(dialog->property("preferredHeight").toInt(), 710);
        QCOMPARE(dialog->property("width").toInt(), 940);
        QCOMPARE(dialog->property("height").toInt(), 710);

        // 父窗口缩到 700×500：显示尺寸 clamp 到 676×476，但记住的仍是 940×710。
        win->resize(700, 500);
        QTRY_COMPARE(dialog->property("width").toInt(), 676);
        QTRY_COMPARE(dialog->property("height").toInt(), 476);

        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QTRY_VERIFY(!dialog->property("opened").toBool());
        QCOMPARE(store->load(QStringLiteral("ui.settingsDialogWidth")).toInt(), 940);
        QCOMPARE(store->load(QStringLiteral("ui.settingsDialogHeight")).toInt(), 710);

        // 保存过小的尺寸时按最小值回弹，不会打开成一个抓不住的窄条。
        win->resize(1000, 800);
        QVERIFY(store->save(QStringLiteral("ui.settingsDialogWidth"), 300));
        QVERIFY(store->save(QStringLiteral("ui.settingsDialogHeight"), 200));
        dialog->setProperty("preferredWidth", 900);
        dialog->setProperty("preferredHeight", 680);
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTRY_COMPARE(dialog->property("preferredWidth").toInt(), 640);
        QTRY_COMPARE(dialog->property("preferredHeight").toInt(), 480);
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QTRY_VERIFY(!dialog->property("opened").toBool());

        store->reset(QStringLiteral("ui.settingsDialogWidth"));
        store->reset(QStringLiteral("ui.settingsDialogHeight"));
    }

    // docs/31 S4.3 N3：底栏「已保存」回执里的「撤销」是真实交互——改一个
    // 偏好后按钮出现，点击后值回原值、按钮消失、C++ 侧记录清空。
    // 用 general.autoStart（Bool、无副作用）而不是字号，省去页面滚动与
    // 控件坐标计算；回执文案与按钮由对话框统一渲染。
    void undoButtonRestoresPreferenceAndDisappears() {
        auto* store = PreferenceStore::instance();
        const QString key = QStringLiteral("general.autoStart");
        QVERIFY(store->reset(key));
        QVERIFY(store->reset(QStringLiteral("settings.lastCategory")));
        QVERIFY(store->reset(QStringLiteral("ui.settingsDialogWidth")));
        QVERIFY(store->reset(QStringLiteral("ui.settingsDialogHeight")));
        EditBuffer::instance()->rollback();

        NexusQrcFallback fallback(QStringLiteral(NEXUS_BUILD_DIR "/NexusAI"));
        QQmlApplicationEngine engine;
        engine.addUrlInterceptor(&fallback);
        engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));
        QQmlComponent component(
            &engine,
            QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR
                                               "/SettingsCenterDialogHarness.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        auto* win = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(win);
        win->resize(1000, 800);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));

        QObject* dialog = win->findChild<QObject*>(QStringLiteral("settingsCenterDialog"));
        QVERIFY(dialog);
        // 显式停在 general：撤销入口只在"这次写入属于当前分类"时出现。
        QVERIFY(QMetaObject::invokeMethod(dialog, "openCategory",
                                          Q_ARG(QVariant, QStringLiteral("general"))));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QCOMPARE(dialog->property("currentCategory").toString(),
                 QStringLiteral("general"));

        // 模拟页面上的编辑（走与真实控件相同的 EditBuffer::stage 写穿）。
        EditBuffer::instance()->stage(key, true);
        QCOMPARE(store->load(key).toBool(), true);
        QTRY_VERIFY(dialog->property("undoAvailable").toBool());
        QCOMPARE(dialog->property("undoKey").toString(), key);

        auto* undoButton =
            win->findChild<QObject*>(QStringLiteral("settingsUndoSaveButton"));
        QVERIFY(undoButton);
        QTRY_VERIFY(undoButton->property("visible").toBool());

        QVERIFY(QMetaObject::invokeMethod(undoButton, "clicked"));
        QTRY_VERIFY(!store->load(key).toBool());
        QVERIFY(!store->hasStoredValue(key));
        QTRY_VERIFY(!undoButton->property("visible").toBool());
        QVERIFY(!EditBuffer::instance()->canUndo());
        QTRY_VERIFY(dialog->property("saveStatusText").toString()
                        .contains(QStringLiteral("撤销")));

        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QTRY_VERIFY(!dialog->property("opened").toBool());
        store->reset(QStringLiteral("ui.settingsDialogWidth"));
        store->reset(QStringLiteral("ui.settingsDialogHeight"));
        store->reset(QStringLiteral("settings.lastCategory"));
    }

    // docs/31 S4.7 O10：「减少动效」开关必须让 Theme.motion* 三令牌即时归零、
    // 关掉后恢复——走与设置页开关完全相同的 PreferenceStore::save 写穿链路
    // （save → ChangeBus → ThemeManager → ThemeProvider），而不是直接调内部
    // setter，这样连"信号没接上、令牌不刷新"这类回归也能拦住。
    void reduceMotionZeroesMotionTokens() {
        // main.cpp 启动时的接线在测试进程里补齐；重复 set 无害。
        PreferenceStore::instance()->setChangeBus(ChangeBus::instance());
        auto* store = PreferenceStore::instance();
        auto* theme = ThemeProvider::instance();
        store->reset(QStringLiteral("ui.reduceMotion"));
        QCOMPARE(theme->motionFast(), 100);
        QCOMPARE(theme->motionBase(), 180);
        QCOMPARE(theme->motionSlow(), 260);

        QVERIFY(store->save(QStringLiteral("ui.reduceMotion"), true));
        QTRY_COMPARE(theme->motionFast(), 0);
        QCOMPARE(theme->motionBase(), 0);
        QCOMPARE(theme->motionSlow(), 0);

        QVERIFY(store->save(QStringLiteral("ui.reduceMotion"), false));
        QTRY_COMPARE(theme->motionBase(), 180);
        QCOMPARE(theme->motionFast(), 100);
        QCOMPARE(theme->motionSlow(), 260);

        // 落盘回读 + 清掉存储值回到 schema 默认，不污染其它用例。
        store->reset(QStringLiteral("ui.reduceMotion"));
        QCOMPARE(store->hasStoredValue(QStringLiteral("ui.reduceMotion")), false);
    }

    // docs/31 S4.5 N5：数据管理页。统计渲染（体积/行数真的显示出来）与
    // 三个维护动作的二次确认门槛——未点「确认执行」绝不触发动作。
    void dataPageRendersStatsAndGatesActions() {
        NexusQrcFallback fallback(QStringLiteral(NEXUS_BUILD_DIR "/NexusAI"));
        QQmlApplicationEngine engine;
        engine.addUrlInterceptor(&fallback);
        engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));
        QQmlComponent component(
            &engine,
            QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR
                                               "/DataPageHarness.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        auto* win = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(win);
        win->resize(1000, 800);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));

        QObject* harness = root.get();
        // statusMessage / statusIsError 挂在页面（非 Window 根）上。
        auto* page = win->findChild<QObject*>(QStringLiteral("dataPage"));
        QVERIFY(page);
        // Component.onCompleted 的首次 refresh 在测试进程里没有数据库，
        // 把 status 置成了「统计失败：…」；清掉再注入受控数据。
        QVERIFY(QMetaObject::invokeMethod(harness, "clearStatus"));

        // 渲染：注入一份受控 stats，体积/路径/行数必须出现在控件上。
        QVariantMap stats;
        stats.insert(QStringLiteral("dbPath"),
                     QDir::toNativeSeparators(QStringLiteral("X:/data/nexusai_memory.db")));
        stats.insert(QStringLiteral("fileSizeBytes"), 2048);
        stats.insert(QStringLiteral("walSizeBytes"), 0);
        stats.insert(QStringLiteral("pageCount"), 8);
        stats.insert(QStringLiteral("pageSize"), 4096);
        stats.insert(QStringLiteral("sessions"), 3);
        stats.insert(QStringLiteral("messages"), 12);
        stats.insert(QStringLiteral("tokenUsage"), 7);
        stats.insert(QStringLiteral("auditLog"), 0);
        QVERIFY(QMetaObject::invokeMethod(harness, "setStats",
                                          Q_ARG(QVariant, stats)));

        auto* dbSize = win->findChild<QObject*>(QStringLiteral("storageDbSizeText"));
        QVERIFY(dbSize);
        QVERIFY2(dbSize->property("text").toString()
                     .contains(QStringLiteral("2 KB")),
                 qPrintable(dbSize->property("text").toString()));
        auto* walSize = win->findChild<QObject*>(QStringLiteral("storageWalSizeText"));
        QVERIFY(walSize);
        QVERIFY2(walSize->property("text").toString()
                     .contains(QStringLiteral("0 B")),
                 qPrintable(walSize->property("text").toString()));
        auto* overview = win->findChild<QObject*>(QStringLiteral("storage/overview"));
        QVERIFY(overview);
        QCOMPARE(overview->property("subtitle").toString(),
                 QDir::toNativeSeparators(QStringLiteral("X:/data/nexusai_memory.db")));

        // 确认门槛：点「压缩」只开确认框，状态行保持干净（动作未执行）。
        auto* vacuumButton =
            win->findChild<QObject*>(QStringLiteral("storageVacuumButton"));
        QVERIFY(vacuumButton);
        QVERIFY(QMetaObject::invokeMethod(vacuumButton, "clicked"));
        auto* dialog =
            win->findChild<QObject*>(QStringLiteral("storageConfirmDialog"));
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(harness, "clearStatus"));
        QCOMPARE(page->property("statusMessage").toString(), QString());
        QCOMPARE(page->property("statusIsError").toBool(), false);

        // 取消：动作不执行，确认框关闭。
        auto* cancelButton =
            win->findChild<QObject*>(QStringLiteral("storageCancelButton"));
        QVERIFY(cancelButton);
        QVERIFY(QMetaObject::invokeMethod(cancelButton, "clicked"));
        QTRY_VERIFY(!dialog->property("opened").toBool());
        QCOMPARE(page->property("statusMessage").toString(), QString());

        // 确认：动作真正执行（测试进程里 AgentVM 没有数据库 → 显示
        // 「数据库未就绪」错误反馈——这证明动作链路走通了）。
        QVERIFY(QMetaObject::invokeMethod(vacuumButton, "clicked"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        auto* confirmButton =
            win->findChild<QObject*>(QStringLiteral("storageConfirmButton"));
        QVERIFY(confirmButton);
        QVERIFY(QMetaObject::invokeMethod(confirmButton, "clicked"));
        QTRY_VERIFY(!dialog->property("opened").toBool());
        const QString status =
            page->property("statusMessage").toString();
        QVERIFY2(!status.isEmpty(),
                 "确认后必须有反馈（成功或错误），静默 = 动作链路断了");
        QVERIFY2(page->property("statusIsError").toBool(),
                 qPrintable(QStringLiteral("预期「数据库未就绪」错误，实际：%1").arg(status)));
    }

    // docs/31 S4.4（4a）：点底栏「复制」必须真的生成一条不带密钥的配置，
    // 并把页面切到副本的编辑态。走真实按钮的 clicked 信号，是因为
    // duplicateProfile 会在信号栈里发 profilesChanged 重建芯片 Repeater、
    // 再改 editingId——只断言 C++ 返回值看不见这条"边发信号边重建视图"的
    // 链路（这里出问题就是闪退，而不是断言失败）。
    void duplicateButtonAddsKeylessProfile() {
        auto* settings = SettingsService::instance();
        while (!settings->profiles().isEmpty()) {
            settings->removeProfile(settings->profiles().first().toMap()
                                        .value(QStringLiteral("id")).toString());
        }
        const QString sourceId = settings->addProfile(QStringLiteral("openai"));
        QVERIFY(!sourceId.isEmpty());
        settings->updateProfile(sourceId,
                                QVariantMap{{QStringLiteral("apiKey"),
                                             QStringLiteral("sk-canary")},
                                            {QStringLiteral("baseUrl"),
                                             QStringLiteral("https://api.example.com/v1")},
                                            {QStringLiteral("model"),
                                             QStringLiteral("gpt-test")}});
        settings->setActiveProfile(sourceId);
        QCOMPARE(settings->activeProfileId(), sourceId);

        NexusQrcFallback fallback(QStringLiteral(NEXUS_BUILD_DIR "/NexusAI"));
        QQmlApplicationEngine engine;
        engine.addUrlInterceptor(&fallback);
        engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));
        QQmlComponent component(
            &engine,
            QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR
                                               "/ModelConfigHarness.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        auto* win = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(win);
        win->resize(1000, 760);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));

        QQuickItem* page = findVisualItem(win->contentItem(),
                                          QStringLiteral("modelConfigPage"));
        QVERIFY(page);
        // 芯片与底栏都要先真实渲染出来，否则按钮点击没有意义。
        QTRY_VERIFY(findClickableWithText(page, QStringLiteral("复制")) != nullptr);

        QQuickItem* copyButton = findClickableWithText(page, QStringLiteral("复制"));
        QVERIFY(QMetaObject::invokeMethod(copyButton, "clicked"));

        QCOMPARE(settings->profiles().size(), 2);
        // 启用关系不变：复制只新增副本，不切换活动配置。
        QCOMPARE(settings->activeProfileId(), sourceId);
        const QVariantMap copy = settings->profiles().at(1).toMap();
        QVERIFY(!copy.value(QStringLiteral("id")).toString().isEmpty());
        QVERIFY(copy.value(QStringLiteral("id")).toString() != sourceId);
        QCOMPARE(copy.value(QStringLiteral("apiKey")).toString(), QString());
        QCOMPARE(copy.value(QStringLiteral("model")).toString(),
                 QStringLiteral("gpt-test"));
        QCOMPARE(copy.value(QStringLiteral("displayName")).toString(),
                 QStringLiteral("%1 副本")
                     .arg(settings->profiles().at(0).toMap()
                              .value(QStringLiteral("displayName"))
                              .toString()));
        // 界面切到副本：底栏显示「编辑中的配置未启用」而不是继续停在源配置。
        QCOMPARE(page->property("editingId").toString(),
                 copy.value(QStringLiteral("id")).toString());

        win->close();
        while (!settings->profiles().isEmpty()) {
            settings->removeProfile(settings->profiles().first().toMap()
                                        .value(QStringLiteral("id")).toString());
        }
    }

private:
    QTemporaryDir m_settingsDir;
};

QTEST_MAIN(TestSettingsPageQml)
#include "test_settings_page_qml.moc"
