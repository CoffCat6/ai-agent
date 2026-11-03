// docs/35 P4.3: BackgroundTasksDialog 渲染回归。
//
// 面板是后台子代理唯一的管理面：数据链（hub → AgentVM → QML）已有单元
// 测试，这里锁住"用户看得见"——空态、行数、运行中/已完成的状态文案与
// 「清除已结束」入口。与 test_todo_qml 共用「build/NexusAI 文件系统副本 +
// qrc 回退拦截器 + offscreen」方案。
#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlAbstractUrlInterceptor>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QFile>
#include <QVariantList>
#include <QVariantMap>

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
                if (QFile::exists(fsPath))
                    return QUrl::fromLocalFile(fsPath);
            }
        }
        return url;
    }

private:
    QString m_baseDir;
};

// 弹窗动作绑定 AgentVM；测试只需要这些入口存在，不做真实取消。
class StubAgentVM : public QObject {
    Q_OBJECT
public:
    Q_INVOKABLE bool cancelBackgroundSubAgent(const QString& id)
    { ++cancels; cancelledId = id; return true; }
    Q_INVOKABLE int cancelAllBackgroundSubAgents() { ++cancelsAll; return 1; }
    Q_INVOKABLE QString backgroundSubAgentReport(const QString& id)
    { lastReportId = id; return QStringLiteral("report for %1").arg(id); }
    Q_INVOKABLE void dismissFinishedBackgroundSubAgents() { ++dismissals; }

    int cancels{0};
    int cancelsAll{0};
    int dismissals{0};
    QString cancelledId;
    QString lastReportId;
};

QQuickItem* findChildItem(QQuickItem* root, const QString& objectName) {
    if (!root) return nullptr;
    if (root->objectName() == objectName) return root;
    for (QQuickItem* child : root->childItems()) {
        if (QQuickItem* hit = findChildItem(child, objectName))
            return hit;
    }
    return nullptr;
}

QVariantMap task(const QString& id, const QString& goal, const QString& state,
                 qint64 startedAt, int steps, int proposals) {
    QVariantMap map;
    map["id"] = id;
    map["goal"] = goal;
    map["role"] = "explore";
    map["state"] = state;
    map["startedAt"] = startedAt;
    map["elapsedMs"] = 1200;
    map["steps"] = steps;
    map["proposalCount"] = proposals;
    map["progressTail"] = QVariantList{};
    map["error"] = "";
    return map;
}

class BackgroundDialogHarness {
public:
    NexusQrcFallback fallback{QStringLiteral(NEXUS_BUILD_DIR "/NexusAI")};
    StubAgentVM stub;
    QQmlApplicationEngine engine;
    std::unique_ptr<QObject> root;
    QQuickWindow* window{nullptr};
    QString error;

    BackgroundDialogHarness() {
        engine.addUrlInterceptor(&fallback);
        engine.rootContext()->setContextProperty(QStringLiteral("AgentVM"), &stub);
        engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));
        QQmlComponent component(
            &engine,
            QUrl::fromLocalFile(QStringLiteral(
                NEXUS_TEST_QML_DIR "/BackgroundTasksDialogHarness.qml")));
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
        QTest::qWait(150);
        if (!injectAgent(&stub))
            error = QStringLiteral("failed to inject the stub façade");
    }

    // 组件在 NexusAI 模块内预编译，`AgentVM` 编译期绑定到 QML_SINGLETON
    // 类型，context property 遮蔽不了它——桩只能经组件的 agentFacade 注入。
    bool injectAgent(QObject* agent) const {
        return root
            && QMetaObject::invokeMethod(
                root.get(), "setInjectedAgent",
                Q_ARG(QVariant, QVariant::fromValue<QObject*>(agent)));
    }

    // 点真实像素：覆盖 MouseArea 命中、布局遮挡与绑定求值。
    bool click(const QString& objectName) const {
        QQuickItem* item = findChildItem(window->contentItem(), objectName);
        if (!item || !item->isVisible() || !item->isEnabled())
            return false;
        QTest::mouseClick(
            window, Qt::LeftButton, Qt::NoModifier,
            item->mapToScene(QPointF(item->width() / 2, item->height() / 2))
                .toPoint());
        return true;
    }

    bool setTasks(const QVariantList& tasks) const {
        return root && QMetaObject::invokeMethod(root.get(), "setTasks",
                                                 Q_ARG(QVariant, tasks));
    }
    bool openDialog() const {
        QVariant out;
        return root && QMetaObject::invokeMethod(
                   root.get(), "openDialog", Q_RETURN_ARG(QVariant, out))
            && out.toBool();
    }
};

} // namespace

