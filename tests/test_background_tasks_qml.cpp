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
#include <functional>

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
    // docs/43 O4-2: 失败任务的一键重试入口。
    Q_INVOKABLE bool retryBackgroundSubAgent(const QString& id)
    { ++retries; retriedId = id; return true; }
    // docs/43 O4-3: 详情区 findings 按需查询。
    Q_INVOKABLE QVariantList backgroundSubAgentFindings(const QString& id)
    {
        if (id == QLatin1String("sa-1")) {
            return QVariantList{
                QVariantMap{{QStringLiteral("severity"), QStringLiteral("high")},
                            {QStringLiteral("path"), QStringLiteral("src/a.cpp")},
                            {QStringLiteral("line"), 12},
                            {QStringLiteral("message"),
                             QStringLiteral("null deref after the check")}},
                QVariantMap{{QStringLiteral("severity"), QStringLiteral("low")},
                            {QStringLiteral("path"), QStringLiteral("src/b.cpp")},
                            {QStringLiteral("line"), 40},
                            {QStringLiteral("message"),
                             QStringLiteral("stale comment")}}};
        }
        return QVariantList{};
    }

    int cancels{0};
    int cancelsAll{0};
    int dismissals{0};
    int retries{0};
    QString cancelledId;
    QString lastReportId;
    QString retriedId;
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
    // docs/43 O4: finished runs carry cost/findings; running ones stay empty.
    const bool done = state == QLatin1String("completed");
    map["tokensUsed"] = done ? 1200 : 0;
    map["usage"] = done
        ? QVariantMap{{QStringLiteral("input"), 1000},
                      {QStringLiteral("output"), 200},
                      {QStringLiteral("cached"), 0},
                      {QStringLiteral("reported"), true},
                      {QStringLiteral("model"), QStringLiteral("test-model")}}
        : QVariantMap{};
    map["findingCount"] = done ? 2 : 0;
    map["stopCode"] = done ? QStringLiteral("completed") : QString();
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
        // 右栏高度必须真正分给进度区（曾因进度/报告同时 fillHeight 被挤成 0）。
        QVERIFY2(progress->height() > 40, qPrintable(QString::number(progress->height())));
        QVERIFY(progress->width() > 100);
        QQuickItem* progressPage = findChildItem(
            h.window->contentItem(),
            QStringLiteral("backgroundTaskProgressPage"));
        QVERIFY(progressPage);
        QTRY_VERIFY(progressPage->height() > 100);

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
        // 页签切换后的几何更新在下一轮布局之后，先等它落定；滚动的
        // 是 Flickable 本身（Text 只是 contentItem 里的内容，单行高度很小）。
        QQuickItem* page = findChildItem(
            h.window->contentItem(),
            QStringLiteral("backgroundTaskReportPage"));
        QVERIFY(page);
        QTRY_VERIFY(page->height() > 100);
        QVERIFY(report->width() > 100);
        QVERIFY(report->implicitHeight() > 0);
        QCOMPARE(report->property("text").toString(),
                 QStringLiteral("report for sa-3"));

        // 回到运行中的任务：详情停止按钮仍在。
        QVERIFY(h.click(QStringLiteral("backgroundTaskRow_sa-1")));
        QTRY_VERIFY(visible(QStringLiteral("backgroundTaskDetailStop")));
        // 运行中自动回到「实时进度」页签，进度区仍可滚动。
        QTRY_VERIFY(visible(QStringLiteral("backgroundTaskDetailProgress")));
        QVERIFY(progress->height() > 40);

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
// docs/43 O4: a finished run exposes cost and finding count in the row and
    // the classified usage (model/input/output) in the detail pane.
    void finishedTaskShowsCostAndFindings() {
        BackgroundDialogHarness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setTasks({task(QStringLiteral("sa-1"),
                                 QStringLiteral("review the patch"),
                                 QStringLiteral("completed"), 1000, 4, 0)}));
        QVERIFY(h.openDialog());
        QTest::qWait(150);

        const auto itemOf = [&](const QString& name) -> QQuickItem* {
            return findChildItem(h.window->contentItem(), name);
        };
        QQuickItem* tokens = itemOf(QStringLiteral("backgroundTaskRowTokens_sa-1"));
        QVERIFY2(tokens, "行内 tokens 不可见");
        QVERIFY(tokens->isVisible());
        QVERIFY2(tokens->property("text").toString().contains(
                     QStringLiteral("tokens 1200")),
                 qPrintable(tokens->property("text").toString()));

        QQuickItem* findings = itemOf(QStringLiteral("backgroundTaskRowFindings_sa-1"));
        QVERIFY2(findings, "行内结论数不可见");
        QVERIFY(findings->isVisible());
        QVERIFY(findings->property("text").toString().contains(
            QStringLiteral("2 条结论")));

        QQuickItem* usage = itemOf(QStringLiteral("backgroundTaskDetailUsage"));
        QVERIFY2(usage, "详情用量行缺失");
        QVERIFY(usage->isVisible());
        const QString usageText = usage->property("text").toString();
        QVERIFY2(usageText.contains(QStringLiteral("test-model"))
                     && usageText.contains(QStringLiteral("输入 1000")),
                 qPrintable(usageText));
    }
