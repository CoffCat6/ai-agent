// G4: FileChangesPanel rendering regression.
//
// The data chain (tool result -> SQLite -> AgentVM.fileCheckpoints) is
// covered by test_tools/test_memory/test_agent; this case locks the visible
// contract: empty state takes no space, rows carry the undone/truncated
// state, collapse works, and the panel actually paints content.

#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlAbstractUrlInterceptor>
#include <QQuickWindow>
#include <QQuickItem>
#include <QImage>
#include <QColor>
#include <QDateTime>
#include <QFile>
#include <QMap>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>

#include <cmath>
#include <memory>

#include "core/tools/Sandbox.h"
#include "viewmodels/ThemeProvider.h"

using namespace NexusAI;

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

void collectVisualItems(QQuickItem* parent, const QString& objectName,
                        QList<QQuickItem*>* out) {
    const auto children = parent->childItems();
    for (QQuickItem* child : children) {
        if (child->objectName() == objectName)
            out->append(child);
        collectVisualItems(child, objectName, out);
    }
}

QVariantMap checkpoint(qint64 id, const QString& path, bool existed,
                       bool truncated, bool undone) {
    return QVariantMap{
        {"id", id},
        {"path", path},
        {"existed", existed},
        {"truncated", truncated},
        {"undone", undone},
        {"byteSize", 12},
        {"requestId", 1},
        {"createdAt", QDateTime::currentMSecsSinceEpoch()},
    };
}

class Harness {
public:
    NexusQrcFallback fallback{QStringLiteral(NEXUS_BUILD_DIR "/NexusAI")};
    QQmlApplicationEngine engine;
    std::unique_ptr<QObject> root;
    QQuickWindow* window{nullptr};
    QString error;

    explicit Harness(const QString& qmlFile = QStringLiteral("FileChangesHarness.qml")) {
        engine.addUrlInterceptor(&fallback);
        engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));
        QQmlComponent component(
            &engine, QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR "/") + qmlFile));
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
    }

    QVariant call(const char* fn) const {
        QVariant out;
        if (!root || !QMetaObject::invokeMethod(root.get(), fn, Q_RETURN_ARG(QVariant, out)))
            return {};
        return out;
    }

    bool setCheckpoints(const QVariantList& items) const {
        return root && QMetaObject::invokeMethod(root.get(), "setCheckpoints",
                                                 Q_ARG(QVariant, items));
    }

    bool setCollapsed(bool value) const {
        return root && QMetaObject::invokeMethod(root.get(), "setCollapsed",
                                                 Q_ARG(QVariant, value));
    }
};

} // namespace

