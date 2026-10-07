// docs/20 §3.4（A4 拖拽附件）：QML DropArea → C++ 的拖放边界回归测试。
//
// 该测试存在的理由：A4 首版"拖拽完全没反应"，根因不在分类或读数逻辑，
// 而在 QML/C++ 边界——`drop.urls` 的元素是 QML 的 URL 包装对象，原型上
// 没有 `toLocalFile()`（只有 toString/valueOf）。在 QML 里写
// `u.toLocalFile()` 会抛 TypeError，整个 onDropped 静默失效；而
// `onEntered` 早已 accept，光标照常变"允许"，所以从界面看不出问题。
//
// 这类缺陷不会被纯函数单测覆盖（分类逻辑是对的），也不会被编译期发现
// （QML 是运行时解析）。因此这里用真实的 QDragEnterEvent/QDragMoveEvent/
// QDropEvent 走一遍真实的 delivery agent，断言：
//   1) QML 的 url 对象确实没有 toLocalFile()（锁定根因，防止有人"顺手"
//      把它挪回 QML 里）；
//   2) 根级 DropArea 能收到拖入/拖放并被 accept（否则平台光标是禁止形状）；
//   3) drop.urls 原样传给 C++ 的 `const QList<QUrl>&` 形参后，能拿到
//      正确解码的本地路径（空格 / 中文 / '#' / '%' 都要还原）。
//
// 依赖仅为 Qt Quick（不依赖应用的 QML 模块），因此可在 ctest 中独立运行。

#include <QtTest>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QTemporaryDir>
#include <QFile>

namespace {

// 与 AgentViewModel::handleDroppedFiles 的真实签名同形（收 QUrl 原始值）。
class DropSink : public QObject {
    Q_OBJECT
public:
    struct Call {
        QList<QUrl> urls;
    };
    QList<Call> calls;

    Q_INVOKABLE void handleDroppedFiles(const QList<QUrl>& urls) {
        calls.append(Call{urls});
    }

    void reset() { calls.clear(); }
};

} // namespace

class TestDropArea : public QObject {
    Q_OBJECT

private:
    // 复用临时目录里的一个真实文件，供"能读盘"的断言使用。
    static QString writeFile(QTemporaryDir& dir, const QString& name) {
        const QString path = dir.filePath(name);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) return {};
        f.write("drop target");
        f.close();
        return path;
    }

    // 被测组件放在真实 .qml 文件里（tests/qml/DropAreaHarness.qml），而不是
    // 内嵌 raw string：moc 的预处理器会把内嵌 QML 里的 `Q_OBJECT` 类误判，
    // 导致 TestDropArea 的 vtable 不生成。独立文件也让被测组件能被 qmllint
    // 与 qml 侧测试直接复用。
    static QUrl harnessUrl() {
        return QUrl::fromLocalFile(
            QStringLiteral(NEXUS_TEST_QML_DIR "/DropAreaHarness.qml"));
    }

    // 返回被测窗口；失败时返回 nullptr（错误已写入 QVERIFY 上下文）。
    QQuickWindow* createWindow(QQmlEngine& engine, DropSink& sink) {
        engine.rootContext()->setContextProperty(QStringLiteral("Sink"), &sink);
        QQmlComponent comp(&engine, harnessUrl());
        if (comp.isError()) {
            qWarning().noquote() << "component errors:" << comp.errorString();
            return nullptr;
        }
        QObject* obj = comp.create();
        if (!obj) {
            qWarning().noquote() << "create failed:" << comp.errorString();
            return nullptr;
        }
        QQuickWindow* window = qobject_cast<QQuickWindow*>(obj);
        if (!window) {
            qWarning() << "root object is not a QQuickWindow";
            return nullptr;
        }
        // offscreen 平台插件的 QPlatformWindow 不会把窗口尺寸同步到
        // contentItem（实测 0x0），而拖放命中测试依赖 contentItem 的几何：
        // 尺寸为 0 时 item->contains(p) 恒为 false，拖放事件一个都到不了。
        // 显式给尺寸让本用例在无显示环境下也能跑（真实窗口平台本来就会同步）。
        window->resize(400, 300);
        if (QQuickItem* content = window->contentItem()) {
            if (content->width() <= 0 || content->height() <= 0) {
                content->setWidth(400);
                content->setHeight(300);
            }
        }
        QCoreApplication::processEvents();
        return window;
    }

    // 走真实的窗口拖放分派路径。
    struct DragResult {
        bool enterAccepted = false;
        bool moveAccepted = false;
        bool dropAccepted = false;
    };

    static DragResult sendDrag(QQuickWindow* window, const QList<QUrl>& urls) {
        QMimeData mime;
        mime.setUrls(urls);
        const QPoint pos(200, 150);

        QDragEnterEvent enter(pos, Qt::CopyAction, &mime,
                              Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &enter);

        QDragMoveEvent move(pos, Qt::CopyAction, &mime,
                            Qt::LeftButton, Qt::NoModifier);
        move.setDropAction(Qt::CopyAction);
        QCoreApplication::sendEvent(window, &move);

        QDropEvent drop(QPointF(pos), Qt::CopyAction, &mime,
                        Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &drop);

        return DragResult{enter.isAccepted(), move.isAccepted(), drop.isAccepted()};
    }

