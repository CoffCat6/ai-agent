// docs/32 §9: ProcessCard 子代理过程可视化的 QML 渲染级回归。
//
// 背景：用户真机反馈「子代理过程展开后看不到，只有参数 JSON」，而数据链路
// 全部实证正常——test_agent 断言核心 trace 节点里有 subagentLog、用户库的
// trace_nodes.metadata 里 50 条满、TraceModel/applyDelta 透传。根因在
// ProcessCard 的判型：QVariantList 进 QML 后是 sequence wrapper，
// Array.isArray() 对它返回 false（QJSEngine 探针实测
// `false|1|tool`），`Array.isArray(raw) && raw.length > 0` 恒为假，
// hasSubagentLog 永远 false，UI 永远走参数代码块分支。这是本项目第二起
// 「C++ 测试全绿、纯 QML 盲区」——同 docs/25 图表空白的教训，补渲染级用例。
//
// 方案与 test_charts_qml 一致：进程内加载真实组件（build/NexusAI 文件系统
// 副本 + qrc 回退拦截器 + offscreen）。

#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlAbstractUrlInterceptor>
#include <QQuickWindow>
#include <QQuickItem>
#include <QtQuick>
#include <QVariantMap>
#include <QVariantList>
#include <QFile>

#include <memory>
#include "TestSettingsIsolation.h"

namespace {
const NexusAI::TestSupport::SettingsIsolation kSettingsIsolation;
} // namespace

namespace {

// qrc → build/NexusAI 文件系统副本的回退拦截器（机制说明见
// test_charts_qml.cpp：qmldir 的 prefer 指令只做优先、不做失败回退）。
// 注意基目录是 build/NexusAI（qt_add_qml_module URI 目录），qrc 里的
// /qt/qml/NexusAI/src/... 对应 NexusAI/src/...（含模块 JS 资源如
// FileTree.js——模块 import 会急切加载它，拦截不到就整体报 unavailable）。
class NexusQrcFallback : public QQmlAbstractUrlInterceptor {
public:
    explicit NexusQrcFallback(QString baseDir) : m_baseDir(std::move(baseDir)) {}

