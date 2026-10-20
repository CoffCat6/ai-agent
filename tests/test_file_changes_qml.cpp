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
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>

#include <cmath>
#include <memory>

#include "core/git/GitService.h"
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

// Phase 3 end-to-end fixture: a real repository for the dialog's GitVM path.
class TempRepo {
public:
    bool valid() const { return m_dir.isValid(); }
    QString path() const { return m_dir.path(); }
    QString filePath(const QString& relative) const { return m_dir.filePath(relative); }

    bool init() {
        if (!m_dir.isValid()) return false;
        return run({QStringLiteral("init"), QStringLiteral("-q")})
            && run({QStringLiteral("config"), QStringLiteral("user.name"),
                    QStringLiteral("Test User")})
            && run({QStringLiteral("config"), QStringLiteral("user.email"),
                    QStringLiteral("test@example.com")})
            && run({QStringLiteral("config"), QStringLiteral("commit.gpgsign"),
                    QStringLiteral("false")})
            && run({QStringLiteral("config"), QStringLiteral("core.autocrlf"),
                    QStringLiteral("false")});
    }

    bool write(const QString& relative, const QByteArray& content) const {
        QFile file(filePath(relative));
        if (!file.open(QIODevice::WriteOnly)) return false;
        return file.write(content) == content.size();
    }

    bool commit(const QString& message) const {
        return run({QStringLiteral("add"), QStringLiteral("-A")})
            && run({QStringLiteral("commit"), QStringLiteral("-q"),
                    QStringLiteral("-m"), message});
    }