private slots:
    void initTestCase() {
        // 拖放需要 GUI 栈；无显示环境下靠 offscreen 平台插件（ctest 里设置）。
        QVERIFY(QGuiApplication::instance() != nullptr);
    }

    // 锁定根因：QML 的 url 对象没有 toLocalFile()。若将来 Qt 或代码改动
    // 让这个前提不再成立，本用例会失败并提醒复核 main.qml 的注释与实现。
    void qmlUrlObjectHasNoToLocalFile() {
        QQmlEngine engine;
        QQmlComponent comp(&engine, QUrl::fromLocalFile(
            QStringLiteral(NEXUS_TEST_QML_DIR "/UrlApiProbe.qml")));
        QVERIFY2(!comp.isError(), qPrintable(comp.errorString()));
        QScopedPointer<QObject> obj(comp.create());
        QVERIFY(obj);
        QCOMPARE(obj->property("toLocalFileType").toString(),
                 QStringLiteral("undefined"));
        // toString() 仍可用——所以 main.qml 用它做前缀判断是安全的。
        QVERIFY(obj->property("href").toString()
                    .startsWith(QStringLiteral("file:")));
    }

    // 核心契约：根级 DropArea 收到 enter/move/drop，drop.urls 交给 C++ 后
    // 能拿到正确解码的本地路径（含空格/中文/保留字符）。
    void rootDropAreaDeliversDecodedLocalPaths() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeFile(dir, QStringLiteral("drop 报告 #1%.txt"));
        QVERIFY(!path.isEmpty());
        QVERIFY(QFile::exists(path));

        QQmlEngine engine;
        DropSink sink;
        QScopedPointer<QQuickWindow> window(createWindow(engine, sink));
        QVERIFY(window);

        const QUrl url = QUrl::fromLocalFile(path);
        const DragResult r = sendDrag(window.data(), {url});

        // 三者都必须被接受：enter 不被接受则平台光标是"禁止"，drop 不被
        // 接受则 Windows 不会真正投放。
        QVERIFY2(r.enterAccepted, "DragEnter not accepted -> forbidden cursor");
        QVERIFY2(r.moveAccepted, "DragMove not accepted");
        QVERIFY2(r.dropAccepted, "Drop not accepted");

        QCOMPARE(sink.calls.size(), 1);
        const QList<QUrl> got = sink.calls.first().urls;
        QCOMPARE(got.size(), 1);
        QVERIFY(got.first().isLocalFile());
        QCOMPARE(got.first().toLocalFile(), path);
        // 与磁盘上的真实文件对得上（不是被 percent-encoding 破坏的路径）。
        QCOMPARE(QFileInfo(got.first().toLocalFile()).fileName(),
                 QStringLiteral("drop 报告 #1%.txt"));
    }

    // 非 file: URL 必须原样送达 C++（由 C++ 判定为不支持），不能被 QML 侧
    // 的 file: 前缀过滤悄悄丢掉——否则 http 拖拽既无附件也无提示。
    void nonLocalUrlsReachCpp() {
        QQmlEngine engine;
        DropSink sink;
        QScopedPointer<QQuickWindow> window(createWindow(engine, sink));
        QVERIFY(window);

        const QUrl remote(QStringLiteral("https://example.com/a.txt"));
        const DragResult r = sendDrag(window.data(), {remote});
        QVERIFY(r.enterAccepted);
        QVERIFY(r.dropAccepted);
        QCOMPARE(sink.calls.size(), 1);
        const QList<QUrl> got = sink.calls.first().urls;
        QCOMPARE(got.size(), 1);
        QVERIFY(!got.first().isLocalFile());
        QCOMPARE(got.first().toString(), remote.toString());
    }

    // 多处投放必须每次都被送到 C++（防"只有第一次生效"的状态残留）。
    void repeatedDropsAreDelivered() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeFile(dir, QStringLiteral("again.txt"));
        QVERIFY(!path.isEmpty());

        QQmlEngine engine;
        DropSink sink;
        QScopedPointer<QQuickWindow> window(createWindow(engine, sink));
        QVERIFY(window);

        const QUrl url = QUrl::fromLocalFile(path);
        for (int i = 0; i < 3; ++i) {
            const DragResult r = sendDrag(window.data(), {url});
            QVERIFY(r.dropAccepted);
        }
        QCOMPARE(sink.calls.size(), 3);
        for (const DropSink::Call& c : sink.calls)
            QCOMPARE(c.urls.first().toLocalFile(), path);
    }
};

QTEST_MAIN(TestDropArea)
#include "test_droparea.moc"