    QUrl intercept(const QUrl& url, DataType /*type*/) override {
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

// docs/35 P4.5: ProcessCard 的「停止」芯片只负责把 trace 节点 id 交给
// AgentVM.cancelSubAgent；真实取消链路在 test_agent 覆盖。这里锁住
// QML → VM 这一段（曾出现过「点了没反应」的入口回归）。
class StubAgentVM : public QObject {
    Q_OBJECT
public:
    Q_INVOKABLE bool cancelSubAgent(const QString& traceNodeId) {
        cancelledIds.append(traceNodeId);
        return true;
    }
    Q_INVOKABLE void openSubagentEvidence(const QString& path) {
        openedEvidence.append(path);
    }

    QStringList cancelledIds;
    QStringList openedEvidence;
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

// 与 TraceNodeRouter 喂入 ProcessCard 的方式同构：QVariantMap 节点经
// QAbstractListModel 的 modelData role 交给 delegate（不是 setProperty——
// 那条路径嵌套 QVariantList 会转成原生 JS Array，只有 role 路径才是
// sequence wrapper，见文件头注释）。
class RowsModel : public QAbstractListModel {
public:
    enum { NodeRole = Qt::UserRole + 1 };
    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : 1;
    }
    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || role != NodeRole) return {};
        return m_node;
    }
    QHash<int, QByteArray> roleNames() const override {
        return {{NodeRole, "modelData"}};
    }
    Q_INVOKABLE void setRow(const QVariantMap& node) {
        beginResetModel();
        m_node = node;
        endResetModel();
    }

private:
    QVariantMap m_node;
};
QVariantMap spawnNode(int logEntries) {
    QVariantList log;
    for (int i = 0; i < logEntries; ++i) {
        log.append(QVariantMap{
            {QStringLiteral("phase"), i % 2 == 0 ? QStringLiteral("tool")
                                                 : QStringLiteral("assistant")},
            {QStringLiteral("text"),
             QStringLiteral("entry-%1: http_fetch({\"url\":\"https://x/%1\"}) ⇒ ok").arg(i)}});
    }
    return QVariantMap{
        {QStringLiteral("id"), QStringLiteral("n1")},
        {QStringLiteral("type"), QStringLiteral("tool_call")},
        {QStringLiteral("title"),
         QStringLiteral("spawn_agent · step 3 · test_sub_probe×2")},
        {QStringLiteral("status"), QStringLiteral("running")},
        {QStringLiteral("args"), QStringLiteral("{\"goal\":\"probe\"}")},
        {QStringLiteral("subagentLog"), log},
    };
}

// docs/34 P3.2: spawn_agent 结果节点上的提案索引（子代理经 propose_edit
// 起草，正文不进 metadata）。withResult=false 用于验证「只有提案、报告
// 正文为空」时展开区仍可用。
QVariantMap proposalNode(bool withResult) {
    QVariantList proposals;
    proposals.append(QVariantMap{
        {QStringLiteral("path"), QStringLiteral("src/app.cpp")},
        {QStringLiteral("rationale"), QStringLiteral("return success")},
        {QStringLiteral("count"), 2}});
    proposals.append(QVariantMap{
        {QStringLiteral("path"), QStringLiteral("README.md")},
        {QStringLiteral("rationale"), QStringLiteral("document it")},
        {QStringLiteral("count"), 1}});
    return QVariantMap{
        {QStringLiteral("id"), QStringLiteral("n2")},
        {QStringLiteral("type"), QStringLiteral("tool_result")},
        {QStringLiteral("title"), QStringLiteral("spawn_agent")},
        {QStringLiteral("status"), QStringLiteral("success")},
        {QStringLiteral("args"), QString()},
        {QStringLiteral("result"),
         withResult ? QStringLiteral("[Sub-agent report] ... ## PROPOSALS (not applied)")
                    : QString()},
        {QStringLiteral("subagentProposals"), proposals},
    };
}

// docs/33 E1: 成功的 spawn_agent 节点带证据工件路径，头部渲染「查看证据」
// 芯片；点击必须把路径交给门面（与停止芯片同一套遮挡问题）。
QVariantMap evidenceNode() {
    QVariantMap node = spawnNode(0);
    node[QStringLiteral("status")] = QStringLiteral("success");
    node[QStringLiteral("title")] = QStringLiteral("spawn_agent");
    node[QStringLiteral("evidencePath")] =
        QStringLiteral("/evidence/sa-1.md");
    return node;
}

} // namespace