class TestBackgroundTasksQml : public QObject {
    Q_OBJECT

private slots:
    void emptyStateExplainsWhereTasksComeFrom() {
        BackgroundDialogHarness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setTasks({}));
        QVERIFY(h.openDialog());

        QQuickItem* empty = findChildItem(h.window->contentItem(),
                                          QStringLiteral("backgroundTasksEmpty"));
        QVERIFY(empty);
        QVERIFY2(empty->property("visible").toBool(), "空态没有显示");
        QQuickItem* list = findChildItem(h.window->contentItem(),
                                         QStringLiteral("backgroundTasksList"));
        QVERIFY(list);
        QVERIFY2(!list->property("visible").toBool(), "空列表不应占据视口");
    }

    void rowsShowStateAndClearFinished() {
        BackgroundDialogHarness h;
        QVERIFY2(h.window, qPrintable(h.error));
        const qint64 now = 1000;
        const QVariantList tasks{
            task(QStringLiteral("sa-1"), QStringLiteral("scan the tree"),
                 QStringLiteral("running"), now, 3, 0),
            task(QStringLiteral("sa-2"), QStringLiteral("draft the change"),
                 QStringLiteral("completed"), now, 4, 2)};
        QVERIFY(h.setTasks(tasks));
        QVERIFY(h.openDialog());

        QQuickItem* list = findChildItem(h.window->contentItem(),
                                         QStringLiteral("backgroundTasksList"));
        QVERIFY(list);
        QVERIFY2(list->property("visible").toBool(), "任务列表不可见");
        QTRY_COMPARE(list->property("count").toInt(), 2);

        QQuickItem* title = findChildItem(h.window->contentItem(),
                                          QStringLiteral("backgroundTasksTitle"));
        QVERIFY(title);
        const QString titleText = title->property("text").toString();
        QVERIFY2(titleText.contains(QStringLiteral("后台子代理")), qPrintable(titleText));

        // 已完成任务存在 → 「清除已结束」入口出现。
        QQuickItem* clear = findChildItem(
            h.window->contentItem(),
            QStringLiteral("backgroundTasksClearFinished"));
        QVERIFY(clear);
        QVERIFY2(clear->property("visible").toBool(),
                 "有已结束任务时却没有清除入口");
    }

    // docs/35 P4.3/P4.5（2026-11-03 两栏改版）：面板按钮必须真的调到门面，
