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

// A Dialog is a QQuickPopup: it is not part of the item tree an objectName
// search can reach, so the review dialog is located by the properties only
// ReviewDialog declares (opened comes from Popup, the rest from the dialog).
QObject* findReviewDialog(QObject* root) {
    if (!root)
        return nullptr;
    const QMetaObject* meta = root->metaObject();
    if (meta->indexOfProperty("opened") >= 0 && meta->indexOfProperty("fileCount") >= 0
        && meta->indexOfProperty("reviewPanel") >= 0)
        return root;
    for (QObject* child : root->children()) {
        if (QObject* hit = findReviewDialog(child))
            return hit;
    }
    return nullptr;
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

    // Generic harness plumbing. Most harnesses in tests/qml expose plain
    // setter functions; only the review-panel one needs a typed helper, so
    // everything else goes through here instead of one method per harness.
    template <typename T>
    bool set(const char* fn, const T& value) const {
        return root && QMetaObject::invokeMethod(root.get(), fn,
                                                 Q_ARG(QVariant, QVariant::fromValue(value)));
    }
    bool invoke(const char* fn) const {
        return root && QMetaObject::invokeMethod(root.get(), fn);
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

    // Phase 1 layout chain: a panel hosted in a fill-height slot with
    // listMaxHeight 0 must take the slot's height and render its rows.
    // The review moved into a dialog, but the contract is the same one the
    // dialog relies on (its body is a fill-height slot with listMaxHeight 0).
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

    // 审阅内嵌分栏（纵向轴）：分隔条存在、可拖动，双击复位到默认比例。
    //
    // 该用例守住面板组件自身的「窄栏回退」：面板的默认轴仍是纵向（树在
    // 上、差异在下），任何窄宿主都能直接用它。审阅弹窗走横向轴（树在左、
    // 差异在右），由 reviewDialogDividerMovesAndResets 守住。
    void stackedDividerMovesAndResets() {
        Harness h(QStringLiteral("ReviewTabHarness.qml"));
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setCheckpoints({
            checkpoint(91, QStringLiteral("/project/src/a.cpp"), true, false, false),
            checkpoint(92, QStringLiteral("/project/src/b.cpp"), true, false, false),
            checkpoint(93, QStringLiteral("/project/docs/guide.md"), false, false, false)
        }));
        QTRY_COMPARE(h.call("renderedCount").toInt(), 3);

        // The divider itself must be on screen between tree and diff.
        QTest::qWait(200);
        (void)h.window->grabWindow();
        QList<QQuickItem*> dividers;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("reviewSplitBar"), &dividers);
        QCOMPARE(dividers.size(), 1);
        QVERIFY(dividers.first()->isVisible());
        QVERIFY(dividers.first()->height() >= 4.0);
        // A horizontal seam: it spans the panel and is only a few pixels
        // tall - the stacked axis puts the tree above the diff.
        QVERIFY(dividers.first()->width() > 100.0);

        // The tree sits above the diff in this axis.
        QList<QQuickItem*> trees;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("changeTree"), &trees);
        QVERIFY(!trees.isEmpty());
        QList<QQuickItem*> diffViews;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("detailDiffView"), &diffViews);
        QVERIFY(!diffViews.isEmpty());
        const double treeBottom =
            trees.first()->mapToScene(QPointF(0, trees.first()->height())).y();
        const double diffTop = diffViews.first()->mapToScene(QPointF(0, 0)).y();
        QVERIFY2(diffTop >= treeBottom - 1.0,
                 qPrintable(QStringLiteral("纵向轴下差异不在树的下方（%1 < %2）")
                                .arg(diffTop).arg(treeBottom)));

        // Dragging the divider up gives the diff pane more room.
        const double before = h.call("treeHeight").toReal();
        QVERIFY(before > 0.0);
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "dragSplit",
                                          Q_ARG(QVariant, -60.0)));
        QTRY_VERIFY(h.call("treeHeight").toReal() < before - 10.0);

        // Double-click restores the default share.
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "resetSplit"));
        QTRY_VERIFY_WITH_TIMEOUT(
            std::abs(h.call("treeHeight").toReal() - before) < 2.0, 5000);
    }

    // 面板的窄栏回退：面板的默认轴是纵向（树在上、差异在下），所以即使
    // 审阅搬进弹窗，任何窄宿主仍能直接用这个组件——用例守住它在窄宽度
    // 下两半都可用（列表出行、差异有可读宽度、操作行不溢出）。
    //
    // 注：右栏不再承载审阅（它已搬进 ReviewDialog），所以这条不再是产品
    // 面的约束，而是组件自身的降级契约。
    void narrowPanelKeepsBothHalvesUsable() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));

        QVariantMap item =
            checkpoint(94, QStringLiteral("/project/src/narrow.cpp"), true, false, false);
        item["hasDiff"] = true;
        item["addedLines"] = 1;
        QVERIFY(h.setCheckpoints({item}));
        QTRY_COMPARE(h.call("renderedCount").toInt(), 1);

        // 220px 面板宽度减去 ContextPanel 自身的 2x20px 外边距，
        // 即审阅页签真正可用的内容宽度。
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "setPanelWidth",
                                          Q_ARG(QVariant, 180.0)));
        QTRY_COMPARE(h.call("panelWidth").toReal(), 180.0);

        QVariantMap detail;
        detail["diff"] = QStringLiteral(
            "--- a/narrow.cpp\n"
            "+++ b/narrow.cpp\n"
            "@@ -1 +1 @@\n"
            "-old\n"
            "+new\n");
        detail["diffTruncated"] = false;
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "setDetail",
                                          Q_ARG(QVariant, detail)));
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "openDetail",
                                          Q_ARG(QVariant, 0)));
        QTRY_VERIFY(h.call("detailVisible").toBool());

        QTest::qWait(150);
        (void)h.window->grabWindow();
        QTest::qWait(150);

        // The file list still shows its row at the minimum width.
        QList<QQuickItem*> rows;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("fileChangeRow"), &rows);
        int visibleRows = 0;
        for (QQuickItem* row : rows)
            if (row->isVisible()) ++visibleRows;
        QCOMPARE(visibleRows, 1);

        // The diff pane keeps a usable width and height.
        QList<QQuickItem*> diffViews;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("detailDiffView"), &diffViews);
        QVERIFY2(!diffViews.isEmpty(), "detailDiffView missing");
        QVERIFY2(diffViews.first()->width() >= 150.0,
                 qPrintable(QStringLiteral("差异区被压窄（%1px）")
                                .arg(diffViews.first()->width())));
        QVERIFY2(diffViews.first()->height() > 100.0,
                 "差异区高度塌陷");

        // The action row (统一/并排/撤销) must fit inside the pane.
        QList<QQuickItem*> undoButtons;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("undoButton"), &undoButtons);
        QVERIFY2(!undoButtons.isEmpty(), "撤销按钮未渲染");
        QQuickItem* undo = undoButtons.first();
        const double paneRight =
            diffViews.first()->mapToScene(QPointF(diffViews.first()->width(), 0)).x();
        const double undoRight =
            undo->mapToScene(QPointF(undo->width(), 0)).x();
        QVERIFY2(undoRight <= paneRight + 1.0,
                 qPrintable(QStringLiteral("操作行溢出最小宽度面板（%1 > %2）")
                                .arg(undoRight).arg(paneRight)));
    }


    // ── DiffView split-column geometry ────────────────
    // The old|new divide used to be a hard 50% of the row width, so a
    // one-word edit beside a rewritten paragraph wrapped the long side into
    // a staircase while the short side sat on whitespace. These cases lock
    // the three behaviours that replaced it: a content-aware default, a
    // draggable seam, and a clamp that keeps either column usable.

    // A diff whose OLD side is short and NEW side is long must give the NEW
    // side more than half — that is the whole point of the auto divide.
    // Line lengths are built to exact sizes so the expected ratio is exact
    // rather than hand-waved.
    void splitDefaultsToContentNotFiftyFifty() {
        Harness h(QStringLiteral("DiffViewHarness.qml"));
        QVERIFY2(h.window, qPrintable(h.error));

        // ~100 chars old vs ~200 chars new: together they exceed what the
        // 900px harness can show unwrapped, so the divide has to pick a side.
        const QString oldLine = QString(100, QLatin1Char('a'));
        const QString newLine = QString(200, QLatin1Char('b'));
        QString diff;
        diff += "--- a/f.md\n+++ b/f.md\n@@ -1,1 +1,1 @@\n";
        diff += "-" + oldLine + "\n";
        diff += "+" + newLine + "\n";
        QVERIFY(h.set("setDiff", diff));
        QTRY_VERIFY(h.call("hasDiff").toBool());

        // Expected: old share = 100 / 300, inside the 0.25..0.75 band.
        const double expected = 100.0 / 300.0;
        const double autoRatio = h.call("autoRatio").toDouble();
        QVERIFY2(qAbs(autoRatio - expected) < 0.02,
                 qPrintable(QStringLiteral("auto divide %1, expected ~%2")
                                .arg(autoRatio).arg(expected)));
        QVERIFY2(autoRatio < 0.5,
                 qPrintable(QStringLiteral("auto divide did not favour the long side: %1")
                                .arg(autoRatio)));

        // ...and the live ratio adopted it without anyone touching it.
        QCOMPARE(h.call("ratio").toDouble(), autoRatio);
        QVERIFY2(!h.call("touched").toBool(),
                 "auto divide must not count as a user edit");

        // The seam really is left of centre, i.e. the new column is wider.
        const double seam = h.call("seamX").toDouble();
        QVERIFY2(seam < h.call("viewWidth").toDouble() / 2.0,
                 qPrintable(QStringLiteral("seam not left of centre: %1").arg(seam)));
    }

    // A lopsided diff beyond the band must clamp rather than collapse a
    // column: 10 chars old vs 400 new would otherwise ask for a 2% share.
    void splitClampsExtremeContentSkew() {
        Harness h(QStringLiteral("DiffViewHarness.qml"));
        QVERIFY2(h.window, qPrintable(h.error));

        QString diff;
        diff += "--- a/f.md\n+++ b/f.md\n@@ -1,1 +1,1 @@\n";
        diff += "-" + QString(10, QLatin1Char('a')) + "\n";
        diff += "+" + QString(400, QLatin1Char('b')) + "\n";
        QVERIFY(h.set("setDiff", diff));
        QTRY_VERIFY(h.call("hasDiff").toBool());

        // 10/410 = 0.024, so the 0.25 floor is what should apply.
        QCOMPARE(h.call("autoRatio").toDouble(), 0.25);
    }

    // Equal-length sides gain nothing from an uneven divide, so the auto
    // value must stay at the neutral half.
    void splitStaysBalancedWhenSidesMatch() {
        Harness h(QStringLiteral("DiffViewHarness.qml"));
        QVERIFY2(h.window, qPrintable(h.error));

        QString diff;
        diff += "--- a/f.md\n+++ b/f.md\n@@ -1,1 +1,1 @@\n";
        diff += "-alpha beta\n";
        diff += "+gamma delta\n";
        QVERIFY(h.set("setDiff", diff));
        QTRY_VERIFY(h.call("hasDiff").toBool());
        QCOMPARE(h.call("autoRatio").toDouble(), 0.5);
        QCOMPARE(h.call("ratio").toDouble(), 0.5);
    }

    // Dragging moves the seam, latches the manual choice, and a later diff
    // must not silently undo it.
    void splitDragMovesSeamAndLatches() {
        Harness h(QStringLiteral("DiffViewHarness.qml"));
        QVERIFY2(h.window, qPrintable(h.error));

        QString diff;
        diff += "--- a/f.md\n+++ b/f.md\n@@ -1,1 +1,1 @@\n";
        diff += "-alpha beta\n";
        diff += "+gamma delta\n";
        QVERIFY(h.set("setDiff", diff));
        QTRY_VERIFY(h.call("hasDiff").toBool());
        QCOMPARE(h.call("ratio").toDouble(), 0.5);

        const double before = h.call("seamX").toDouble();
        // Drag right: the OLD column grows, so the ratio rises.
        QVERIFY(h.set("dragSplit", 120.0));
        const double afterRight = h.call("seamX").toDouble();
        QVERIFY2(afterRight > before + 50,
                 qPrintable(QStringLiteral("drag right did not move the seam: %1 -> %2")
                                .arg(before).arg(afterRight)));
        QVERIFY2(h.call("touched").toBool(), "dragging must latch the manual choice");

        // Drag back left past centre.
        QVERIFY(h.set("dragSplit", -260.0));
        QVERIFY2(h.call("seamX").toDouble() < afterRight - 100,
                 "drag left did not move the seam back");

        // A new diff arrives: the manual divide survives.
        const double held = h.call("ratio").toDouble();
        QString other;
        other += "--- a/g.md\n+++ b/g.md\n@@ -1,1 +1,1 @@\n";
        other += "-x\n";
        other += "+y\n";
        QVERIFY(h.set("setDiff", other));
        QTRY_VERIFY(h.call("hasDiff").toBool());
        QCOMPARE(h.call("ratio").toDouble(), held);
    }

    // The seam cannot be dragged into a sliver: both columns keep a usable
    // text area at either extreme.
    void splitClampsToUsableColumns() {
        Harness h(QStringLiteral("DiffViewHarness.qml"));
        QVERIFY2(h.window, qPrintable(h.error));

        QString diff;
        diff += "--- a/f.md\n+++ b/f.md\n@@ -1,1 +1,1 @@\n";
        diff += "-alpha beta\n";
        diff += "+gamma delta\n";
        QVERIFY(h.set("setDiff", diff));
        QTRY_VERIFY(h.call("hasDiff").toBool());

        // Shove the seam hard left, then hard right.
        QVERIFY(h.set("dragSplit", -100000.0));
        const double leftSeam = h.call("seamX").toDouble();
        QVERIFY(h.set("dragSplit", 200000.0));
        const double rightSeam = h.call("seamX").toDouble();

        const double w = h.call("viewWidth").toDouble();
        const bool gutters = h.call("showGutters").toBool();
        const double leftPad = gutters ? 40 : 4;
        const double rightPad = gutters ? 41 : 4;
        // minColumnText is 110 on each side.
        QVERIFY2(leftSeam >= leftPad + 110 - 1,
                 qPrintable(QStringLiteral("left column collapsed to %1px").arg(leftSeam)));
        QVERIFY2(rightSeam <= w - rightPad - 110 + 1,
                 qPrintable(QStringLiteral("right column collapsed: seam %1 of %2")
                                .arg(rightSeam).arg(w)));
        QVERIFY2(rightSeam > leftSeam, "clamps did not bracket the seam");
    }

    // Double-click returns to the content-aware divide and re-arms auto.
    void splitDoubleClickResetsToAuto() {
        Harness h(QStringLiteral("DiffViewHarness.qml"));
        QVERIFY2(h.window, qPrintable(h.error));

        QString diff;
        diff += "--- a/f.md\n+++ b/f.md\n@@ -1,2 +1,2 @@\n";
        diff += "-short\n";
        diff += "+this replacement line is deliberately much longer than the one it replaces so the auto divide has something to measure\n";
        QVERIFY(h.set("setDiff", diff));
        QTRY_VERIFY(h.call("hasDiff").toBool());

        QVERIFY(h.set("dragSplit", 150.0));
        QVERIFY2(h.call("touched").toBool(), "precondition: manual divide");

        QVERIFY(h.invoke("resetToAuto"));
        QVERIFY2(!h.call("touched").toBool(), "reset must re-arm auto");
        QCOMPARE(h.call("ratio").toDouble(), h.call("autoRatio").toDouble());
    }

    // The handle only exists in split mode — unified mode has one column and
    // a seam would be meaningless.
    void splitHandleHiddenInUnifiedMode() {
        Harness h(QStringLiteral("DiffViewHarness.qml"));
        QVERIFY2(h.window, qPrintable(h.error));

        QString diff;
        diff += "--- a/f.md\n+++ b/f.md\n@@ -1,1 +1,1 @@\n";
        diff += "-alpha beta\n";
        diff += "+gamma delta\n";
        QVERIFY(h.set("setDiff", diff));
        QTRY_VERIFY(h.call("hasDiff").toBool());

        QList<QQuickItem*> handles;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("diffSplitHandle"), &handles);
        QCOMPARE(handles.size(), 1);
        QTRY_VERIFY(handles.first()->isVisible());

        QVERIFY(h.set("setMode", QStringLiteral("unified")));
        QTRY_VERIFY(!handles.first()->isVisible());
    }

    // ── Review dialog (the panel's wide host) ────────────────
    //
    // The review moved out of the 220..640px sidebar into a dialog, and
    // the split changed shape with it: the tree now stands left of the
    // diff (horizontal axis) so the diff gets the remaining width *and*
    // the full dialog height. These cases lock that new reality in.

    // The dialog opens, prints the session's change summary in its header
    // and renders the changed files.
    void reviewDialogOpensWithSummary() {
        Harness h(QStringLiteral("ReviewDialogHarness.qml"));
        QVERIFY2(h.window, qPrintable(h.error));

        QVariantMap first =
            checkpoint(101, QStringLiteral("/project/src/a.cpp"), true, false, false);
        first["hasDiff"] = true;
        first["addedLines"] = 4;
        first["removedLines"] = 2;
        QVariantMap second =
            checkpoint(102, QStringLiteral("/project/docs/guide.md"), false, false, false);
        second["addedLines"] = 1;
        QVERIFY(h.setCheckpoints({first, second}));

        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "openDialog"));
        QTRY_VERIFY(h.call("dialogVisible").toBool());
        QTest::qWait(200);
        (void)h.window->grabWindow();

        // The dialog is the wide one - the whole point of the move.
        QVERIFY2(h.call("dialogWidth").toReal() >= 900.0,
                 qPrintable(QStringLiteral("审阅弹窗宽度不足（%1px）")
                                .arg(h.call("dialogWidth").toReal())));

        // The summary mirrors the panel's counters.
        QCOMPARE(h.call("fileCount").toInt(), 2);
        QCOMPARE(h.call("addedLines").toInt(), 5);
        QCOMPARE(h.call("removedLines").toInt(), 2);

        const auto summaryText = [&h](const char* name) {
            QList<QQuickItem*> items;
            collectVisualItems(h.window->contentItem(),
                               QString::fromLatin1(name), &items);
            if (items.isEmpty()) return QString();
            return items.first()->property("text").toString();
        };
        QVERIFY(summaryText("reviewSummaryFiles").contains(QStringLiteral("2")));
        QCOMPARE(summaryText("reviewSummaryAdded"), QStringLiteral("+5"));
        QCOMPARE(summaryText("reviewSummaryRemoved"), QStringLiteral("−2"));

        // The file list is rendered inside the dialog.
        QList<QQuickItem*> rows;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("fileChangeRow"), &rows);
        int visibleRows = 0;
        for (QQuickItem* row : rows)
            if (row->isVisible()) ++visibleRows;
        QCOMPARE(visibleRows, 2);

        // The dialog is really on screen (it covers the window colour) and
        // it really paints something inside its own background.
        const QColor windowBg(h.call("windowBackgroundColor").toString());
        const QColor dialogBg(h.call("dialogBackgroundColor").toString());
        QVERIFY(windowBg.isValid());
        QVERIFY(dialogBg.isValid());
        const QImage shot = h.window->grabWindow();
        QVERIFY2(!shot.isNull(), "整窗抓图失败（offscreen 渲染异常）");
        const int covered = countNonBackground(shot, windowBg);
        QVERIFY2(covered > 100000,
                 qPrintable(QStringLiteral("审阅弹窗未覆盖窗口（%1 像素）")
                                .arg(covered)));
        const int painted = countNonBackground(shot, dialogBg);
        QVERIFY2(painted > 1000,
                 qPrintable(QStringLiteral("审阅弹窗内部为空白（%1 像素）")
                                .arg(painted)));
    }

    // A session that touched no file shows the dialog's own empty state -
    // the panel hides itself, so without it the dialog is a blank box.
    void reviewDialogShowsEmptyState() {
        Harness h(QStringLiteral("ReviewDialogHarness.qml"));
        QVERIFY2(h.window, qPrintable(h.error));

        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "openDialog"));
        QTRY_VERIFY(h.call("dialogVisible").toBool());
        QTRY_VERIFY(h.call("emptyStateVisible").toBool());
        QCOMPARE(h.call("fileCount").toInt(), 0);

        QList<QQuickItem*> emptyStates;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("reviewEmptyState"), &emptyStates);
        QCOMPARE(emptyStates.size(), 1);
        QVERIFY(emptyStates.first()->isVisible());

        // The panel itself is hidden while there is nothing to review.
        QList<QQuickItem*> panels;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("reviewPanel"), &panels);
        QCOMPARE(panels.size(), 1);
        QVERIFY(!panels.first()->isVisible());
    }

    // The wide axis: the tree stands left of the diff, the divider is a
    // vertical seam, and the diff keeps both a wide and a tall box.
    void reviewDialogSplitsSideBySide() {
        Harness h(QStringLiteral("ReviewDialogHarness.qml"));
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setCheckpoints({
            checkpoint(111, QStringLiteral("/project/src/a.cpp"), true, false, false),
            checkpoint(112, QStringLiteral("/project/src/b.cpp"), true, false, false),
            checkpoint(113, QStringLiteral("/project/docs/guide.md"), false, false, false)
        }));
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "openDialog"));
        QTRY_VERIFY(h.call("dialogVisible").toBool());
        QTest::qWait(250);
        (void)h.window->grabWindow();

        QList<QQuickItem*> trees;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("changeTree"), &trees);
        QVERIFY(!trees.isEmpty());
        QList<QQuickItem*> diffViews;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("detailDiffView"), &diffViews);
        QVERIFY(!diffViews.isEmpty());

        const double treeRight =
            trees.first()->mapToScene(QPointF(trees.first()->width(), 0)).x();
        const double diffLeft = diffViews.first()->mapToScene(QPointF(0, 0)).x();
        QVERIFY2(diffLeft >= treeRight,
                 qPrintable(QStringLiteral("差异不在树的右侧（树右沿 %1，差异左沿 %2）")
                                .arg(treeRight).arg(diffLeft)));

        // The tree is a path list: it keeps a column, not half the dialog.
        const double treeWidth = trees.first()->width();
        QVERIFY2(treeWidth >= 150.0 && treeWidth <= 460.0,
                 qPrintable(QStringLiteral("文件树列宽异常（%1px）").arg(treeWidth)));

        // The diff is the wide one - and it is tall, because the tree no
        // longer steals half the height.
        QVERIFY2(diffViews.first()->width() >= 400.0,
                 qPrintable(QStringLiteral("差异区宽度不足（%1px）")
                                .arg(diffViews.first()->width())));
        QVERIFY2(diffViews.first()->height() >= 300.0,
                 qPrintable(QStringLiteral("差异区高度不足（%1px）")
                                .arg(diffViews.first()->height())));

        // The divider is a vertical seam spanning the whole body.
        QList<QQuickItem*> dividers;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("reviewSplitBar"), &dividers);
        QCOMPARE(dividers.size(), 1);
        QVERIFY(dividers.first()->isVisible());
        QVERIFY(dividers.first()->width() >= 4.0);
        QVERIFY(dividers.first()->height() > 200.0);
        QCOMPARE(dividers.first()->property("orientation").toString(),
                 QStringLiteral("horizontal"));
    }

    // The divider still drags and still resets - now horizontally.
    void reviewDialogDividerMovesAndResets() {
        Harness h(QStringLiteral("ReviewDialogHarness.qml"));
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setCheckpoints({
            checkpoint(121, QStringLiteral("/project/src/a.cpp"), true, false, false),
            checkpoint(122, QStringLiteral("/project/src/b.cpp"), true, false, false)
        }));
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "openDialog"));
        QTRY_VERIFY(h.call("dialogVisible").toBool());
        QTest::qWait(200);

        const double before = h.call("treeWidth").toReal();
        QVERIFY(before > 0.0);

        // Dragging the seam left hands the space to the diff.
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "dragSplit",
                                          Q_ARG(QVariant, -140.0)));
        QTRY_VERIFY(h.call("treeWidth").toReal() < before - 60.0);

        // Double-click (reset) restores the default share.
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "resetSplit"));
        QTRY_VERIFY_WITH_TIMEOUT(
            std::abs(h.call("treeWidth").toReal() - before) < 2.0, 5000);
    }

    // Everything the sidebar host offered still works in the dialog:
    // opening a diff, stepping between files and the 统一/并排 toggle.
    void reviewDialogDiffPaneStaysUsable() {
        Harness h(QStringLiteral("ReviewDialogHarness.qml"));
        QVERIFY2(h.window, qPrintable(h.error));

        QVariantMap first =
            checkpoint(131, QStringLiteral("/project/src/one.cpp"), true, false, false);
        first["hasDiff"] = true;
        first["addedLines"] = 1;
        first["removedLines"] = 1;
        QVariantMap second =
            checkpoint(132, QStringLiteral("/project/src/two.cpp"), true, false, false);
        second["hasDiff"] = true;
        QVERIFY(h.setCheckpoints({first, second}));

        QVariantMap detailOne;
        detailOne["diff"] = QStringLiteral(
            "--- a/one.cpp\n"
            "+++ b/one.cpp\n"
            "@@ -1 +1 @@\n"
            "-old\n"
            "+one\n");
        detailOne["diffTruncated"] = false;
        QVariantMap detailTwo;
        detailTwo["diff"] = QStringLiteral(
            "--- a/two.cpp\n"
            "+++ b/two.cpp\n"
            "@@ -1 +1 @@\n"
            "-old\n"
            "+two\n");
        QVariantMap details;
        details[QStringLiteral("131")] = detailOne;
        details[QStringLiteral("132")] = detailTwo;
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "setDetailById",
                                          Q_ARG(QVariant, details)));

        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "openDialog"));
        QTRY_VERIFY(h.call("dialogVisible").toBool());
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "openDetail",
                                          Q_ARG(QVariant, 0)));
        QTRY_VERIFY(h.call("detailVisible").toBool());
        QCOMPARE(h.call("detailPath").toString(),
                 QStringLiteral("/project/src/one.cpp"));
        QTRY_VERIFY(h.call("detailDiffLineCount").toInt() >= 5);

        QTest::qWait(200);
        (void)h.window->grabWindow();
        QList<QQuickItem*> diffViews;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("detailDiffView"), &diffViews);
        QVERIFY2(!diffViews.isEmpty(), "detailDiffView missing");
        QVERIFY2(diffViews.first()->width() >= 400.0,
                 qPrintable(QStringLiteral("弹窗内差异区被压窄（%1px）")
                                .arg(diffViews.first()->width())));
        QVERIFY2(diffViews.first()->height() >= 300.0,
                 "弹窗内差异区高度塌陷");

        // The file stepping buttons still work.
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "stepEntry",
                                          Q_ARG(QVariant, 1)));
        QTRY_COMPARE(h.call("detailPath").toString(),
                     QStringLiteral("/project/src/two.cpp"));

        // ...and so does the 统一/并排 toggle.
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "setViewMode",
                                          Q_ARG(QVariant, QStringLiteral("split"))));
        QTRY_COMPARE(h.call("detailDiffLineCount").toInt(), 4);
        QList<QQuickItem*> pairs;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("diffSplitRow"), &pairs);
        QCOMPARE(pairs.size(), 1);
        const QVariantMap pair = pairs.first()->property("modelData").toMap();
        QCOMPARE(pair.value("newText").toString(), QStringLiteral("two"));
    }

    // Esc closes the dialog (the house close policy).
    void reviewDialogClosesOnEscape() {
        Harness h(QStringLiteral("ReviewDialogHarness.qml"));
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setCheckpoints({
            checkpoint(141, QStringLiteral("/project/src/a.cpp"), true, false, false)
        }));
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "openDialog"));
        QTRY_VERIFY(h.call("dialogVisible").toBool());
        QTest::keyClick(h.window, Qt::Key_Escape);
        QTRY_VERIFY(!h.call("dialogVisible").toBool());
    }
};

QTEST_MAIN(TestFileChangesQml)
#include "test_file_changes_qml.moc"