class TestProcessCardQml : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("NexusAI"));
        QCoreApplication::setApplicationName(QStringLiteral("NexusAI"));
    }

    void subagentLogRendersFromVariantList() {
        QQmlApplicationEngine engine;
        RowsModel rows;
        engine.rootContext()->setContextProperty(QStringLiteral("rowsModel"),
                                                 &rows);
        NexusQrcFallback interceptor(QStringLiteral(NEXUS_MODULE_DIR));
        engine.addUrlInterceptor(&interceptor);
        engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));

        QQmlComponent component(&engine, QUrl::fromLocalFile(
            QStringLiteral(NEXUS_TEST_QML_DIR "/ProcessCardHarness.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto* win = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(win);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));

        auto* harness = root.get();
        const auto probe = [&]() -> QVariantMap {
            QVariant out;
            const bool ok = QMetaObject::invokeMethod(harness, "probe",
                                              Q_RETURN_ARG(QVariant, out));
            return ok ? out.toMap() : QVariantMap{{QStringLiteral("found"), false}};
        };

        // ── 回归核心：role 路径的 QVariantList 进 hasSubagentLog 的判型 ──
        rows.setRow(spawnNode(2));
        QVariantMap p = probe();
        QCOMPARE(p.value(QStringLiteral("found")).toBool(), true);
        // role 路径的嵌套 QVariantList 不是原生 JS 数组——判型若改回
        // Array.isArray，下一条断言就会红（曾真实翻车的语义）。
        QCOMPARE(p.value(QStringLiteral("rawIsArray")).toBool(), false);
        QCOMPARE(p.value(QStringLiteral("hasSubagentLog")).toBool(), true);
        QCOMPARE(p.value(QStringLiteral("logLength")).toInt(), 2);
        QCOMPARE(p.value(QStringLiteral("firstPhase")).toString(),
                 QStringLiteral("tool"));

        // 展开态 body 加载的是过程日志组件（而非参数代码块）。
        QCOMPARE(p.value(QStringLiteral("bodyActive")).toBool(), true);
        QCOMPARE(p.value(QStringLiteral("bodyObjectName")).toString(),
                 QStringLiteral("subagentLogBody"));

        // 日志行为空（无 subagentLog 键）时回落到参数代码块，不渲染日志体。
        QVariantMap noLog = spawnNode(2);
        noLog.remove(QStringLiteral("subagentLog"));
        rows.setRow(noLog);
        p = probe();
        QCOMPARE(p.value(QStringLiteral("hasSubagentLog")).toBool(), false);
        QCOMPARE(p.value(QStringLiteral("bodyObjectName")).toString(),
                 QStringLiteral(""));

        // 日志容量控制在采集端（50 条上限），UI 有多少渲染多少：传 60 条
        // 照常出 60 条，不因条数异常。
        rows.setRow(spawnNode(60));
        p = probe();
        QCOMPARE(p.value(QStringLiteral("logLength")).toInt(), 60);
    }

    // docs/34 P3.2: 提案索引徽章与展开列表（同一 role 路径，同一判型坑）。
    void subagentProposalsRenderFromVariantList() {
        QQmlApplicationEngine engine;
        RowsModel rows;
        engine.rootContext()->setContextProperty(QStringLiteral("rowsModel"), &rows);
        NexusQrcFallback interceptor(QStringLiteral(NEXUS_MODULE_DIR));
        engine.addUrlInterceptor(&interceptor);
        engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));

        QQmlComponent component(&engine, QUrl::fromLocalFile(
            QStringLiteral(NEXUS_TEST_QML_DIR "/ProcessCardHarness.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto* win = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(win);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));

        auto* harness = root.get();
        const auto probe = [&]() -> QVariantMap {
            QVariant out;
            const bool ok = QMetaObject::invokeMethod(harness, "probe",
                                              Q_RETURN_ARG(QVariant, out));
            return ok ? out.toMap() : QVariantMap{{QStringLiteral("found"), false}};
        };

        rows.setRow(proposalNode(true));
        QVariantMap p = probe();
        QCOMPARE(p.value(QStringLiteral("found")).toBool(), true);
        QCOMPARE(p.value(QStringLiteral("rawProposalsIsArray")).toBool(), false);
        QCOMPARE(p.value(QStringLiteral("hasSubagentProposals")).toBool(), true);
        QCOMPARE(p.value(QStringLiteral("proposalCount")).toInt(), 2);
        // 徽章总数 = 各提案 count 之和（2 + 1）。
        QCOMPARE(p.value(QStringLiteral("proposalTotal")).toInt(), 3);
        // 展开区加载提案列表组件。
        QCOMPARE(p.value(QStringLiteral("bodyActive")).toBool(), true);
        QCOMPARE(p.value(QStringLiteral("bodyObjectName")).toString(),
                 QStringLiteral("subagentProposalsBody"));

        // 报告正文为空（仅提案）时仍可展开——bodyVisible 不能只看正文。
        rows.setRow(proposalNode(false));
        p = probe();
        QCOMPARE(p.value(QStringLiteral("hasSubagentProposals")).toBool(), true);
        QCOMPARE(p.value(QStringLiteral("bodyActive")).toBool(), true);
        QCOMPARE(p.value(QStringLiteral("bodyObjectName")).toString(),
                 QStringLiteral("subagentProposalsBody"));

        // 非 spawn_agent 结果即使带了同名字段也不渲染提案（白名单语义）。
        QVariantMap foreign = proposalNode(true);
        foreign.insert(QStringLiteral("title"), QStringLiteral("edit_file"));
        rows.setRow(foreign);
        p = probe();
        QCOMPARE(p.value(QStringLiteral("hasSubagentProposals")).toBool(), false);

        // 无提案键回落代码块。
        QVariantMap noProposals = proposalNode(true);
        noProposals.remove(QStringLiteral("subagentProposals"));
        rows.setRow(noProposals);
        p = probe();
        QCOMPARE(p.value(QStringLiteral("hasSubagentProposals")).toBool(), false);
        QCOMPARE(p.value(QStringLiteral("bodyObjectName")).toString(),
                 QStringLiteral(""));
    }

    // docs/35 P4.5：运行中的 spawn_agent 卡片点「停止」必须把节点 id 交给
    // AgentVM。用例点真实像素（不是直接调 handler），覆盖 MouseArea 命中、
    // 遮挡与绑定求值。
    void stopChipInvokesCancelWithNodeId() {
        QQmlApplicationEngine engine;
        RowsModel rows;
        StubAgentVM vm;
        engine.rootContext()->setContextProperty(QStringLiteral("rowsModel"),
                                                 &rows);
        NexusQrcFallback interceptor(QStringLiteral(NEXUS_MODULE_DIR));
        engine.addUrlInterceptor(&interceptor);
        engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));

        QQmlComponent component(&engine, QUrl::fromLocalFile(
            QStringLiteral(NEXUS_TEST_QML_DIR "/ProcessCardHarness.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto* win = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(win);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));

        // ProcessCard 在模块内被预编译，`AgentVM` 在编译期就绑定到
        // QML_SINGLETON 类型，context property 遮蔽不了它；桩只能通过组件
        // 自己的 agentFacade 属性注入（见 ProcessCard.qml 的注释）。
        QVERIFY(QMetaObject::invokeMethod(
            root.get(), "setInjectedAgent",
            Q_ARG(QVariant, QVariant::fromValue<QObject*>(&vm))));

        rows.setRow(spawnNode(1));   // status=running, title spawn_agent...
        QTest::qWait(120);

        QQuickItem* chip = findChildItem(win->contentItem(),
                                         QStringLiteral("subagentStopChip"));
        QVERIFY2(chip, "停止芯片没有渲染");
        QVERIFY(chip->isVisible());
        const QPoint center = chip->mapToScene(
            QPointF(chip->width() / 2, chip->height() / 2)).toPoint();
        QTest::mouseClick(win, Qt::LeftButton, Qt::NoModifier, center);
        QTRY_COMPARE(vm.cancelledIds.size(), 1);
        QCOMPARE(vm.cancelledIds.first(), QStringLiteral("n1"));
        // 芯片必须独占点击，不能顺带折叠卡片。
        QCOMPARE(root->property("toggleCount").toInt(), 0);

        // 空白处仍能折叠卡片——z:-1 修复不能把展开热区一起废掉。
        QQuickItem* header = findChildItem(win->contentItem(),
                                           QStringLiteral("cardHeaderArea"));
        QVERIFY2(header, "卡片头展开热区没有渲染");
        QTest::mouseClick(
            win, Qt::LeftButton, Qt::NoModifier,
            header->mapToScene(QPointF(5, header->height() / 2)).toPoint());
        QTRY_COMPARE(root->property("toggleCount").toInt(), 1);
    }

    // docs/33 E1：证据芯片与停止芯片同在卡片头，也被同一个展开热区
    // （headerMA）盖住过。点真实像素锁住修复。
    void evidenceChipInvokesFacade() {
        QQmlApplicationEngine engine;
        RowsModel rows;
        StubAgentVM vm;
        engine.rootContext()->setContextProperty(QStringLiteral("rowsModel"),
                                                 &rows);
        NexusQrcFallback interceptor(QStringLiteral(NEXUS_MODULE_DIR));
        engine.addUrlInterceptor(&interceptor);
        engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));

        QQmlComponent component(&engine, QUrl::fromLocalFile(
            QStringLiteral(NEXUS_TEST_QML_DIR "/ProcessCardHarness.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto* win = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(win);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));

        QVERIFY(QMetaObject::invokeMethod(
            root.get(), "setInjectedAgent",
            Q_ARG(QVariant, QVariant::fromValue<QObject*>(&vm))));

        rows.setRow(evidenceNode());
        QTest::qWait(120);

        QQuickItem* chip = findChildItem(win->contentItem(),
                                         QStringLiteral("subagentEvidenceChip"));
        QVERIFY2(chip, "证据芯片没有渲染");
        QVERIFY(chip->isVisible());
        QTest::mouseClick(
            win, Qt::LeftButton, Qt::NoModifier,
            chip->mapToScene(QPointF(chip->width() / 2, chip->height() / 2))
                .toPoint());
        QTRY_COMPARE(vm.openedEvidence.size(), 1);
        QCOMPARE(vm.openedEvidence.first(), QStringLiteral("/evidence/sa-1.md"));
    }
};

QTEST_MAIN(TestProcessCardQml)
#include "test_process_card_qml.moc"
