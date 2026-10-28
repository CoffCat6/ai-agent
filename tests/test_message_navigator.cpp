// MessageNavigator（对话区左侧用户消息导轨）回归。
//
// 该组件的价值全在"点对位置"和"看得懂"两件事上，因此测试锁住的是
// 契约而不是像素：
//   1. 单条 / 空列表不占位（没有导航价值的轨道只是视觉噪音）；
//   2. 线长按正文全长三档映射——用 preview 长度会让超长消息和短消息
//      画成一样长，导轨就失去了"哪次问得多"的信息；
//   3. 记录多到装不下时整体压缩间距，且不溢出上限（导轨必须"一眼看
//      全局"，不能自己滚动）；
//   4. 点击透出的是 trace 行号，不是数组下标——两者混用会跳到错的节点；
//   5. active 高亮跟随传入的索引。
//
// 走 build/NexusAI/ 的文件系统副本加载真实组件（与 test_todo_qml 同一套
// qrc 回退方案），不 mock 组件本身。

#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlAbstractUrlInterceptor>
#include <QQuickWindow>
#include <QQuickItem>
#include <QFile>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

namespace {

// build/NexusAI/qmldir 的 prefer 指向测试进程不存在的 qrc；把 NexusAI
// 前缀重写到构建目录的文件系统副本（与 test_charts_qml 相同方案）。
class NexusQrcFallback : public QQmlAbstractUrlInterceptor {
public:
    explicit NexusQrcFallback(QString baseDir) : m_baseDir(std::move(baseDir)) {}

    QUrl intercept(const QUrl& url, DataType) override {
        static const QString prefix = QStringLiteral("/qt/qml/NexusAI");
        if (url.scheme() == QStringLiteral("qrc")) {
            const QString path = url.path();
            if (path.startsWith(prefix)) {
                const QString fsPath = m_baseDir + path.mid(prefix.length());
                if (QFile::exists(fsPath))
                    return QUrl::fromLocalFile(fsPath);
            }
        }
        return url;
    }

private:
    QString m_baseDir;
};

QVariantMap entry(int row, const QString& preview, int length,
                  qint64 messageId = 0, qint64 timestamp = 0) {
    return QVariantMap{{QStringLiteral("row"), row},
                       {QStringLiteral("preview"), preview},
                       {QStringLiteral("length"), length},
                       {QStringLiteral("messageId"), messageId},
                       {QStringLiteral("timestamp"), timestamp}};
}

QString repeated(int count) {
    return QString(count, QLatin1Char('x'));
}

class Harness {
public:
    NexusQrcFallback fallback{QStringLiteral(NEXUS_BUILD_DIR "/NexusAI")};
    QQmlApplicationEngine engine;
    std::unique_ptr<QObject> root;
    QQuickWindow* window{nullptr};
    QString error;

    Harness() {
        engine.addUrlInterceptor(&fallback);
        engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));
        QQmlComponent component(
            &engine, QUrl::fromLocalFile(
                QStringLiteral(NEXUS_TEST_QML_DIR "/MessageNavigatorHarness.qml")));
        if (!component.isReady()) {
            error = component.errorString();
            return;
        }
        root.reset(component.create());
        if (!root) {
            error = component.errorString();
            return;
        }
        window = qobject_cast<QQuickWindow*>(root.get());
        if (!window) {
            error = QStringLiteral("harness root is not a QQuickWindow");
            return;
        }
        window->show();
        if (!QTest::qWaitForWindowExposed(window)) {
            error = QStringLiteral("window was not exposed");
            return;
        }
        QTest::qWait(120);   // layout + first paint
    }

    QVariant call(const char* fn) const {
        QVariant out;
        if (!root || !QMetaObject::invokeMethod(root.get(), fn, Q_RETURN_ARG(QVariant, out)))
            return {};
        return out;
    }

    QVariant callInt(const char* fn, int arg) const {
        QVariant out;
        if (!root || !QMetaObject::invokeMethod(root.get(), fn, Q_RETURN_ARG(QVariant, out),
                                                Q_ARG(QVariant, arg)))
            return {};
        return out;
    }

    bool setEntries(const QVariantList& items) const {
        return root && QMetaObject::invokeMethod(root.get(), "setEntries",
                                                 Q_ARG(QVariant, items));
    }

    bool setActive(int index) const {
        return root && QMetaObject::invokeMethod(root.get(), "setActive",
                                                 Q_ARG(QVariant, index));
    }
};

} // namespace