    bool run(const QStringList& args) const {
        QProcess process;
        process.setProgram(QStringLiteral("git"));
        process.setArguments(args);
        process.setWorkingDirectory(m_dir.path());
        process.setProcessChannelMode(QProcess::MergedChannels);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
        env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
        env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
        process.setProcessEnvironment(env);
        process.start();
        if (!process.waitForStarted(3000)) return false;
        if (!process.waitForFinished(15000)) {
            process.kill();
            process.waitForFinished(1000);
            return false;
        }
        return process.exitCode() == 0;
    }

private:
    QTemporaryDir m_dir;
};

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
            &engine, QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR "/FileChangesHarness.qml")));
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

    bool setGitAutoLoad(bool value) const {
        return root && QMetaObject::invokeMethod(root.get(), "setGitAutoLoad",
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

    // Phase 3: the dialog's git tabs render injected working-tree and history
    // data (the async data path is covered by test_git_viewmodel).
    void gitTabsRenderWorkingTreeAndHistory() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));

        QVariantMap item =
            checkpoint(11, QStringLiteral("/project/src/a.cpp"), true, false, false);
        item["hasDiff"] = true;
        item["addedLines"] = 1;
        item["removedLines"] = 1;
        QVERIFY(h.setCheckpoints({item}));
        QTRY_COMPARE(h.call("renderedCount").toInt(), 1);
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "openDetail",
                                          Q_ARG(QVariant, 0)));
        QTRY_VERIFY(h.call("detailVisible").toBool());

        QObject* dlg = h.call("dialogItem").value<QObject*>();
        QVERIFY(dlg);

        const QString workingDiff = QStringLiteral(
            "--- a/a.cpp\n"
            "+++ b/a.cpp\n"
            "@@ -1,2 +1,2 @@\n"
            "-old\n"
            "+new\n");
        const QString historyDiff = QStringLiteral(
            "--- a/a.cpp\n"
            "+++ b/a.cpp\n"
            "@@ -1 +1 @@\n"
            "-first\n"
            "+second\n");
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        const QVariantList commits{
            QVariantMap{{"sha", QString(40, QLatin1Char('a'))},
                        {"shortSha", QStringLiteral("aaaaaaa")},
                        {"subject", QStringLiteral("edit a.cpp")},
                        {"author", QStringLiteral("Test User")},
                        {"timestamp", now}},
            QVariantMap{{"sha", QString(40, QLatin1Char('b'))},
                        {"shortSha", QStringLiteral("bbbbbbb")},
                        {"subject", QStringLiteral("add a.cpp")},
                        {"author", QStringLiteral("Test User")},
                        {"timestamp", now - 60}}};

        dlg->setProperty("gitRepo", true);
        dlg->setProperty("gitRepoKnown", true);
        dlg->setProperty("workingDiffLoading", false);
        dlg->setProperty("workingDiff", workingDiff);
        dlg->setProperty("historyLoading", false);
        dlg->setProperty("history", commits);
        dlg->setProperty("commitDiffLoading", false);
        dlg->setProperty("commitDiff", historyDiff);
        dlg->setProperty("selectedSha", commits.first().toMap().value("sha"));
        dlg->setProperty("currentTab", 1);
        QTest::qWait(200);

        QList<QQuickItem*> tabs;
        collectVisualItems(h.window->contentItem(), QStringLiteral("workingTab"), &tabs);
        QCOMPARE(tabs.size(), 1);
        QVERIFY(tabs.first()->isVisible());

        QList<QQuickItem*> workingViews;
        collectVisualItems(h.window->contentItem(), QStringLiteral("workingDiffView"),
                           &workingViews);
        QCOMPARE(workingViews.size(), 1);
        QVERIFY2(workingViews.first()->property("renderedCount").toInt() >= 4,
                 "工作区差异未渲染");
        QCOMPARE(workingViews.first()->property("diffText").toString(), workingDiff);

        dlg->setProperty("currentTab", 2);
        QTest::qWait(200);
        QTRY_COMPARE(dlg->property("gitHistoryRenderedCount").toInt(), 2);
        QList<QQuickItem*> rows;
        collectVisualItems(h.window->contentItem(), QStringLiteral("gitCommitRow"), &rows);
        QCOMPARE(rows.size(), 2);

        QList<QQuickItem*> commitViews;
        collectVisualItems(h.window->contentItem(), QStringLiteral("commitDiffView"),
                           &commitViews);
        QCOMPARE(commitViews.size(), 1);
        QVERIFY2(commitViews.first()->property("renderedCount").toInt() >= 4,
                 "提交差异未渲染");
        QCOMPARE(commitViews.first()->property("diffText").toString(), historyDiff);

        // Outside a repository the git tabs disappear and the dialog falls
        // back to the checkpoint view.
        dlg->setProperty("gitRepo", false);
        dlg->setProperty("currentTab", 2);
        QTest::qWait(100);
        QTRY_VERIFY(!tabs.first()->isVisible());
        QCOMPARE(dlg->property("effectiveTab").toInt(), 0);

        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "closeDetail"));
    }

    // Phase 3 end-to-end: dialog -> GitVM -> GitService -> rendered rows.
    void realGitIntegrationLoadsHistory() {
        QString gitError;
        if (!GitService::isGitAvailable(&gitError))
            QSKIP(qPrintable(QStringLiteral("git is not available: %1").arg(gitError)));

        TempRepo repo;
        QVERIFY(repo.valid());
        QVERIFY2(repo.init(), "git init failed");
        QVERIFY(repo.write(QStringLiteral("a.txt"), "one\n"));
        QVERIFY2(repo.commit(QStringLiteral("first commit")), "git commit failed");
        Sandbox::setProjectDir(repo.path());

        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setGitAutoLoad(true));

        QVariantMap item =
            checkpoint(21, repo.filePath(QStringLiteral("a.txt")), true, false, false);
        item["hasDiff"] = true;
        QVERIFY(h.setCheckpoints({item}));
        QTRY_COMPARE(h.call("renderedCount").toInt(), 1);
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "openDetail",
                                          Q_ARG(QVariant, 0)));
        QTRY_VERIFY(h.call("detailVisible").toBool());

        QObject* dlg = h.call("dialogItem").value<QObject*>();
        QVERIFY(dlg);
        QTRY_VERIFY_WITH_TIMEOUT(dlg->property("gitRepo").toBool(), 10000);

        QVERIFY(QMetaObject::invokeMethod(dlg, "selectTab",
                                          Q_ARG(QVariant, 2)));
        QTRY_VERIFY_WITH_TIMEOUT(
            dlg->property("gitHistoryRenderedCount").toInt() >= 1, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(
            !dlg->property("commitDiff").toString().isEmpty(), 10000);
        QVERIFY2(dlg->property("commitDiff").toString()
                     .contains(QStringLiteral("+one")),
                 qPrintable(dlg->property("commitDiff").toString()));

        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "closeDetail"));
    }
};

QTEST_MAIN(TestFileChangesQml)
#include "test_file_changes_qml.moc"
