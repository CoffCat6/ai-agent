// F1: TodoListPanel 的渲染与状态回归。
//
// 既有测试从不加载该组件；而 todo_write -> AgentVM.todoList 的数据链早已
// 存在，真正缺失的是"用户能否看见"。本用例加载 build/NexusAI/ 文件系统
// 副本里的真实组件（与 test_charts_qml 同一套 qrc 回退机制），锁住：
//   1. 空列表不占位（visible=false、implicitHeight=0）；
//   2. 三种状态与优先级正确映射到 delegate 属性；
//   3. 折叠只隐藏列表、展开恢复；
//   4. 面板确实画出了内容（像素级非背景断言）；
//   5. 头部进度摘要（计数 / 比例 / 进度条几何）；
//   6. 长清单被高度封顶并内部滚动（不会把下方的审阅区挤出面板）；
//   7. 三种状态在视觉上可区分（进行中行有底色与状态词、优先级色条
//      真的按 high/medium/low 上色）。
//
// 同一批组件的第二种宿主：任务清单搬进对话框后（TodoDialog），弹窗必须
// 能打开、行要画在弹窗里、长清单要被封顶在弹窗之内、空清单要有话说，
// 底部渐隐还得收在弹窗列表井的颜色上 —— 那条渐隐以前写死侧栏底色，换个
// 承载面就会在最后几行上盖出一条色带。
//
// 不通过 AgentVM 注入数据：AgentVM 是应用级门面，测试里构造成本高；组件
// 只依赖 todos 数组，直接注入 todo_write 的输出形状即可覆盖展示契约。

#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlAbstractUrlInterceptor>
#include <QQuickWindow>
#include <QQuickItem>
#include <QImage>
#include <QColor>
#include <QFile>
#include <QVariantList>
#include <QVariantMap>

#include <cmath>
#include <memory>

#include "viewmodels/ThemeProvider.h"

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

double colorDistance(const QColor& a, const QColor& b) {
    const double dr = (a.red() - b.red()) / 255.0;
    const double dg = (a.green() - b.green()) / 255.0;
    const double db = (a.blue() - b.blue()) / 255.0;
    return std::sqrt((dr * dr + dg * dg + db * db) / 3.0);
}

int countNonBackground(const QImage& image, const QColor& bg) {
    const QImage argb = image.convertToFormat(QImage::Format_ARGB32);
    int count = 0;
    for (int y = 0; y < argb.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(argb.scanLine(y));
        for (int x = 0; x < argb.width(); ++x)
            if (colorDistance(QColor(line[x]), bg) > 0.03)
                ++count;
    }
    return count;
}

QVariantMap todo(const QString& content, const QString& status, const QString& priority) {
    return QVariantMap{{"content", content}, {"status", status}, {"priority", priority}};
}

// Repeater delegates are visual children but not necessarily QObject children
// of the window, so QObject::findChildren misses them. Walk childItems().
void collectVisualItems(QQuickItem* parent, const QString& objectName,
                        QList<QQuickItem*>* out) {
    const auto children = parent->childItems();
    for (QQuickItem* child : children) {
        if (child->objectName() == objectName)
            out->append(child);
        collectVisualItems(child, objectName, out);
    }
}

// Descendant lookup — used for anchors that live inside one delegate row.
QQuickItem* findChildItem(QQuickItem* parent, const QString& objectName) {
    if (!parent)
        return nullptr;
    const auto children = parent->childItems();
    for (QQuickItem* child : children) {
        if (child->objectName() == objectName)
            return child;
        if (QQuickItem* nested = findChildItem(child, objectName))
            return nested;
    }
    return nullptr;
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
            &engine, QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR "/TodoPanelHarness.qml")));
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
        QTest::qWait(150);   // layout + first paint
    }

    QVariant call(const char* fn) const {
        QVariant out;
        if (!root || !QMetaObject::invokeMethod(root.get(), fn, Q_RETURN_ARG(QVariant, out)))
            return {};
        return out;
    }

    bool setTodos(const QVariantList& items) const {
        return root && QMetaObject::invokeMethod(root.get(), "setTodos",
                                                 Q_ARG(QVariant, items));
    }

    bool setCollapsed(bool value) const {
        return root && QMetaObject::invokeMethod(root.get(), "setCollapsed",
                                                 Q_ARG(QVariant, value));
    }
};