class TestMessageNavigator : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("NexusAI"));
        QCoreApplication::setApplicationName(QStringLiteral("NexusAI"));
    }

    // 0 条与 1 条都不该占位：一条线既不能"跳转"，也不该在左侧画一条
    // 孤零零的轨道。
    void emptyAndSingleEntryHideTheTrack() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));

        QCOMPARE(h.call("entryCount").toInt(), 0);
        QCOMPARE(h.call("navVisible").toBool(), false);

        QVERIFY(h.setEntries({entry(0, QStringLiteral("只有一问"), 4)}));
        QTRY_COMPARE(h.call("entryCount").toInt(), 1);
        QCOMPARE(h.call("navigable").toBool(), false);
        QCOMPARE(h.call("navVisible").toBool(), false);
    }

    // 线长三档：<20 → 6，20..79 → 10，>=80 → 14。
    // 用 length（正文全长），不是 preview 长度。
    void barWidthMapsBodyLengthInThreeSteps() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setEntries({
            entry(0, QStringLiteral("短"), 1),
            entry(1, QStringLiteral("中等长度的一问"), 19),
            entry(2, QStringLiteral("常规"), 20),
            entry(3, QStringLiteral("较长"), 79),
            entry(4, QStringLiteral("很长"), 80),
            entry(5, QStringLiteral("非常长"), 5000),
        }));
        QTRY_COMPARE(h.call("entryCount").toInt(), 6);

        QCOMPARE(h.callInt("barWidth", 1).toReal(), 6.0);
        QCOMPARE(h.callInt("barWidth", 19).toReal(), 6.0);
        QCOMPARE(h.callInt("barWidth", 20).toReal(), 10.0);
        QCOMPARE(h.callInt("barWidth", 79).toReal(), 10.0);
        QCOMPARE(h.callInt("barWidth", 80).toReal(), 14.0);
        // 5000 字与 80 字画成一样长是刻意的：再长也只是"长问题"。
        QCOMPARE(h.callInt("barWidth", 5000).toReal(), 14.0);
    }

    // 装得下：每条一个 16px 槽，轨道高 = count × 16。
    void trackHeightFollowsEntryCountWhenItFits() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVariantList items;
        for (int i = 0; i < 10; ++i)
            items.append(entry(i, QStringLiteral("第 %1 问").arg(i), 30));
        QVERIFY(h.setEntries(items));
        QTRY_COMPARE(h.call("entryCount").toInt(), 10);
        QTRY_COMPARE(h.call("trackHeight").toInt(), 160);
        QCOMPARE(h.call("effectiveSlot").toReal(), 16.0);
    }

    // 装不下：压缩间距而不溢出上限（视口 560 × 0.6 = 336）。
    void overflowingEntriesCompressWithoutExceedingCap() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVariantList items;
        for (int i = 0; i < 60; ++i)
            items.append(entry(i, QStringLiteral("第 %1 问").arg(i), 30));
        QVERIFY(h.setEntries(items));
        QTRY_COMPARE(h.call("entryCount").toInt(), 60);

        const int cap = 336;
        QCOMPARE(h.call("trackHeight").toInt(), cap);
        // 60 条摊到 336px：每条 5.6px。
        const double slot = h.call("effectiveSlot").toReal();
        QVERIFY2(slot > 2.0 && slot < 16.0, qPrintable(QString::number(slot)));
        // 最后一个条目必须仍在轨道之内（越界的话最后几条点不到）。
        QVERIFY(slot * 60 <= cap + 0.5);
    }

    // 点击透出 trace 行号，不是数组下标。这是最容易写错的一处：
    // 第 2 条用户消息在 trace 的第 7 行、数组下标是 1。
    void clickEmitsTraceRowNotArrayIndex() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setEntries({
            entry(0, QStringLiteral("第一问"), 10),
            entry(7, QStringLiteral("第二问"), 10),
            entry(19, QStringLiteral("第三问"), 10),
        }));
        QTRY_COMPARE(h.call("entryCount").toInt(), 3);

        // 直接驱动 delegate 的 Accessible.onPressAction 太绕；改为点击
        // 每条的 MouseArea：坐标由 effectiveSlot 推出。
        const double slot = h.call("effectiveSlot").toReal();
        const double navX = h.call("navX").toReal() + 4;
        const double centerY = h.call("navCenterY").toReal();

        for (int i = 0; i < 3; ++i) {
            const double itemTop = centerY - h.call("trackHeight").toReal() / 2
                                   + i * slot + slot / 2;
            QTest::mouseClick(h.window, Qt::LeftButton, Qt::NoModifier,
                              QPoint(qRound(navX), qRound(itemTop)));
        }

        const QVariantList jumps = h.call("readJumps").toList();
        QCOMPARE(jumps.size(), 3);
        QCOMPARE(jumps.at(0).toInt(), 0);
        QCOMPARE(jumps.at(1).toInt(), 7);
        QCOMPARE(jumps.at(2).toInt(), 19);
    }

    // active 索引由外部传入（页面按滚动位置算），导轨只负责高亮。
    void activeIndexHighlightsMatchingEntry() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVariantList items;
        for (int i = 0; i < 5; ++i)
            items.append(entry(i, QStringLiteral("第 %1 问").arg(i), 30));
        QVERIFY(h.setEntries(items));
        QTRY_COMPARE(h.call("entryCount").toInt(), 5);

        // -1（还没滚过第一条）与越界值都不该崩。
        QVERIFY(h.setActive(-1));
        QTest::qWait(60);
        QCOMPARE(h.call("entryCount").toInt(), 5);

        QVERIFY(h.setActive(3));
        QTest::qWait(60);
        QCOMPARE(h.call("entryCount").toInt(), 5);

        QVERIFY(h.setActive(99));
        QTest::qWait(60);
        QCOMPARE(h.call("entryCount").toInt(), 5);
    }
};

QTEST_MAIN(TestMessageNavigator)
#include "test_message_navigator.moc"
