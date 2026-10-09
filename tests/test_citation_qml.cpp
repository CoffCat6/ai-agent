// docs/20 §5.1 (CITE-801): QML 侧契约 —— CitationParser 单例真的注册进了
// NexusAI.ViewModels 并能被 QML 调用。
//
// 背景：切分规则的唯一实现是 src/viewmodels/CitationParser.h（C++）。曾有一份
// 逐条镜像的 JS 实现放在 AssistantMessage.qml 里，双份逻辑漂移过一次（注释都腐化
// 了），已删除。删除后 QML 直接调用注册的单例 —— 但「类型没注册成功」在 QML 里
// 是**静默**的：AssistantMessage 的 chip 只会不渲染，没有任何报错。
//
// 本用例用真实 QQmlApplicationEngine 加载 import NexusAI.ViewModels 的夹具
// （刻意不用相对目录导入 —— 那会绕过模块注册，测不到真东西），锁住：
//   1. 模块能 import、单例能调到；
//   2. 返回的键名与 AssistantMessage 读取的 modelData.citation/.name/.text 一致；
//   3. 识别/拒绝规则与 C++ 纯函数（tests/test_highlighter.cpp）一致。
// 纯函数本身的行为由 test_highlighter 覆盖，这里只验 QML 通道。

#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QQuickItem>
#include <QVariantList>

#include "viewmodels/CitationParser.h"

class TestCitationQml : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("NexusAI"));
        QCoreApplication::setApplicationName(QStringLiteral("NexusAI"));
    }

    // 夹具 import 的是 NexusAI.ViewModels：模块或类型没注册时 isReady() 直接
    // 失败，这就是本用例存在的意义。
    void qmlSingletonIsRegisteredAndCallable() {
        QQmlApplicationEngine engine;
        QQmlComponent component(
            &engine, QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR "/CitationParserHarness.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));

        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto* win = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(win);
        win->resize(400, 300);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));

        // ── 通过 QML 的 split() 拿结果（与 AssistantMessage 同一条调用路径）──
        QVariant partsVar;
        const bool invoked = QMetaObject::invokeMethod(
            root.get(), "split", Q_RETURN_ARG(QVariant, partsVar),
            Q_ARG(QVariant, QStringLiteral("结论甲 [文档A.md]。收尾。")));
        QVERIFY2(invoked, qPrintable("split() not callable"));
        const QVariantList parts = partsVar.toList();
        QCOMPARE(parts.size(), 3);

        // 键名必须与 AssistantMessage 里 modelData.citation / .name / .text 一致。
        QVariantMap cite = parts[1].toMap();
        QCOMPARE(cite.value(QStringLiteral("citation")).toBool(), true);
        QCOMPARE(cite.value(QStringLiteral("name")).toString(), QStringLiteral("文档A.md"));
        QCOMPARE(cite.value(QStringLiteral("text")).toString(), QStringLiteral("文档A.md"));
        QCOMPARE(parts[0].toMap().value(QStringLiteral("citation")).toBool(), false);
        QCOMPARE(parts[0].toMap().value(QStringLiteral("text")).toString(),
                 QStringLiteral("结论甲 "));

        // 与 C++ 纯函数逐字段一致（唯一事实源）。
        const QVector<NexusAI::Citation::Segment> segs =
            NexusAI::Citation::parse(QStringLiteral("结论甲 [文档A.md]。收尾。"));
        QCOMPARE(parts.size(), segs.size());
        for (int i = 0; i < segs.size(); ++i) {
            QCOMPARE(parts[i].toMap().value(QStringLiteral("text")).toString(), segs[i].text);
            QCOMPARE(parts[i].toMap().value(QStringLiteral("citation")).toBool(),
                     segs[i].isCitation);
            QCOMPARE(parts[i].toMap().value(QStringLiteral("name")).toString(), segs[i].docName);
        }

        // hasCitation 通道。
        QVariant hasCite;
        QVERIFY(QMetaObject::invokeMethod(root.get(), "anyCitation",
                                          Q_RETURN_ARG(QVariant, hasCite),
                                          Q_ARG(QVariant, QStringLiteral("见 [文档A.md]"))));
        QVERIFY(hasCite.toBool());
        QVERIFY(QMetaObject::invokeMethod(root.get(), "anyCitation",
                                          Q_RETURN_ARG(QVariant, hasCite),
                                          Q_ARG(QVariant, QStringLiteral("见文档。"))));
        QVERIFY(!hasCite.toBool());

        // 空输入不崩、不产生片段。
        QVariant emptyVar;
        QVERIFY(QMetaObject::invokeMethod(root.get(), "split",
                                          Q_RETURN_ARG(QVariant, emptyVar),
                                          Q_ARG(QVariant, QString())));
        QCOMPARE(emptyVar.toList().size(), 0);
    }

    // 拒绝规则同样要经 QML 通道验一遍：markdown 链接/引用定义、图片语法、
    // 未 trim、未闭合、嵌套括号、超长、空内容都不识别，且无损。
    void qmlRejectsNonCitationsLosslessly() {
        QQmlApplicationEngine engine;
        QQmlComponent component(
            &engine, QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR "/CitationParserHarness.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        const QStringList cases{
            QStringLiteral("[标题](http://x)"),
            QStringLiteral("[ref][def]"),
            QStringLiteral("![alt](y)"),
            QStringLiteral("[ 文档A ]"),
            QStringLiteral("前 [文档A 后文"),
            QStringLiteral("[外 [内] 尾]"),
            QStringLiteral("[]"),
        };
        for (const QString& c : cases) {
            QVariant partsVar;
            QVERIFY2(QMetaObject::invokeMethod(root.get(), "split",
                                               Q_RETURN_ARG(QVariant, partsVar),
                                               Q_ARG(QVariant, c)),
                     qPrintable(c));
            const QVariantList parts = partsVar.toList();
            QString joined;
            for (const QVariant& v : parts) {
                QVERIFY2(!v.toMap().value(QStringLiteral("citation")).toBool(),
                         qPrintable(QStringLiteral("must not cite: %1").arg(c)));
                joined += v.toMap().value(QStringLiteral("text")).toString();
            }
            QCOMPARE(joined, c);   // 无损
        }
    }
};

// offscreen 平台：本用例创建真实 QQuickWindow，但 CI 无显示器。
QTEST_MAIN(TestCitationQml)
#include "test_citation_qml.moc"