// 对话框夹具：与面板夹具同一套「构建目录文件系统副本 + qrc 回退」方案，
// 但宿主方式不同 —— 弹窗挂在 Overlay.overlay 上（main.qml 的做法），
// 因此它的几何与层级都按真实场景走。
class DialogHarness {
public:
    NexusQrcFallback fallback{QStringLiteral(NEXUS_BUILD_DIR "/NexusAI")};
    QQmlApplicationEngine engine;
    std::unique_ptr<QObject> root;
    QQuickWindow* window{nullptr};
    QString error;

    DialogHarness() {
        engine.addUrlInterceptor(&fallback);
        engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));
        QQmlComponent component(
            &engine, QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR "/TodoDialogHarness.qml")));
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
        QTest::qWait(150);   // layout + first paint
    }

    QVariant call(const char* fn) const {
        QVariant out;
        if (!root || !QMetaObject::invokeMethod(root.get(), fn, Q_RETURN_ARG(QVariant, out)))
            return {};
        return out;
    }

    bool setTodos(const QVariantList& items) const {
        return root && QMetaObject::invokeMethod(root.get(), "setTodos",
                                                 Q_ARG(QVariant, items));
    }

    // 打开弹窗并等入场动画结束 —— 动画期间弹窗带缩放，几何断言会失真。
    bool openDialog() const {
        if (!root || !QMetaObject::invokeMethod(root.get(), "openDialog"))
            return false;
        QTest::qWait(400);
        return true;
    }
};

} // namespace