// docs/43 O4-2: failed/cancelled 终态任务的详情区出现「重试」，点击把任务
// id 交给门面；completed 任务不给重试入口。
    void failedTaskShowsRetryButton() {
        BackgroundDialogHarness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setTasks({task(QStringLiteral("sa-1"),
                                 QStringLiteral("survey failed"),
                                 QStringLiteral("failed"), 1000, 2, 0)}));
        QVERIFY(h.openDialog());
        QTest::qWait(150);

        const auto itemOf = [&](const QString& name) -> QQuickItem* {
            return findChildItem(h.window->contentItem(), name);
        };

        // 唯一任务是 failed → 默认选中它，重试按钮可见。
        QTRY_VERIFY_WITH_TIMEOUT(([&]() {
            QQuickItem* retry = itemOf(QStringLiteral("backgroundTaskDetailRetry"));
            return retry && retry->isVisible();
        })(), 5000);
        QQuickItem* retry = itemOf(QStringLiteral("backgroundTaskDetailRetry"));
        QTest::mouseClick(
            h.window, Qt::LeftButton, Qt::NoModifier,
            retry->mapToScene(QPointF(retry->width() / 2, retry->height() / 2))
                .toPoint());
        QTRY_COMPARE(h.stub.retries, 1);
        QCOMPARE(h.stub.retriedId, QStringLiteral("sa-1"));

        // completed 任务不显示重试按钮。
        QVERIFY(h.setTasks({task(QStringLiteral("sa-2"),
                                 QStringLiteral("all good"),
                                 QStringLiteral("completed"), 1000, 4, 0)}));
        QTest::qWait(150);
        QQuickItem* hidden = itemOf(QStringLiteral("backgroundTaskDetailRetry"));
        QVERIFY2(!hidden || !hidden->isVisible(),
                 "completed 任务不应显示重试按钮");
    }

    // docs/43 O4-3: completed 任务详情区按需渲染 findings 列表——severity
    // 徽章、path:line 定位、说明文本；运行中任务不渲染（查询返回空）。
    void completedTaskShowsFindingsList() {
        BackgroundDialogHarness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setTasks({task(QStringLiteral("sa-1"),
                                 QStringLiteral("review the patch"),
                                 QStringLiteral("completed"), 1000, 4, 0)}));
        QVERIFY(h.openDialog());
        QTest::qWait(150);

        const auto itemOf = [&](const QString& name) -> QQuickItem* {
            return findChildItem(h.window->contentItem(), name);
        };
        const auto textOf = [&](const QString& name) -> QString {
            QQuickItem* item = itemOf(name);
            return item ? item->property("text").toString() : QString();
        };

        QQuickItem* section =
            itemOf(QStringLiteral("backgroundTaskDetailFindings"));
        QVERIFY2(section, "findings 区没有渲染");
        QVERIFY2(section->isVisible(), "findings 区不可见");
        QVERIFY2(textOf(QStringLiteral("backgroundTaskDetailFindingsTitle"))
                     .contains(QStringLiteral("2")),
                 "findings 标题缺条数");

        // 逐条：徽章文本 + 定位 path:line。
        int highBadges = 0;
        int locations = 0;
        // 递归遍历需显式类型（auto lambda 不能自引用）。
        std::function<void(QQuickItem*)> walkTexts =
            [&](QQuickItem* item) {
            if (item->objectName().isEmpty()
                    && !item->property("text").toString().isEmpty()) {
                const QString text = item->property("text").toString();
                if (text == QLatin1String("high")) ++highBadges;
                if (text.contains(QStringLiteral("src/a.cpp:12"))) ++locations;
            }
            for (QQuickItem* child : item->childItems()) walkTexts(child);
        };
        walkTexts(section);
        QCOMPARE(highBadges, 1);
        QCOMPARE(locations, 1);
    }
};

QTEST_MAIN(TestBackgroundTasksQml)
#include "test_background_tasks_qml.moc"