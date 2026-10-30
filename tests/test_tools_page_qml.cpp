// 工具管理页分组与折叠回归。
//
// 背景：内置工具随应用增长到十几项，平铺在设置页首屏会把联网搜索与
// 权限模式挤到很下面；MCP 工具又和内置工具混在同一串里，用户分不清
// 哪些是本地随附、哪些来自服务器。本用例锁住两条契约：
//   1. 工具列表按 source 拆成「内置 / MCP」两组，数量之和等于注册表总数；
//   2. 内置组默认折叠，点击标题（toggle）后展开，折叠期间行不可见。
//
// 加载方式与 test_charts_qml / test_icon_flip 相同：build/NexusAI 的
// 文件系统副本 + qrc 回退拦截器 + offscreen。
#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlAbstractUrlInterceptor>
#include <QQuickWindow>
#include <QQuickItem>
#include <QFile>
#include <memory>

namespace {
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

// 按 objectName 递归收集视觉子项（动态创建的 delegate 不保证是
// 窗口的 QObject 后代，走 visual parent 链更稳）。
void collectByName(QQuickItem* item, const QString& name,
                   QList<QQuickItem*>* out) {
    if (item->objectName() == name) out->append(item);
    const auto kids = item->childItems();
    for (QQuickItem* kid : kids) collectByName(kid, name, out);
}
} // namespace

class TestToolsPage : public QObject {
    Q_OBJECT
private slots:
    void builtinGroupCollapsedByDefault() {
        NexusQrcFallback fb(QStringLiteral(NEXUS_BUILD_DIR "/NexusAI"));
        QQmlApplicationEngine engine;
        engine.addUrlInterceptor(&fb);
        engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));
        QQmlComponent c(&engine, QUrl::fromLocalFile(
            QStringLiteral(NEXUS_TEST_QML_DIR "/ToolsPageHarness.qml")));
        QVERIFY2(c.isReady(), qPrintable(c.errorString()));
        std::unique_ptr<QObject> root(c.create());
        QVERIFY(root);
        auto* win = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(win);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));

        // 真实注册表至少包含内置工具；两个分组必须完整覆盖它。
        // QML 函数返回 JS number，经元对象是 QVariant，不能直接按 int 取。
        QVariant registryV, builtinV, mcpV;
        QVERIFY(QMetaObject::invokeMethod(root.get(), "registryCount",
                                          Q_RETURN_ARG(QVariant, registryV)));
        QVERIFY(QMetaObject::invokeMethod(root.get(), "builtinCount",
                                          Q_RETURN_ARG(QVariant, builtinV)));
        QVERIFY(QMetaObject::invokeMethod(root.get(), "mcpCount",
                                          Q_RETURN_ARG(QVariant, mcpV)));
        const int registry = registryV.toInt();
        const int builtin = builtinV.toInt();
        const int mcp = mcpV.toInt();
        QVERIFY2(builtin > 0, "内置工具注册表为空");
        QCOMPARE(builtin + mcp, registry);

        auto* builtinSection = win->findChild<QQuickItem*>(
            QStringLiteral("toolsBuiltinSection"));
        auto* mcpSection = win->findChild<QQuickItem*>(
            QStringLiteral("toolsMcpSection"));
        QVERIFY(builtinSection && mcpSection);
        QVERIFY(builtinSection->property("collapsible").toBool());
        QCOMPARE(builtinSection->property("collapsed").toBool(), true);
        QVERIFY(mcpSection->property("collapsible").toBool());

        // 折叠期间行仍然创建，但不可见（不占首屏）。
        QList<QQuickItem*> rows;
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            rows.clear();
            collectByName(win->contentItem(), QStringLiteral("toolRow"), &rows);
            return rows.size() == registry;
        }(), 3000);
        for (QQuickItem* row : rows) {
            const bool isBuiltin =
                row->property("modelData").toMap().value("source").toString() != "MCP";
            if (isBuiltin) QVERIFY2(!row->isVisible(), "折叠组内工具行仍然可见");
        }

        // 展开内置组 → 折叠标志翻转、内置行可见。
        QVERIFY(QMetaObject::invokeMethod(builtinSection, "toggle"));
        QTRY_COMPARE(builtinSection->property("collapsed").toBool(), false);
        QTRY_VERIFY([&] {
            for (QQuickItem* row : rows) {
                const bool isBuiltin =
                    row->property("modelData").toMap().value("source").toString() != "MCP";
                if (isBuiltin && !row->isVisible()) return false;
            }
            return true;
        }());

        // MCP 组：没有服务器时显示引导提示，有工具时不显示。
        auto* hint = win->findChild<QQuickItem*>(QStringLiteral("toolsMcpEmptyHint"));
        QVERIFY(hint);
        QCOMPARE(hint->isVisible(), mcp == 0);

        // ── 搜索：名称/说明子串过滤，命中时自动展开内置组 ──
        auto readInt = [&](const char* fn) {
            QVariant v;
            QMetaObject::invokeMethod(root.get(), fn, Q_RETURN_ARG(QVariant, v));
            return v.toInt();
        };
        QVariant firstNameV;
        QVERIFY(QMetaObject::invokeMethod(root.get(), "firstBuiltinName",
                                          Q_RETURN_ARG(QVariant, firstNameV)));
        const QString firstName = firstNameV.toString();
        QVERIFY(!firstName.isEmpty());

        auto* search = win->findChild<QQuickItem*>(QStringLiteral("toolsSearch"));
        QVERIFY(search);
        const int builtinBefore = readInt("builtinCount");
        search->setProperty("text", firstName);
        QTRY_VERIFY(readInt("builtinCount") >= 1
                    && readInt("builtinCount") < builtinBefore);
        // 折叠状态下搜索会以为没结果，所以命中时必须展开。
        QTRY_COMPARE(builtinSection->property("collapsed").toBool(), false);

        search->setProperty("text", QStringLiteral("zzz-no-such-tool"));
        QTRY_COMPARE(readInt("builtinCount"), 0);
        QTRY_COMPARE(readInt("mcpCount"), 0);
        auto* searchEmpty = win->findChild<QQuickItem*>(
            QStringLiteral("toolsSearchEmpty"));
        QVERIFY(searchEmpty);
        QTRY_VERIFY(searchEmpty->isVisible());

        // 清空搜索恢复完整列表（内置组保持展开，便于继续操作）。
        search->setProperty("text", QString());
        QTRY_COMPARE(readInt("builtinCount"), builtinBefore);
    }
};

QTEST_MAIN(TestToolsPage)
#include "test_tools_page_qml.moc"
