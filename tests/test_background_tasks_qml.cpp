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

    // docs/35 P4.3/P4.5: 面板按钮必须真的调到门面——用户反馈过「点了没
    // 反应」，而渲染级用例（只看文案）发现不了。这里逐颗点像素。
    void rowActionsInvokeFacade() {
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
        QTest::qWait(150);

        QTRY_VERIFY2(findChildItem(h.window->contentItem(),
                                   QStringLiteral("backgroundTaskStopButton")),
                     "运行中的任务没有停止按钮");
        QVERIFY2(h.click(QStringLiteral("backgroundTaskStopButton")),
                 "停止按钮不可点击（被遮挡或不可见）");
        QCOMPARE(h.stub.cancels, 1);
        QCOMPARE(h.stub.cancelledId, QStringLiteral("sa-1"));

        // 查看按需取全文：展开时才调报告接口。
        QCOMPARE(h.stub.lastReportId, QString());
        QVERIFY2(h.click(QStringLiteral("backgroundTaskViewButton")),
                 "查看按钮不可点击");
        QCOMPARE(h.stub.lastReportId, QStringLiteral("sa-1"));

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