class TestFileChangesQml : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("NexusAI"));
        QCoreApplication::setApplicationName(QStringLiteral("NexusAI"));
    }

    void cleanup() { Sandbox::setProjectDir(QString()); }

    void emptyStateTakesNoSpace() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QCOMPARE(h.call("itemCount").toInt(), 0);
        QCOMPARE(h.call("panelVisible").toBool(), false);
        QCOMPARE(h.call("panelHeight").toReal(), 0.0);
    }

    void rendersRowsAndStatuses() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));

        const QVariantList items{
            checkpoint(3, QStringLiteral("/project/src/new.cpp"), false, false, false),
            checkpoint(2, QStringLiteral("/project/src/big.cpp"), true, true, false),
            checkpoint(1, QStringLiteral("/project/src/old.cpp"), true, false, true)
        };
        QVariantList grouped = items;
        QVariantMap groupedNew = grouped.at(0).toMap();
        groupedNew["changeCount"] = 3;
        groupedNew["activeCount"] = 3;
        groupedNew["undoneCount"] = 0;
        grouped[0] = groupedNew;
        QVariantMap groupedOld = grouped.at(2).toMap();
        groupedOld["changeCount"] = 2;
        groupedOld["activeCount"] = 0;
        groupedOld["undoneCount"] = 2;
        grouped[2] = groupedOld;
        QVERIFY(h.setCheckpoints(grouped));

        QTRY_COMPARE(h.call("itemCount").toInt(), 3);
        QTRY_COMPARE(h.call("renderedCount").toInt(), 3);
        QVERIFY(h.call("panelVisible").toBool());
        QVERIFY(h.call("panelHeight").toReal() > 0);
        QVERIFY(h.call("listVisible").toBool());
        QTest::qWait(200);
        (void)h.window->grabWindow();
        QTest::qWait(200);

        QList<QQuickItem*> rows;
        collectVisualItems(h.window->contentItem(), QStringLiteral("fileChangeRow"), &rows);
        QCOMPARE(rows.size(), 3);

        QMap<QString, QQuickItem*> byPath;
        for (QQuickItem* row : rows)
            byPath.insert(row->property("modelData").toMap().value("path").toString(), row);
        QCOMPARE(byPath.size(), 3);
        QCOMPARE(byPath.value(QStringLiteral("/project/src/big.cpp"))
                     ->property("truncated").toBool(), true);
        QCOMPARE(byPath.value(QStringLiteral("/project/src/old.cpp"))
                     ->property("undone").toBool(), true);
        QCOMPARE(byPath.value(QStringLiteral("/project/src/new.cpp"))
                     ->property("undone").toBool(), false);

        // Repeated edits surface as a ×N badge on the single file row.
        QList<QQuickItem*> badges;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("fileChangeCountBadge"), &badges);
        QSet<QString> badgeTexts;
        for (QQuickItem* badge : badges) {
            if (badge->isVisible())
                badgeTexts.insert(badge->property("text").toString());
        }
        QCOMPARE(badgeTexts.size(), 1);
        QVERIFY(badgeTexts.contains(QStringLiteral("×3")));

        const QColor bg(h.call("backgroundColor").toString());
        QVERIFY(bg.isValid());
        const QImage shot = h.window->grabWindow();
        QVERIFY2(!shot.isNull(), "整窗抓图失败（offscreen 渲染异常）");
        const int visible = countNonBackground(shot, bg);
        qInfo() << "FileChangesPanel non-background pixels:" << visible;
        QVERIFY2(visible > 200,
                 qPrintable(QStringLiteral("文件改动面板渲染为空白（非背景像素 %1）").arg(visible)));
    }

    void collapsesAndExpands() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setCheckpoints({checkpoint(1, QStringLiteral("/p/a.cpp"), true, false, false),
                                  checkpoint(2, QStringLiteral("/p/b.cpp"), false, false, false)}));
        QTRY_VERIFY(h.call("panelVisible").toBool());
        QTest::qWait(150);
        (void)h.window->grabWindow();
        QTest::qWait(150);
        const qreal expanded = h.call("panelHeight").toReal();
        QVERIFY(expanded > 40);

        QVERIFY(h.setCollapsed(true));
        QTRY_COMPARE(h.call("listVisible").toBool(), false);
        QTRY_VERIFY(h.call("panelHeight").toReal() < expanded);

        QVERIFY(h.setCollapsed(false));
        QTRY_COMPARE(h.call("listVisible").toBool(), true);
        QTRY_COMPARE(h.call("panelHeight").toReal(), expanded);
    }

    void clearingHidesPanel() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setCheckpoints({checkpoint(1, QStringLiteral("/p/a.cpp"), true, false, false)}));
        QTRY_VERIFY(h.call("panelVisible").toBool());

        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "clearCheckpoints"));
        QTRY_COMPARE(h.call("itemCount").toInt(), 0);
        QCOMPARE(h.call("panelVisible").toBool(), false);
        QCOMPARE(h.call("panelHeight").toReal(), 0.0);
    }

    // Phase 1: a click on a row opens the detail dialog and the write-time
    // diff is rendered line by line; closing restores the list.
    void rowOpensDetailWithDiff() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));

        QVariantMap item =
            checkpoint(7, QStringLiteral("/project/src/new.cpp"), false, false, false);
        item["hasDiff"] = true;
        item["addedLines"] = 2;
        item["removedLines"] = 0;
        QVERIFY(h.setCheckpoints({item}));
        QTRY_COMPARE(h.call("renderedCount").toInt(), 1);

        const QString diff = QStringLiteral(
            "--- /dev/null\n"
            "+++ /project/src/new.cpp\n"
            "@@ -0,0 +1,2 @@\n"
            "+line one\n"
            "+line two\n");
        QVariantMap detail;
        detail["diff"] = diff;
        detail["diffTruncated"] = false;
        detail["addedLines"] = 2;
        detail["removedLines"] = 0;
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "setDetail",
                                          Q_ARG(QVariant, detail)));
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "openDetail",
                                          Q_ARG(QVariant, 0)));
        QTRY_VERIFY(h.call("detailVisible").toBool());
        QCOMPARE(h.call("detailPath").toString(),
                 QStringLiteral("/project/src/new.cpp"));
        QTRY_VERIFY(h.call("detailDiffLineCount").toInt() >= 5);

        QTest::qWait(150);
        (void)h.window->grabWindow();
        QTest::qWait(150);

        QList<QQuickItem*> diffViews;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("detailDiffView"), &diffViews);
        QVERIFY2(!diffViews.isEmpty(), "detailDiffView missing");
        QVERIFY2(diffViews.first()->width() > 200,
                 "detail diff pane collapsed horizontally");
        QVERIFY2(diffViews.first()->height() > 100,
                 "detail diff pane collapsed vertically");

        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "closeDetail"));
        QTRY_COMPARE(h.call("detailVisible").toBool(), false);

        // A path-only checkpoint still opens, with no invented diff lines.
        QVariantMap truncatedItem =
            checkpoint(8, QStringLiteral("/project/src/big.cpp"), true, true, false);
        QVERIFY(h.setCheckpoints({truncatedItem}));
        QVariantMap truncatedDetail;
        truncatedDetail["diff"] = QString();
        truncatedDetail["diffTruncated"] = false;
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "setDetail",
                                          Q_ARG(QVariant, truncatedDetail)));
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "openDetail",
                                          Q_ARG(QVariant, 0)));
        QTRY_VERIFY(h.call("detailVisible").toBool());
        QCOMPARE(h.call("detailPath").toString(),
                 QStringLiteral("/project/src/big.cpp"));
        QCOMPARE(h.call("detailDiffLineCount").toInt(), 0);
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "closeDetail"));
    }

    // 审阅升级：目录分组 + 折叠（折叠后仍然只统计可见文件行）。
    void treeGroupsFilesByFolder() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setCheckpoints({
            checkpoint(41, QStringLiteral("/project/src/a.cpp"), true, false, false),
            checkpoint(42, QStringLiteral("/project/src/b.cpp"), true, false, false),
            checkpoint(43, QStringLiteral("/project/docs/guide.md"), false, false, false)
        }));
        QTRY_COMPARE(h.call("renderedCount").toInt(), 3);
        QCOMPARE(h.call("treeFolderCount").toInt(), 2);
        QCOMPARE(h.call("treeRowCount").toInt(), 5);

        // Folder delegates appear only after a rendered frame; retry the
        // collection instead of assuming the first layout pass created them.
        const auto visibleFolderRows = [&h]() {
            QList<QQuickItem*> items;
            collectVisualItems(h.window->contentItem(),
                               QStringLiteral("fileTreeFolderRow"), &items);
            int visible = 0;
            for (QQuickItem* item : items)
                if (item->isVisible()) ++visible;
            return visible;
        };
        QTest::qWait(150);
        (void)h.window->grabWindow();
        QTRY_COMPARE_WITH_TIMEOUT(visibleFolderRows(), 2, 5000);

        QList<QQuickItem*> folders;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("fileTreeFolderRow"), &folders);
        QMap<QString, QQuickItem*> byName;
        for (QQuickItem* row : folders) {
            if (row->isVisible())
                byName.insert(row->property("modelData").toMap().value("name").toString(), row);
        }
        QCOMPARE(byName.size(), 2);
        QCOMPARE(byName.value(QStringLiteral("src"))
                     ->property("modelData").toMap().value("fileCount").toInt(), 2);

        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "toggleFolder",
                                          Q_ARG(QVariant, QStringLiteral("src"))));
        QTRY_COMPARE(h.call("renderedCount").toInt(), 1);
        QCOMPARE(h.call("treeRowCount").toInt(), 3);
        QVariant collapsed;
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "folderCollapsed",
                                          Q_RETURN_ARG(QVariant, collapsed),
                                          Q_ARG(QVariant, QStringLiteral("src"))));
        QCOMPARE(collapsed.toBool(), true);

        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "toggleFolder",
                                          Q_ARG(QVariant, QStringLiteral("src"))));
        QTRY_COMPARE(h.call("renderedCount").toInt(), 3);
        QCOMPARE(h.call("treeRowCount").toInt(), 5);
    }

    // 审阅升级：统一视图带行号，并排视图把 -/+ 段配对对齐。
    void splitViewPairsChanges() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));

        QVariantMap item =
            checkpoint(51, QStringLiteral("/project/src/a.cpp"), true, false, false);
        item["hasDiff"] = true;
        item["addedLines"] = 1;
        item["removedLines"] = 1;
        QVERIFY(h.setCheckpoints({item}));
        QTRY_COMPARE(h.call("renderedCount").toInt(), 1);

        const QString diff = QStringLiteral(
            "--- a/a.cpp\n"
            "+++ b/a.cpp\n"
            "@@ -1 +1 @@\n"
            "-old\n"
            "+new\n");
        QVariantMap detail;
        detail["diff"] = diff;
        detail["diffTruncated"] = false;
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "setDetail",
                                          Q_ARG(QVariant, detail)));
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "openDetail",
                                          Q_ARG(QVariant, 0)));
        QTRY_VERIFY(h.call("detailVisible").toBool());
        QTRY_COMPARE(h.call("detailDiffLineCount").toInt(), 5);

        QList<QQuickItem*> lines;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("diffLine"), &lines);
        QVERIFY(lines.size() >= 5);
        bool sawOldNumber = false;
        bool sawNewNumber = false;
        for (QQuickItem* line : lines) {
            const QVariantMap data = line->property("modelData").toMap();
            if (data.value("oldNo").toInt() > 0) sawOldNumber = true;
            if (data.value("newNo").toInt() > 0) sawNewNumber = true;
        }
        QVERIFY2(sawOldNumber && sawNewNumber, "统一视图缺少旧/新行号");

        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "setViewMode",
                                          Q_ARG(QVariant, QStringLiteral("split"))));
        QTRY_COMPARE(h.call("detailDiffLineCount").toInt(), 4);
        QList<QQuickItem*> pairs;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("diffSplitRow"), &pairs);
        QCOMPARE(pairs.size(), 1);
        const QVariantMap pair = pairs.first()->property("modelData").toMap();
        QCOMPARE(pair.value("oldText").toString(), QStringLiteral("old"));
        QCOMPARE(pair.value("newText").toString(), QStringLiteral("new"));
        QCOMPARE(pair.value("oldNo").toInt(), 1);
        QCOMPARE(pair.value("newNo").toInt(), 1);
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "closeDetail"));
    }

    // 审阅升级：弹窗左侧文件树可在多文件间切换，上/下一个同步边界。
    void dialogSwitchesBetweenFiles() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setCheckpoints({
            checkpoint(61, QStringLiteral("/project/src/one.cpp"), true, false, false),
            checkpoint(62, QStringLiteral("/project/src/two.cpp"), true, false, false)
        }));

        QVariantMap detailOne;
        detailOne["diff"] = QStringLiteral(
            "--- a/one.cpp\n"
            "+++ b/one.cpp\n"
            "@@ -1 +1 @@\n"
            "-old\n"
            "+one\n");
        QVariantMap detailTwo;
        detailTwo["diff"] = QStringLiteral(
            "--- a/two.cpp\n"
            "+++ b/two.cpp\n"
            "@@ -1 +1 @@\n"
            "-old\n"
            "+two\n");
        QVariantMap details;
        details[QStringLiteral("61")] = detailOne;
        details[QStringLiteral("62")] = detailTwo;
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "setDetailById",
                                          Q_ARG(QVariant, details)));

        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "openDetail",
                                          Q_ARG(QVariant, 0)));
        QTRY_VERIFY(h.call("detailVisible").toBool());
        QCOMPARE(h.call("detailPath").toString(),
                 QStringLiteral("/project/src/one.cpp"));

        // The tree must stay at its capped width and leave room for the diff.
        QTest::qWait(150);
        QList<QQuickItem*> diffViews;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("detailDiffView"), &diffViews);
        QVERIFY2(!diffViews.isEmpty(), "detailDiffView missing");
        QVERIFY2(diffViews.first()->width() > 100,
                 "file tree squeezed the detail diff pane");

        QObject* dlg = h.call("dialogItem").value<QObject*>();
        QVERIFY(dlg);
        QCOMPARE(dlg->property("treeVisible").toBool(), true);
        QCOMPARE(dlg->property("canStepForward").toBool(), true);
        QCOMPARE(dlg->property("canStepBack").toBool(), false);

        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "selectEntry",
                                          Q_ARG(QVariant, 1)));
        QTRY_COMPARE(h.call("detailPath").toString(),
                     QStringLiteral("/project/src/two.cpp"));
        QCOMPARE(dlg->property("canStepForward").toBool(), false);
        QCOMPARE(dlg->property("canStepBack").toBool(), true);
        QVERIFY2(dlg->property("diffText").toString().contains(QStringLiteral("+two")),
                 qPrintable(dlg->property("diffText").toString()));

        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "stepEntry",
                                          Q_ARG(QVariant, -1)));
        QTRY_COMPARE(h.call("detailPath").toString(),
                     QStringLiteral("/project/src/one.cpp"));
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "closeDetail"));
    }

    // 审阅升级：树行撤销按钮（IconButton 替换旧 AppButton）仍进入确认链路。
    void treeRowActionOpensUndoConfirmation() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));

        QVariantMap item =
            checkpoint(71, QStringLiteral("/project/src/undo.cpp"), true, false, false);
        item["hasDiff"] = true;
        item["addedLines"] = 2;
        QVERIFY(h.setCheckpoints({item}));
        QTRY_COMPARE(h.call("renderedCount").toInt(), 1);

        QTest::qWait(150);
        (void)h.window->grabWindow();
        QTest::qWait(150);

        QList<QQuickItem*> actions;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("fileRowAction"), &actions);
        QQuickItem* action = nullptr;
        for (QQuickItem* candidate : actions) {
            if (candidate->isVisible()) {
                action = candidate;
                break;
            }
        }
        QVERIFY2(action, "撤销按钮未渲染");
        const QPoint center =
            action->mapToScene(QPointF(action->width() / 2,
                                       action->height() / 2)).toPoint();
        QTest::mouseClick(h.window, Qt::LeftButton, Qt::NoModifier, center);
        QTRY_VERIFY(h.call("undoDialogVisible").toBool());
    }

    // Phase 1 layout chain: the review panel hosted in a tab slot with
    // listMaxHeight 0 must take the tab's height and render its rows.
    void reviewPanelFillsTabHeight() {
        Harness h(QStringLiteral("ReviewTabHarness.qml"));
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setCheckpoints({
            checkpoint(81, QStringLiteral("/project/src/a.cpp"), true, false, false),
            checkpoint(82, QStringLiteral("/project/src/b.cpp"), true, false, false),
            checkpoint(83, QStringLiteral("/project/docs/guide.md"), false, false, false)
        }));
        QTRY_COMPARE(h.call("renderedCount").toInt(), 3);
        QTRY_VERIFY_WITH_TIMEOUT(h.call("panelHeight").toReal() > 250, 5000);

        QTest::qWait(150);
        (void)h.window->grabWindow();
        const auto visibleRows = [&h]() {
            QList<QQuickItem*> rows;
            collectVisualItems(h.window->contentItem(),
                               QStringLiteral("fileChangeRow"), &rows);
            int visible = 0;
            for (QQuickItem* row : rows)
                if (row->isVisible()) ++visible;
            return visible;
        };
        QTRY_COMPARE_WITH_TIMEOUT(visibleRows(), 3, 5000);
        qInfo() << "Review tab panel height:" << h.call("panelHeight").toReal()
                << "px; visible file rows:" << visibleRows();
    }

};

QTEST_MAIN(TestFileChangesQml)
#include "test_file_changes_qml.moc"