class TestTodoQml : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("NexusAI"));
        QCoreApplication::setApplicationName(QStringLiteral("NexusAI"));
    }

    void emptyStateTakesNoSpace() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QCOMPARE(h.call("itemCount").toInt(), 0);
        QCOMPARE(h.call("panelVisible").toBool(), false);
        QCOMPARE(h.call("panelHeight").toReal(), 0.0);
    }

    void rendersStatusesAndPriorities() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));

        const QVariantList items{
            todo(QStringLiteral("Inspect code"), QStringLiteral("completed"), QStringLiteral("high")),
            todo(QStringLiteral("Apply fix"), QStringLiteral("in_progress"), QStringLiteral("medium")),
            todo(QStringLiteral("Run tests"), QStringLiteral("pending"), QStringLiteral("low"))
        };
        QVERIFY(h.setTodos(items));

        QTRY_COMPARE(h.call("itemCount").toInt(), 3);
        QTRY_COMPARE(h.call("completedCount").toInt(), 1);
        QTRY_VERIFY(h.call("panelVisible").toBool());
        QTRY_VERIFY(h.call("panelHeight").toReal() > 0);
        QTRY_VERIFY(h.call("listVisible").toBool());
        // Force one frame: QQmlApplicationEngine installs an incubation
        // controller, so delegate creation may be flushed by the render
        // loop rather than by plain event processing.
        QTest::qWait(200);
        (void)h.window->grabWindow();
        QTest::qWait(200);
        QTRY_COMPARE(h.call("renderedCount").toInt(), 3);

        QList<QQuickItem*> delegates;
        collectVisualItems(h.window->contentItem(), QStringLiteral("todoItem"), &delegates);
        QCOMPARE(delegates.size(), 3);

        QMap<QString, QQuickItem*> byContent;
        for (QQuickItem* item : delegates)
            byContent.insert(item->property("content").toString(), item);
        QCOMPARE(byContent.size(), 3);

        QCOMPARE(byContent.value(QStringLiteral("Inspect code"))->property("itemStatus").toString(),
                 QStringLiteral("completed"));
        QCOMPARE(byContent.value(QStringLiteral("Inspect code"))->property("priority").toString(),
                 QStringLiteral("high"));
        QCOMPARE(byContent.value(QStringLiteral("Inspect code"))->property("isCompleted").toBool(), true);

        QCOMPARE(byContent.value(QStringLiteral("Apply fix"))->property("itemStatus").toString(),
                 QStringLiteral("in_progress"));
        QCOMPARE(byContent.value(QStringLiteral("Apply fix"))->property("priority").toString(),
                 QStringLiteral("medium"));
        QCOMPARE(byContent.value(QStringLiteral("Apply fix"))->property("isCompleted").toBool(), false);

        QCOMPARE(byContent.value(QStringLiteral("Run tests"))->property("itemStatus").toString(),
                 QStringLiteral("pending"));
        QCOMPARE(byContent.value(QStringLiteral("Run tests"))->property("priority").toString(),
                 QStringLiteral("low"));
        QCOMPARE(byContent.value(QStringLiteral("Run tests"))->property("isCompleted").toBool(), false);

        // 组件真的画出了内容，而不是"数据有、界面空白"。
        const QColor bg(h.call("backgroundColor").toString());
        QVERIFY(bg.isValid());
        const QImage shot = h.window->grabWindow();
        QVERIFY2(!shot.isNull(), "整窗抓图失败（offscreen 渲染异常）");
        const int visible = countNonBackground(shot, bg);
        qInfo() << "TodoListPanel non-background pixels:" << visible;
        QVERIFY2(visible > 200,
                 qPrintable(QStringLiteral("任务清单渲染为空白（非背景像素 %1）").arg(visible)));
    }

    // 头部进度摘要：计数、比例与进度条几何——不逐行扫读就能看出整体进度。
    void progressSummaryReportsCompletion() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));

        const QVariantList items{
            todo(QStringLiteral("Read the spec"), QStringLiteral("completed"), QStringLiteral("high")),
            todo(QStringLiteral("Write the code"), QStringLiteral("in_progress"), QStringLiteral("medium")),
            todo(QStringLiteral("Run the tests"), QStringLiteral("pending"), QStringLiteral("low")),
            todo(QStringLiteral("Ship it"), QStringLiteral("pending"), QStringLiteral("low"))
        };
        QVERIFY(h.setTodos(items));

        QTRY_COMPARE(h.call("itemCount").toInt(), 4);
        QTRY_COMPARE(h.call("completedCount").toInt(), 1);
        QTRY_COMPARE(h.call("inProgressCount").toInt(), 1);
        QTRY_COMPARE(h.call("pendingCount").toInt(), 2);
        QTRY_VERIFY(!h.call("allCompleted").toBool());
        QTRY_COMPARE(h.call("progressRatio").toDouble(), 0.25);

        // 状态汇总行把计数写成词："进行中 1 · 待处理 2"。
        const QString summary = h.call("stateSummary").toString();
        QVERIFY2(summary.contains(QString::fromUtf8("进行中 1")),
                 qPrintable(QStringLiteral("状态汇总缺少进行中计数：%1").arg(summary)));
        QVERIFY2(summary.contains(QString::fromUtf8("待处理 2")),
                 qPrintable(QStringLiteral("状态汇总缺少待处理计数：%1").arg(summary)));

        // 进度条：填充宽度 = 轨道宽度 × 完成比例（宽度有动画，等它落定）。
        QList<QQuickItem*> tracks;
        collectVisualItems(h.window->contentItem(), QStringLiteral("todoProgressTrack"), &tracks);
        QCOMPARE(tracks.size(), 1);
        QList<QQuickItem*> fills;
        collectVisualItems(h.window->contentItem(), QStringLiteral("todoProgressFill"), &fills);
        QCOMPARE(fills.size(), 1);
        QQuickItem* track = tracks.first();
        QQuickItem* fill = fills.first();
        QTRY_VERIFY(track->width() > 0);
        QTRY_VERIFY2(qAbs(fill->width() - track->width() * 0.25) < 1.0,
                     qPrintable(QStringLiteral("进度条填充 %1 与轨道 %2 不匹配")
                                    .arg(fill->width()).arg(track->width())));

        // 全部完成时给一个明确的收尾态。
        QVERIFY(h.setTodos({todo(QStringLiteral("Only one"), QStringLiteral("completed"),
                                  QStringLiteral("low"))}));
        QTRY_VERIFY(h.call("allCompleted").toBool());
        QTRY_COMPARE(h.call("progressRatio").toDouble(), 1.0);
    }

    // 长清单必须被高度封顶并内部滚动，否则会把下方的审阅/分区挤出面板。
    void longListIsCappedAndScrolls() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));

        QVariantList items;
        for (int i = 0; i < 25; ++i) {
            items.append(todo(QStringLiteral("Task %1").arg(i + 1),
                              i == 0 ? QStringLiteral("completed") : QStringLiteral("pending"),
                              QStringLiteral("low")));
        }
        QVERIFY(h.setTodos(items));

        QTRY_COMPARE(h.call("itemCount").toInt(), 25);
        QTRY_VERIFY(h.call("panelVisible").toBool());

        // 视口被封顶在组件自带的 200px 上限内。
        QCOMPARE(h.call("listHeightCeiling").toInt(), 200);
        QCOMPARE(h.call("listMaxHeight").toInt(), 200);
        QTRY_COMPARE(h.call("listViewportHeight").toReal(), 200.0);
        QVERIFY(h.call("listContentHeight").toReal() > h.call("listViewportHeight").toReal());
        QTRY_VERIFY(h.call("listScrollable").toBool());
        // 渐隐提示：告诉用户下面还有内容。
        QTRY_VERIFY(h.call("moreBelowFold").toBool());

        // 面板总高 = 固定头部 + 封顶后的视口，下方的分区仍有余量。
        const qreal panelHeight = h.call("panelHeight").toReal();
        const qreal chrome = h.call("chromeHeight").toReal();
        QVERIFY(chrome > 0);
        QVERIFY2(qAbs(panelHeight - (chrome + 200.0)) < 2.0,
                 qPrintable(QStringLiteral("面板高度 %1 与（头部 %2 + 视口 200）不符")
                                .arg(panelHeight).arg(chrome)));
    }

    // 三种状态必须在视觉上可区分：进行中行带底色与状态词，
    // 优先级色条按 high/medium/low 上不同的颜色。
    void activeRowAndPriorityRailAreDistinct() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));

        const QVariantList items{
            todo(QStringLiteral("Done"), QStringLiteral("completed"), QStringLiteral("high")),
            todo(QStringLiteral("Running"), QStringLiteral("in_progress"), QStringLiteral("medium")),
            todo(QStringLiteral("Queued"), QStringLiteral("pending"), QStringLiteral("low"))
        };
        QVERIFY(h.setTodos(items));
        QTRY_COMPARE(h.call("renderedCount").toInt(), 3);
        QTest::qWait(200);
        (void)h.window->grabWindow();
        QTest::qWait(200);

        QList<QQuickItem*> delegates;
        collectVisualItems(h.window->contentItem(), QStringLiteral("todoItem"), &delegates);
        QCOMPARE(delegates.size(), 3);
        QMap<QString, QQuickItem*> byContent;
        for (QQuickItem* item : delegates)
            byContent.insert(item->property("content").toString(), item);

        QQuickItem* running = byContent.value(QStringLiteral("Running"));
        QVERIFY(running);
        QVERIFY(running->property("isInProgress").toBool());
        QVERIFY(byContent.value(QStringLiteral("Done"))->property("isCompleted").toBool());
        QVERIFY(byContent.value(QStringLiteral("Queued"))->property("isPending").toBool());

        // 进行中：带品牌色底纹 + 可见的状态词。
        QQuickItem* runningTint = findChildItem(running, QStringLiteral("todoActiveTint"));
        QVERIFY(runningTint);
        const QColor tint = runningTint->property("color").value<QColor>();
        QVERIFY2(tint.alpha() > 0, "进行中的行没有底色，扫读时找不出来");
        QQuickItem* doneTint = findChildItem(byContent.value(QStringLiteral("Done")),
                                         QStringLiteral("todoActiveTint"));
        QVERIFY(doneTint);
        QCOMPARE(doneTint->property("color").value<QColor>().alpha(), 0);

        QQuickItem* runningLabel = findChildItem(running, QStringLiteral("todoStateLabel"));
        QVERIFY(runningLabel);
        QVERIFY(runningLabel->property("visible").toBool());
        QVERIFY(!runningLabel->property("text").toString().isEmpty());
        QQuickItem* queuedLabel = findChildItem(byContent.value(QStringLiteral("Queued")),
                                            QStringLiteral("todoStateLabel"));
        QVERIFY(queuedLabel);
        QVERIFY(!queuedLabel->property("visible").toBool());

        // 优先级色条：high=红 / medium=琥珀 / low=弱化灰。
        NexusAI::ThemeProvider* theme = NexusAI::ThemeProvider::instance();
        QQuickItem* highRail = findChildItem(byContent.value(QStringLiteral("Done")),
                                         QStringLiteral("todoPriorityRail"));
        QQuickItem* mediumRail = findChildItem(byContent.value(QStringLiteral("Running")),
                                           QStringLiteral("todoPriorityRail"));
        QQuickItem* lowRail = findChildItem(byContent.value(QStringLiteral("Queued")),
                                        QStringLiteral("todoPriorityRail"));
        QVERIFY(highRail && mediumRail && lowRail);
        QCOMPARE(highRail->property("color").value<QColor>(), theme->statusDanger());
        QCOMPARE(mediumRail->property("color").value<QColor>(), theme->statusWarning());
        const QColor lowColor = lowRail->property("color").value<QColor>();
        QVERIFY(lowColor.alpha() < 255);
        QVERIFY(lowColor != theme->statusDanger());
        QVERIFY(lowColor != theme->statusWarning());
    }

    void collapsesAndExpands() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        const QVariantList items{
            todo(QStringLiteral("One"), QStringLiteral("pending"), QStringLiteral("low")),
            todo(QStringLiteral("Two"), QStringLiteral("in_progress"), QStringLiteral("medium"))
        };
        QVERIFY(h.setTodos(items));
        QTRY_VERIFY(h.call("panelVisible").toBool());
        QTRY_VERIFY(h.call("panelHeight").toReal() > 0);
        QTest::qWait(150);
        (void)h.window->grabWindow();   // flush delegate incubation
        QTest::qWait(150);
        const qreal expanded = h.call("panelHeight").toReal();
        QVERIFY(expanded > 22);         // header + at least one row

        QVERIFY(h.setCollapsed(true));
        QTRY_COMPARE(h.call("listVisible").toBool(), false);
        QTRY_VERIFY(h.call("panelHeight").toReal() < expanded);
        // 折叠后进度摘要仍在（这正是"折叠成一行摘要"的意义）。
        QTRY_COMPARE(h.call("progressRatio").toDouble(), 0.0);
        QTRY_COMPARE(h.call("listViewportHeight").toReal(), 0.0);
        QVERIFY(h.call("panelHeight").toReal() > 0);

        QVERIFY(h.setCollapsed(false));
        QTRY_COMPARE(h.call("listVisible").toBool(), true);
        QTRY_COMPARE(h.call("panelHeight").toReal(), expanded);
    }

    void clearingHidesPanel() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setTodos({todo(QStringLiteral("One"), QStringLiteral("pending"),
                                 QStringLiteral("low"))}));
        QTRY_VERIFY(h.call("panelVisible").toBool());

        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "clearTodos"));
        QTRY_COMPARE(h.call("itemCount").toInt(), 0);
        QCOMPARE(h.call("panelVisible").toBool(), false);
        QCOMPARE(h.call("panelHeight").toReal(), 0.0);
    }

    // ── TodoDialog ──────────────────────────────────────
    // 任务清单搬进对话框后的回归。侧栏里的那份不再是唯一入口：弹窗必须
    // 能打开、把行画在自己肚子里、把长清单关在自己的框里、空的时候说
    // 清楚为什么空，底部渐隐还得收在弹窗列表井的颜色上（写死侧栏底色会
    // 在最后几行上盖出一条色带）。

    // 弹窗打开后，行确实渲染在弹窗里，标题直接给出进度。
    void dialogOpensWithListAndProgress() {
        DialogHarness h;
        QVERIFY2(h.window, qPrintable(h.error));

        const QVariantList items{
            todo(QStringLiteral("Inspect code"), QStringLiteral("completed"), QStringLiteral("high")),
            todo(QStringLiteral("Apply fix"), QStringLiteral("in_progress"), QStringLiteral("medium")),
            todo(QStringLiteral("Run tests"), QStringLiteral("pending"), QStringLiteral("low"))
        };
        QVERIFY(h.setTodos(items));
        QVERIFY(h.openDialog());
        QVERIFY(h.call("isOpen").toBool());

        QCOMPARE(h.call("itemCount").toInt(), 3);
        QCOMPARE(h.call("completedCount").toInt(), 1);
        QCOMPARE(h.call("inProgressCount").toInt(), 1);
        QCOMPARE(h.call("pendingCount").toInt(), 1);
        // 顶栏角标要的数：还没做完的（含正在跑的那一个）。
        QCOMPARE(h.call("unfinishedCount").toInt(), 2);
        QTRY_COMPARE(h.call("renderedCount").toInt(), 3);

        // 行画在弹窗里（不是画在别处，也不是一片空白）。
        QList<QQuickItem*> delegates;
        collectVisualItems(h.window->contentItem(), QStringLiteral("todoItem"), &delegates);
        QCOMPARE(delegates.size(), 3);

        // 标题即进度："3/7 已完成" 那一类的信息在标题栏就有。
        QQuickItem* title = findChildItem(h.window->contentItem(),
                                          QStringLiteral("todoDialogTitle"));
        QVERIFY(title);
        const QString titleText = title->property("text").toString();
        QVERIFY2(titleText.contains(QStringLiteral("1/3")),
                 qPrintable(QStringLiteral("标题里没有进度：%1").arg(titleText)));
    }

    // 长清单必须被封顶在弹窗里，既不顶出对话框，也不把列表井撑破。
    void dialogCapsListInsideItsBounds() {
        DialogHarness h;
        QVERIFY2(h.window, qPrintable(h.error));

        QVariantList items;
        for (int i = 0; i < 25; ++i) {
            items.append(todo(QStringLiteral("Task %1").arg(i + 1),
                              QStringLiteral("pending"), QStringLiteral("low")));
        }
        QVERIFY(h.setTodos(items));
        QVERIFY(h.openDialog());

        const int cap = h.call("listHeightCap").toInt();
        QVERIFY2(cap > 0, "对话框没有给列表任何高度预算");
        const qreal viewport = h.call("listViewportHeight").toReal();
        QVERIFY2(viewport > 0, "列表视口塌成了 0 高度");
        QVERIFY2(viewport <= cap + 1.0,
                 qPrintable(QStringLiteral("视口 %1 超过封顶 %2").arg(viewport).arg(cap)));
        // 内容比视口高 → 内部滚动，底部渐隐提示下面还有。
        QVERIFY(h.call("listContentHeight").toReal() > viewport);
        QVERIFY(h.call("listScrollable").toBool());
        QVERIFY(h.call("moreBelowFold").toBool());

        // 面板整块待在列表井里。
        QQuickItem* well = findChildItem(h.window->contentItem(),
                                         QStringLiteral("todoDialogWell"));
        QQuickItem* panel = findChildItem(h.window->contentItem(),
                                          QStringLiteral("todoDialogPanel"));
        QVERIFY(well);
        QVERIFY(panel);
        const QPointF wellBottom = well->mapToScene(QPointF(0, well->height()));
        const QPointF panelBottom = panel->mapToScene(QPointF(0, panel->height()));
        QVERIFY2(panelBottom.y() <= wellBottom.y() + 0.5,
                 qPrintable(QStringLiteral("面板底部 %1 超出列表井底部 %2")
                                .arg(panelBottom.y()).arg(wellBottom.y())));

        // 列表井整体在窗口之内（没有溢出到窗口外）。
        const QPointF wellTop = well->mapToScene(QPointF(0, 0));
        QVERIFY2(wellTop.x() >= 0 && wellTop.y() >= 0,
                 qPrintable(QStringLiteral("列表井左上角跑到窗口外：%1,%2")
                                .arg(wellTop.x()).arg(wellTop.y())));
        QVERIFY2(wellBottom.x() <= h.window->width() && wellBottom.y() <= h.window->height(),
                 qPrintable(QStringLiteral("列表井右下角跑出窗口：%1,%2")
                                .arg(wellBottom.x()).arg(wellBottom.y())));
    }

    // 空清单时说清楚"这里本来会有什么"，而不是给一个空对话框。
    void dialogShowsEmptyState() {
        DialogHarness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setTodos({}));
        QVERIFY(h.openDialog());

        QVERIFY(!h.call("hasItems").toBool());
        QCOMPARE(h.call("itemCount").toInt(), 0);
        QCOMPARE(h.call("unfinishedCount").toInt(), 0);

        QQuickItem* empty = findChildItem(h.window->contentItem(),
                                          QStringLiteral("todoDialogEmptyState"));
        QVERIFY(empty);
        QVERIFY2(empty->property("visible").toBool(), "空态没有显示");

        // 有任务时列表井可见、空态隐藏；没有任务时反过来。
        QQuickItem* well = findChildItem(h.window->contentItem(),
                                         QStringLiteral("todoDialogWell"));
        QVERIFY(well);
        QVERIFY(!well->property("visible").toBool());

        // 空清单的标题不该报 "0/0 已完成"。
        QQuickItem* title = findChildItem(h.window->contentItem(),
                                          QStringLiteral("todoDialogTitle"));
        QVERIFY(title);
        const QString titleText = title->property("text").toString();
        QVERIFY2(!titleText.contains(QStringLiteral("0/0")),
                 qPrintable(QStringLiteral("空清单的标题不该报进度：%1").arg(titleText)));
    }

    // 渐隐必须收在承载它的表面（列表井）的颜色上。
    void dialogFadeEndsInListSurface() {
        DialogHarness h;
        QVERIFY2(h.window, qPrintable(h.error));

        QVariantList items;
        for (int i = 0; i < 25; ++i) {
            items.append(todo(QStringLiteral("Task %1").arg(i + 1),
                              QStringLiteral("pending"), QStringLiteral("low")));
        }
        QVERIFY(h.setTodos(items));
        QVERIFY(h.openDialog());
        QVERIFY(h.call("moreBelowFold").toBool());

        const QColor surface = h.call("surfaceColor").value<QColor>();
        const QColor fadeEnd = h.call("fadeEndColor").value<QColor>();
        QVERIFY(surface.isValid());
        QVERIFY(fadeEnd.isValid());
        QCOMPARE(fadeEnd, surface);

        // 旧实现写死侧栏底色：换一个承载面就会在最后几行上盖出色带。
        NexusAI::ThemeProvider* theme = NexusAI::ThemeProvider::instance();
        QVERIFY2(fadeEnd != theme->bgSecondary(),
                 "底部渐隐仍写死侧栏底色，换承载面就会出现色带");

        // 渐隐本身挂在列表视口底部，并且确实可见。
        QQuickItem* fade = findChildItem(h.window->contentItem(),
                                         QStringLiteral("todoBottomFade"));
        QVERIFY(fade);
        QVERIFY(fade->property("visible").toBool());
    }
};

QTEST_MAIN(TestTodoQml)
#include "test_todo_qml.moc"