// 且**实时更新不得打断点击**。旧实现把 ListView 直接绑到整表替换的
// QVariantList：hub 每次 250ms 刷新都会重建 delegate，用户按下停止后
// 释放前按钮被销毁（点了没反应），展开的「查看」也会立刻被收回。这里
// 连续刷新后再点像素，锁住本地 ListModel 按 id 合并的修复。
    void detailSelectionShowsLiveStatsAndSurvivesUpdates() {
        BackgroundDialogHarness h;
        QVERIFY2(h.window, qPrintable(h.error));
        const qint64 now = 1000;
        QVariantMap running = task(QStringLiteral("sa-1"),
                                   QStringLiteral("scan the tree"),
                                   QStringLiteral("running"), now, 3, 0);
        running["progressTail"] = QVariantList{
            QVariantMap{{QStringLiteral("phase"), QStringLiteral("thinking")},
                        {QStringLiteral("text"), QStringLiteral("step 1")}},
            QVariantMap{{QStringLiteral("phase"), QStringLiteral("tool")},
                        {QStringLiteral("text"),
                         QStringLiteral("web_search(...) ⇒ ok")}}};
        const QVariantMap queued = task(QStringLiteral("sa-2"),
                                        QStringLiteral("wait for a slot"),
                                        QStringLiteral("queued"), now, 0, 0);
        // hub 状态仍是 running，但 Runner 在 acquireSlot 里排队（P4.5）。
        QVariantMap slotQueued = task(QStringLiteral("sa-4"),
                                      QStringLiteral("also waiting"),
                                      QStringLiteral("running"), now, 0, 0);
        slotQueued["slotQueued"] = true;
        const QVariantMap done = task(QStringLiteral("sa-3"),
                                      QStringLiteral("draft the change"),
                                      QStringLiteral("completed"), now, 4, 2);
        QVariantList tasks{running, queued, slotQueued, done};
        QVERIFY(h.setTasks(tasks));
        QVERIFY(h.openDialog());
        QTest::qWait(150);

        const auto textOf = [&](const QString& name) -> QString {
            QQuickItem* item = findChildItem(h.window->contentItem(), name);
            return item ? item->property("text").toString() : QString();
        };
        const auto visible = [&](const QString& name) -> bool {
            QQuickItem* item = findChildItem(h.window->contentItem(), name);
            return item && item->isVisible();
        };

        QQuickItem* list = findChildItem(h.window->contentItem(),
                                         QStringLiteral("backgroundTasksList"));
        QVERIFY(list);
        QTRY_COMPARE(list->property("count").toInt(), 4);

        // 实时统计：运行中 1、排队 2（含等待槽位）、已结束 1。
        QCOMPARE(textOf(QStringLiteral("backgroundTasksRunningCount")),
                 QStringLiteral("进行中 1"));
        QCOMPARE(textOf(QStringLiteral("backgroundTasksQueuedCount")),
                 QStringLiteral("排队 2"));
        QCOMPARE(textOf(QStringLiteral("backgroundTasksFinishedCount")),
                 QStringLiteral("已结束 1"));
        QVERIFY(textOf(QStringLiteral("backgroundTasksTitle"))
                    .contains(QStringLiteral("1 进行中")));

        // 自动选中运行中的任务，运行中即可看到实时详情（不依赖最终报告）。
        QTRY_COMPARE(textOf(QStringLiteral("backgroundTaskDetailTitle")),
                     QStringLiteral("explore · 运行中"));
        QCOMPARE(textOf(QStringLiteral("backgroundTaskDetailSteps")),
                 QStringLiteral("3"));
        QQuickItem* progress = findChildItem(
            h.window->contentItem(),
            QStringLiteral("backgroundTaskDetailProgress"));
        QVERIFY(progress);
        QTRY_COMPARE(progress->property("count").toInt(), 2);

        // 模拟 hub 的连续实时刷新（250ms）：行/选择/进度都不能被打断。
        for (int i = 0; i < 6; ++i) {
            running["steps"] = 3 + i;
            running["progressTail"] = QVariantList{
                QVariantMap{{QStringLiteral("phase"), QStringLiteral("thinking")},
                            {QStringLiteral("text"),
                             QStringLiteral("step %1").arg(4 + i)}}};
            QVERIFY(h.setTasks(QVariantList{running, queued, slotQueued, done}));
        }
        QCOMPARE(textOf(QStringLiteral("backgroundTaskDetailTitle")),
                 QStringLiteral("explore · 运行中"));
        QCOMPARE(textOf(QStringLiteral("backgroundTaskDetailSteps")),
                 QStringLiteral("8"));
        QTRY_COMPARE(progress->property("count").toInt(), 1);

        // 刷新之后再点停止——旧实现在这里按下的按钮会被重建掉。
        QVERIFY2(h.click(QStringLiteral("backgroundTaskDetailStop")),
                 "实时刷新后停止按钮不可点击");
        QCOMPARE(h.stub.cancels, 1);
        QCOMPARE(h.stub.cancelledId, QStringLiteral("sa-1"));

        // 点左侧已完成任务 → 右栏立即取完整报告。
        QVERIFY2(h.click(QStringLiteral("backgroundTaskRow_sa-3")),
                 "已完成任务行不可点击");
        QTRY_COMPARE(h.stub.lastReportId, QStringLiteral("sa-3"));
        QCOMPARE(textOf(QStringLiteral("backgroundTaskDetailTitle")),
                 QStringLiteral("explore · 已完成"));
        QQuickItem* report = findChildItem(
            h.window->contentItem(),
            QStringLiteral("backgroundTaskDetailReport"));
        QVERIFY(report);
        QVERIFY(report->isVisible());
        QCOMPARE(report->property("text").toString(),
                 QStringLiteral("report for sa-3"));

        // 回到运行中的任务：详情停止按钮仍在。
        QVERIFY(h.click(QStringLiteral("backgroundTaskRow_sa-1")));
        QTRY_VERIFY(visible(QStringLiteral("backgroundTaskDetailStop")));

        // hub 收到取消请求后 cancelling=true：面板立刻显示「正在停止…」，
        // 停止按钮消失（避免重复点击），任务仍计入进行中。
        running["cancelling"] = true;
        QVERIFY(h.setTasks(QVariantList{running, queued, slotQueued, done}));
        QTRY_VERIFY(visible(QStringLiteral("backgroundTaskDetailStopping")));
        QVERIFY(!visible(QStringLiteral("backgroundTaskDetailStop")));
        QCOMPARE(textOf(QStringLiteral("backgroundTasksRunningCount")),
                 QStringLiteral("进行中 1"));

        QVERIFY2(h.click(QStringLiteral("backgroundTasksClearFinished")),
                 "清除已结束按钮不可点击");
        QCOMPARE(h.stub.dismissals, 1);
        QVERIFY2(h.click(QStringLiteral("backgroundTasksCancelAll")),
                 "全部停止按钮不可点击");
        QCOMPARE(h.stub.cancelsAll, 1);
    }
};

QTEST_MAIN(TestBackgroundTasksQml)
#include "test_background_tasks_qml.moc"