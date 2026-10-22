// Phase 2: right-panel Git view (read-only) against a real temporary repo.
//
// The C++ GitViewModel cases pin the async data contract; this suite pins the
// UI chain: GitPanel -> GitVM -> GitService -> ChangeTree/dialogs, including
// the staged/unstaged split, repo history, commit details and working diffs.

#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlAbstractUrlInterceptor>
#include <QQuickWindow>
#include <QQuickItem>
#include <QFile>
#include <QDir>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

#include "core/git/GitService.h"
#include "viewmodels/CommitMessageDrafter.h"

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

void collectVisualItems(QQuickItem* parent, const QString& objectName,
                        QList<QQuickItem*>* out) {
    const auto children = parent->childItems();
    for (QQuickItem* child : children) {
        if (child->objectName() == objectName)
            out->append(child);
        collectVisualItems(child, objectName, out);
    }
}

class TempRepo {
public:
    bool valid() const { return m_dir.isValid(); }
    QString path() const { return m_dir.path(); }

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
                    QStringLiteral("false")})
            && run({QStringLiteral("config"), QStringLiteral("core.safecrlf"),
                    QStringLiteral("false")});
    }

    bool write(const QString& relative, const QByteArray& content) {
        QFile file(m_dir.filePath(relative));
        if (!file.open(QIODevice::WriteOnly)) return false;
        return file.write(content) == content.size();
    }

    QString filePath(const QString& relative) const {
        return m_dir.filePath(relative);
    }

    bool run(const QStringList& args) {
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

    bool addAll() { return run({QStringLiteral("add"), QStringLiteral("-A")}); }
    bool commit(const QString& message) {
        return run({QStringLiteral("commit"), QStringLiteral("-q"),
                    QStringLiteral("-m"), message});
    }

    // stdout of a git command, for assertions on the index itself.
    QString output(const QStringList& args) {
        QProcess process;
        process.setProgram(QStringLiteral("git"));
        process.setArguments(args);
        process.setWorkingDirectory(m_dir.path());
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
        env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
        process.setProcessEnvironment(env);
        process.start();
        if (!process.waitForStarted(3000)) return {};
        if (!process.waitForFinished(15000)) {
            process.kill();
            process.waitForFinished(1000);
            return {};
        }
        return QString::fromUtf8(process.readAllStandardOutput());
    }

private:
    QTemporaryDir m_dir;
};

// Two commits; a.txt has a staged edit plus a later worktree edit, and c.txt
// is untracked. This yields one staged and two unstaged rows.
bool prepareRepo(TempRepo& repo) {
    return repo.valid() && repo.init()
        && repo.write(QStringLiteral("a.txt"), "base\n")
        && repo.write(QStringLiteral("b.txt"), "b\n")
        && repo.addAll() && repo.commit(QStringLiteral("base"))
        && repo.write(QStringLiteral("b.txt"), "b2\n")
        && repo.addAll() && repo.commit(QStringLiteral("second"))
        && repo.write(QStringLiteral("a.txt"), "staged\n")
        && repo.run({QStringLiteral("add"), QStringLiteral("a.txt")})
        && repo.write(QStringLiteral("a.txt"), "worktree\n")
        && repo.write(QStringLiteral("c.txt"), "untracked\n");
}

// P1 tests drive the real buttons: the routing from a button to a git command
// lives in QML signal handlers, so calling panel methods directly would leave
// that chain untested.
QQuickItem* firstItemNamed(QQuickWindow* window, const QString& objectName) {
    QList<QQuickItem*> items;
    collectVisualItems(window->contentItem(), objectName, &items);
    for (QQuickItem* item : items)
        if (item->isVisible()) return item;
    return nullptr;
}

bool clickItemObject(QQuickWindow* window, QQuickItem* item) {
    if (item == nullptr) return false;
    // Buttons expose a no-argument clicked(); a plain MouseArea's clicked()
    // carries the event, so those are pressed through real window coordinates.
    if (item->metaObject()->indexOfMethod("clicked()") >= 0)
        return QMetaObject::invokeMethod(item, "clicked");
    const QPointF center = item->mapToScene(
        QPointF(item->width() / 2.0, item->height() / 2.0));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center.toPoint());
    return true;
}

bool clickItem(QQuickWindow* window, const QString& objectName) {
    return clickItemObject(window, firstItemNamed(window, objectName));
}

// List delegates only appear after a render pass, so re-arm the grab until the
// row exists instead of sleeping a fixed amount.
bool clickItemWhenReady(QQuickWindow* window, const QString& objectName,
                        int attempts = 30) {
    for (int i = 0; i < attempts; ++i) {
        (void)window->grabWindow();
        QTest::qWait(100);
        if (clickItem(window, objectName)) return true;
    }
    return false;
}

// Set NEXUS_TEST_CAPTURE=1 while reviewing a UI change: each dialog state is
// written next to the test binary so a human can look at the actual pixels.
void captureIfRequested(QQuickWindow* window, const QString& tag) {
    if (window == nullptr || !qEnvironmentVariableIsSet("NEXUS_TEST_CAPTURE"))
        return;
    const QImage image = window->grabWindow();
    image.save(QStringLiteral(NEXUS_BUILD_DIR "/gitpanel_%1.png").arg(tag));
}

// Visible items with a given objectName, in tree order. Needed for the Nth row
// of a list (the comparison pick), where "first visible" is not enough.
QList<QQuickItem*> visibleItemsNamed(QQuickWindow* window,
                                     const QString& objectName) {
    QList<QQuickItem*> found;
    collectVisualItems(window->contentItem(), objectName, &found);
    QList<QQuickItem*> visible;
    for (QQuickItem* item : found)
        if (item->isVisible()) visible.append(item);
    return visible;
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
            &engine, QUrl::fromLocalFile(QStringLiteral(
                NEXUS_TEST_QML_DIR "/GitPanelHarness.qml")));
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
        if (!root || !QMetaObject::invokeMethod(root.get(), fn,
                                                Q_RETURN_ARG(QVariant, out)))
            return {};
        return out;
    }

    QVariant call(const char* fn, const QVariant& arg) const {
        QVariant out;
        if (!root || !QMetaObject::invokeMethod(root.get(), fn,
                                                Q_RETURN_ARG(QVariant, out),
                                                Q_ARG(QVariant, arg)))
            return {};
        return out;
    }

    bool setProjectRoot(const QString& path) const {
        return root && QMetaObject::invokeMethod(root.get(), "setProjectRoot",
                                                 Q_ARG(QVariant, path));
    }

    bool setSubTab(int index) const {
        return root && QMetaObject::invokeMethod(root.get(), "setSubTab",
                                                 Q_ARG(QVariant, index));
    }

    // ContextPanel's second input: whether the Git section is the visible one.
    bool setActive(bool value) const {
        return root && QMetaObject::invokeMethod(root.get(), "setActive",
                                                 Q_ARG(QVariant, value));
    }

    bool openWorkingFile(const QString& path, bool staged, bool untracked) const {
        return root && QMetaObject::invokeMethod(
            root.get(), "openWorkingFile",
            Q_ARG(QVariant, path), Q_ARG(QVariant, staged),
            Q_ARG(QVariant, untracked));
    }

    bool openCommitAt(int index) const {
        return root && QMetaObject::invokeMethod(
            root.get(), "openCommitAt", Q_ARG(QVariant, index));
    }

    bool closeWorkingDialog() const {
        return root && QMetaObject::invokeMethod(root.get(), "closeWorkingDialog");
    }

    bool closeCommitDialog() const {
        return root && QMetaObject::invokeMethod(root.get(), "closeCommitDialog");
    }

    bool setBackupRoot(const QString& path) const {
        return root && QMetaObject::invokeMethod(root.get(), "setBackupRoot",
                                                 Q_ARG(QVariant, path));
    }

    QString absolutePath(const QString& path) const {
        QVariant out;
        if (!root || !QMetaObject::invokeMethod(
                root.get(), "absolutePath", Q_RETURN_ARG(QVariant, out),
                Q_ARG(QVariant, path)))
            return {};
        return out.toString();
    }
};

} // namespace

class TestGitPanelQml : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QString error;
        if (!GitService::isGitAvailable(&error))
            QSKIP(qPrintable(QStringLiteral("git is not available: %1").arg(error)));
    }


    // A local chat has no project root, so nothing may be "identifying a git
    // repository": the panel used to sit on 「正在识别 Git 仓库…」 forever,
    // because repoKnown is only true once a refresh came back and no refresh
    // ever ran. It must stay quiet instead, and git must not be invoked.
    void localChatDoesNotIdentifyRepository() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));

        // ContextPanel's local-chat wiring: no root, section not the Git one.
        QVERIFY(h.setProjectRoot(QString()));
        QVERIFY(h.setActive(false));

        // Give any stray async refresh time to (wrongly) land.
        QTest::qWait(400);

        QVERIFY2(!h.call("repoKnownNow").toBool(),
                 "无项目根时不应得出\"不是仓库\"的结论，而应完全不识别");
        QVERIFY2(h.call("changesHintText").toString() != QStringLiteral("identifying"),
                 "本地会话不应显示「正在识别 Git 仓库…」");

        // Not merely hidden: no git call may have been made at all.
        QVERIFY2(!h.call("everRequestedGit").toBool(),
                 "无项目会话不应触发任何 git 识别");

        // The 改动 tab explains itself instead of hanging on a spinner-ish line.
        QList<QQuickItem*> hints;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("gitNoProjectHint"), &hints);
        QCOMPARE(hints.size(), 1);
        QVERIFY2(hints.first()->isVisible(),
                 "本地会话应提示\"当前会话未关联项目\"");
    }
    void showsStagedAndUnstagedTrees() {
        TempRepo repo;
        QVERIFY2(prepareRepo(repo), "failed to prepare the temporary repository");

        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setProjectRoot(repo.path()));

        QTRY_VERIFY_WITH_TIMEOUT(h.call("repoKnown").toBool(), 15000);
        QVERIFY(h.call("isRepo").toBool());
        QTRY_COMPARE_WITH_TIMEOUT(h.call("stagedCount").toInt(), 1, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(h.call("unstagedCount").toInt(), 2, 15000);
        QCOMPARE(h.call("statusEntryCount").toInt(), 2);
        QVERIFY2(!h.call("branchLabel").toString().isEmpty(), "branch label missing");

        QTest::qWait(200);
        (void)h.window->grabWindow();
        QTest::qWait(200);

        const auto visibleRowCount = [&h]() {
            QList<QQuickItem*> items;
            collectVisualItems(h.window->contentItem(),
                               QStringLiteral("fileChangeRow"), &items);
            int visible = 0;
            for (QQuickItem* item : items)
                if (item->isVisible()) ++visible;
            return visible;
        };
        QTRY_COMPARE_WITH_TIMEOUT(visibleRowCount(), 3, 5000);

        QList<QQuickItem*> rows;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("fileChangeRow"), &rows);
        QSet<QString> labels;
        for (QQuickItem* row : rows) {
            if (!row->isVisible()) continue;
            labels.insert(row->property("modelData").toMap()
                              .value("statusLabel").toString());
        }
        QVERIFY2(labels.contains(QStringLiteral("M")),
                 "modified status letter missing");
        QVERIFY2(labels.contains(QStringLiteral("?")),
                 "untracked status letter missing");
    }

    void historyAndCommitDetail() {
        TempRepo repo;
        QVERIFY2(prepareRepo(repo), "failed to prepare the temporary repository");

        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setProjectRoot(repo.path()));
        QVERIFY(h.setSubTab(1));

        QTRY_COMPARE_WITH_TIMEOUT(h.call("logCount").toInt(), 2, 15000);
        QTest::qWait(200);
        (void)h.window->grabWindow();
        QTest::qWait(200);

        const auto visibleLogRows = [&h]() {
            QList<QQuickItem*> items;
            collectVisualItems(h.window->contentItem(),
                               QStringLiteral("gitRepoLogRow"), &items);
            int visible = 0;
            for (QQuickItem* item : items)
                if (item->isVisible()) ++visible;
            return visible;
        };
        QTRY_COMPARE_WITH_TIMEOUT(visibleLogRows(), 2, 5000);

        QVERIFY(h.openCommitAt(0));
        QTRY_VERIFY(h.call("commitDialogVisible").toBool());
        QObject* dialog = h.call("commitDialogItem").value<QObject*>();
        QVERIFY(dialog);
        QTRY_COMPARE_WITH_TIMEOUT(dialog->property("fileCount").toInt(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(
            dialog->property("diffLineCount").toInt() > 0, 15000);
        QVERIFY2(dialog->property("commitDiff").toString().contains(
                     QStringLiteral("+b2")),
                 qPrintable(dialog->property("commitDiff").toString()));
        QVERIFY(h.closeCommitDialog());
    }

    // P0: stage/unstage through the panel, commit with confirmation, then
    // refresh status and history.
    void stagesUnstagesAndCommits() {
        TempRepo repo;
        QVERIFY2(prepareRepo(repo), "failed to prepare the temporary repository");

        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setProjectRoot(repo.path()));
        QTRY_COMPARE_WITH_TIMEOUT(h.call("stagedCount").toInt(), 1, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(h.call("unstagedCount").toInt(), 2, 15000);

        // Empty messages cannot open the confirmation dialog.
        QVERIFY(!h.call("canCommit").toBool());
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "openCommitConfirm"));
        QVERIFY(!h.call("commitConfirmVisible").toBool());

        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "stageFile",
                                          Q_ARG(QVariant, QStringLiteral("c.txt"))));
        QTRY_COMPARE_WITH_TIMEOUT(h.call("stagedCount").toInt(), 2, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(h.call("unstagedCount").toInt(), 1, 15000);
        QVERIFY2(h.call("operationError").toString().isEmpty(),
                 qPrintable(h.call("operationError").toString()));
        QVERIFY(!h.call("operationBusy").toBool());

        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "unstageFile",
                                          Q_ARG(QVariant, QStringLiteral("a.txt"))));
        QTRY_COMPARE_WITH_TIMEOUT(h.call("stagedCount").toInt(), 1, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(h.call("unstagedCount").toInt(), 1, 15000);
        QCOMPARE(h.call("firstStagedPath").toString(), QStringLiteral("c.txt"));

        // The composer must actually occupy space in the narrow side panel.
        QTest::qWait(200);
        (void)h.window->grabWindow();
        QList<QQuickItem*> inputs;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("gitCommitMessage"), &inputs);
        QCOMPARE(inputs.size(), 1);
        QVERIFY2(inputs.first()->isVisible(), "commit message field not rendered");
        QVERIFY2(inputs.first()->width() > 60 && inputs.first()->height() > 20,
                 "commit message field collapsed");
        QList<QQuickItem*> buttons;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("gitCommitButton"), &buttons);
        QCOMPARE(buttons.size(), 1);
        QVERIFY2(buttons.first()->isVisible(), "commit button not rendered");

        QVERIFY(QMetaObject::invokeMethod(
            h.root.get(), "setCommitMessage",
            Q_ARG(QVariant, QStringLiteral("P0 UI commit"))));
        QVERIFY(h.call("canCommit").toBool());
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "openCommitConfirm"));
        QTRY_VERIFY(h.call("commitConfirmVisible").toBool());

        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "confirmCommit"));
        QTRY_VERIFY_WITH_TIMEOUT(!h.call("operationBusy").toBool(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(
            !h.call("operationNotice").toString().isEmpty(), 15000);
        QVERIFY2(h.call("operationError").toString().isEmpty(),
                 qPrintable(h.call("operationError").toString()));
        QTRY_COMPARE_WITH_TIMEOUT(h.call("stagedCount").toInt(), 0, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(h.call("unstagedCount").toInt(), 1, 15000);
        QTRY_VERIFY(h.call("commitMessage").toString().isEmpty());
        QVERIFY(!h.call("commitConfirmVisible").toBool());

        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "setSubTab",
                                          Q_ARG(QVariant, 1)));
        QTRY_COMPARE_WITH_TIMEOUT(h.call("logCount").toInt(), 3, 15000);
        QCOMPARE(h.call("topLogSubject").toString(), QStringLiteral("P0 UI commit"));
    }

    void workingDiffAndUntrackedPreview() {
        TempRepo repo;
        QVERIFY2(prepareRepo(repo), "failed to prepare the temporary repository");

        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setProjectRoot(repo.path()));
        QTRY_COMPARE_WITH_TIMEOUT(h.call("stagedCount").toInt(), 1, 15000);

        QVERIFY(h.openWorkingFile(QStringLiteral("a.txt"), true, false));
        QTRY_VERIFY(h.call("workingDialogVisible").toBool());
        QObject* dialog = h.call("workingDialogItem").value<QObject*>();
        QVERIFY(dialog);
        QTRY_VERIFY_WITH_TIMEOUT(
            dialog->property("diffText").toString().contains(
                QStringLiteral("+staged")), 15000);
        QVERIFY(dialog->property("diffLineCount").toInt() > 0);
        QVERIFY2(dialog->property("errorText").toString().isEmpty(),
                 qPrintable(dialog->property("errorText").toString()));

        QVERIFY(h.closeWorkingDialog());
        QTRY_VERIFY(!h.call("workingDialogVisible").toBool());

        QVERIFY(h.openWorkingFile(QStringLiteral("c.txt"), false, true));
        QTRY_VERIFY(h.call("workingDialogVisible").toBool());
        QCOMPARE(dialog->property("untracked").toBool(), true);
        QTRY_VERIFY_WITH_TIMEOUT(
            dialog->property("diffText").toString().contains(
                QStringLiteral("+untracked")), 15000);
        QVERIFY(dialog->property("diffLineCount").toInt() > 0);
        QVERIFY2(dialog->property("errorText").toString().isEmpty(),
                 qPrintable(dialog->property("errorText").toString()));
        QVERIFY(!dialog->property("loading").toBool());
    }

    void workingDiffFileActionsAndHistory() {
        TempRepo repo;
        QVERIFY2(prepareRepo(repo), "failed to prepare the temporary repository");

        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setProjectRoot(repo.path()));
        QTRY_COMPARE_WITH_TIMEOUT(h.call("stagedCount").toInt(), 1, 15000);

        QVERIFY(h.openWorkingFile(QStringLiteral("a.txt"), false, false));
        QTRY_VERIFY(h.call("workingDialogVisible").toBool());
        QTest::qWait(200);
        (void)h.window->grabWindow();
        QTest::qWait(200);

        // Copy path resolves against the active project root.
        const QString expected = QDir::toNativeSeparators(
            QFileInfo(repo.filePath(QStringLiteral("a.txt"))).absoluteFilePath());
        QCOMPARE(h.absolutePath(QStringLiteral("a.txt")), expected);
        // An escaping path yields nothing rather than a guess outside the repo.
        QVERIFY(h.absolutePath(QStringLiteral("../outside.txt")).isEmpty());
        QVERIFY(clickItem(h.window, QStringLiteral("workingCopyButton")));
        QTRY_VERIFY_WITH_TIMEOUT(
            h.call("workingDialogItem").value<QObject*>()
                ->property("actionNotice").toString().contains(
                    QStringLiteral("已复制")),
            5000);

        // History button opens the per-file log, whose row opens commit detail.
        QVERIFY(clickItem(h.window, QStringLiteral("workingHistoryButton")));
        QTRY_VERIFY(h.call("fileHistoryVisible").toBool());
        QTRY_COMPARE_WITH_TIMEOUT(h.call("fileHistoryCount").toInt(), 1, 15000);
        QCOMPARE(h.call("fileHistoryError").toString(), QString());
        (void)h.window->grabWindow();
        QTest::qWait(150);
        captureIfRequested(h.window, QStringLiteral("p1_history"));

        // Rows carry no button of their own: the click target is the row's
        // MouseArea, which is what a user actually hits.
        QVERIFY2(clickItemWhenReady(h.window, QStringLiteral("gitFileHistoryRowArea")),
                 "the history row must be clickable");
        QTRY_VERIFY(!h.call("fileHistoryVisible").toBool());
        QTRY_VERIFY(h.call("commitDialogVisible").toBool());
    }

    void discardUntrackedThroughConfirmation() {
        TempRepo repo;
        QVERIFY2(prepareRepo(repo), "failed to prepare the temporary repository");

        QTemporaryDir backup;
        QVERIFY(backup.isValid());

        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setBackupRoot(backup.path()));
        QVERIFY(h.setProjectRoot(repo.path()));
        QTRY_COMPARE_WITH_TIMEOUT(h.call("unstagedCount").toInt(), 2, 15000);

        QVERIFY(h.openWorkingFile(QStringLiteral("c.txt"), false, true));
        QTRY_VERIFY(h.call("workingDialogVisible").toBool());
        QTest::qWait(200);
        (void)h.window->grabWindow();
        QTest::qWait(200);

        // Nothing is deleted before the user confirms.
        QVERIFY(clickItem(h.window, QStringLiteral("workingDiscardButton")));
        QTRY_VERIFY(h.call("actionDialogVisible").toBool());
        (void)h.window->grabWindow();
        QTest::qWait(150);
        captureIfRequested(h.window, QStringLiteral("p1_discard"));
        QVERIFY2(h.call("actionDialogTitle").toString().contains(
                     QStringLiteral("未跟踪")),
                 qPrintable(h.call("actionDialogTitle").toString()));
        QVERIFY(h.call("actionDialogDetails").toInt() >= 3);
        QVERIFY(QFile::exists(repo.filePath(QStringLiteral("c.txt"))));

        QVERIFY(clickItem(h.window, QStringLiteral("gitActionConfirmButton")));
        QTRY_VERIFY(!h.call("actionDialogVisible").toBool());
        // The command runs on a worker thread; wait for the gate to open again.
        QTRY_VERIFY_WITH_TIMEOUT(!h.call("operationBusy").toBool(), 15000);
        QVERIFY2(!QFile::exists(repo.filePath(QStringLiteral("c.txt"))),
                 "the confirmed untracked delete must remove the file");
        QVERIFY2(h.call("statusError").toString().isEmpty(),
                 qPrintable(h.call("statusError").toString()));
        QTRY_COMPARE_WITH_TIMEOUT(h.call("unstagedCount").toInt(), 1, 15000);
        QVERIFY2(!h.call("lastBackupPath").toString().isEmpty(),
                 "the panel must report where the file was backed up");
        QVERIFY2(h.call("lastBackupPath").toString().startsWith(backup.path()),
                 qPrintable(h.call("lastBackupPath").toString()));
        QFile copied(h.call("lastBackupPath").toString());
        QVERIFY(copied.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(copied.readAll()),
                 QStringLiteral("untracked\n"));
        QVERIFY2(h.call("operationNotice").toString().contains(
                     QStringLiteral("备份")),
                 qPrintable(h.call("operationNotice").toString()));
    }

    void stashPushAndPopFromPanel() {
        TempRepo repo;
        QVERIFY2(prepareRepo(repo), "failed to prepare the temporary repository");

        QTemporaryDir backup;
        QVERIFY(backup.isValid());

        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setBackupRoot(backup.path()));
        QVERIFY(h.setProjectRoot(repo.path()));
        QTRY_COMPARE_WITH_TIMEOUT(h.call("unstagedCount").toInt(), 2, 15000);

        QTest::qWait(200);
        (void)h.window->grabWindow();
        QTest::qWait(200);
        captureIfRequested(h.window, QStringLiteral("p1_row"));

        QVERIFY(clickItem(h.window, QStringLiteral("gitStashButton")));
        QTRY_VERIFY(h.call("actionDialogVisible").toBool());
        QVERIFY(clickItem(h.window, QStringLiteral("gitActionConfirmButton")));
        // The command is async: an outcome (notice or error) only appears once
        // the worker reports back.
        QTRY_VERIFY_WITH_TIMEOUT(
            !h.call("operationNotice").toString().isEmpty()
                || !h.call("operationError").toString().isEmpty(), 15000);
        QVERIFY2(h.call("operationError").toString().isEmpty(),
                 qPrintable(h.call("operationError").toString()));
        // Only tracked changes are stashed: a.txt's worktree edit goes, c.txt
        // stays untracked.
        QTRY_COMPARE_WITH_TIMEOUT(h.call("unstagedCount").toInt(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!h.call("stashLoading").toBool(), 15000);
        QCOMPARE(h.call("stashError").toString(), QString());
        QTRY_COMPARE_WITH_TIMEOUT(h.call("stashCount").toInt(), 1, 15000);
        (void)h.window->grabWindow();
        QTest::qWait(150);
        captureIfRequested(h.window, QStringLiteral("p1_stash"));

        QVERIFY2(clickItemWhenReady(h.window, QStringLiteral("gitStashPopButton")),
                 "the stash row must render a clickable pop button");
        QTRY_VERIFY(h.call("actionDialogVisible").toBool());
        QVERIFY2(h.call("actionDialogTitle").toString().contains(
                     QStringLiteral("取出")),
                 qPrintable(h.call("actionDialogTitle").toString()));
        QVERIFY(clickItem(h.window, QStringLiteral("gitActionConfirmButton")));

        QTRY_COMPARE_WITH_TIMEOUT(h.call("unstagedCount").toInt(), 2, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(h.call("stashCount").toInt(), 0, 15000);
        QCOMPARE(h.call("operationError").toString(), QString());
    }

    void undoAndAmendLastCommitFromPanel() {
        TempRepo repo;
        QVERIFY2(prepareRepo(repo), "failed to prepare the temporary repository");

        QTemporaryDir backup;
        QVERIFY(backup.isValid());

        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setBackupRoot(backup.path()));
        QVERIFY(h.setProjectRoot(repo.path()));
        QTRY_COMPARE_WITH_TIMEOUT(h.call("stagedCount").toInt(), 1, 15000);

        QTest::qWait(200);
        (void)h.window->grabWindow();
        QTest::qWait(200);

        // Undo needs HEAD's subject, which the panel fetches on demand: the
        // dialog opens only once the log page arrives.
        QVERIFY(clickItem(h.window, QStringLiteral("gitUndoButton")));
        QTRY_VERIFY(h.call("actionDialogVisible").toBool());
        QVERIFY2(h.call("actionDialogTitle").toString().contains(
                     QStringLiteral("撤销")),
                 qPrintable(h.call("actionDialogTitle").toString()));
        QVERIFY(clickItem(h.window, QStringLiteral("gitActionConfirmButton")));
        QTRY_VERIFY_WITH_TIMEOUT(!h.call("operationBusy").toBool(), 15000);
        QVERIFY2(h.call("operationError").toString().isEmpty(),
                 qPrintable(h.call("operationError").toString()));
        // "second" only touched b.txt, which now comes back staged.
        QTRY_COMPARE_WITH_TIMEOUT(h.call("stagedCount").toInt(), 2, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(h.call("logCount").toInt() >= 1, 15000);

        // Amend rewrites the (now) last commit message.
        QTest::qWait(300);
        (void)h.window->grabWindow();
        QTest::qWait(200);
        QVERIFY(clickItem(h.window, QStringLiteral("gitAmendButton")));
        QTRY_VERIFY(h.call("actionDialogVisible").toBool());
        QQuickItem* message = firstItemNamed(
            h.window, QStringLiteral("gitActionMessage"));
        QVERIFY2(message != nullptr, "amend must offer a message field");
        QVERIFY2(message->property("text").toString()
                     .contains(QStringLiteral("base")),
                 qPrintable(message->property("text").toString()));
        message->setProperty("text", QStringLiteral("amended by panel test"));
        QTRY_VERIFY(h.call("actionDialogCanConfirm").toBool());
        (void)h.window->grabWindow();
        QTest::qWait(150);
        captureIfRequested(h.window, QStringLiteral("p1_amend"));
        QVERIFY(clickItem(h.window, QStringLiteral("gitActionConfirmButton")));
        QTRY_VERIFY_WITH_TIMEOUT(
            h.call("topLogSubject").toString()
                == QStringLiteral("amended by panel test"), 15000);
        QCOMPARE(h.call("operationError").toString(), QString());
    }

    void stagesIndividualHunksFromDialog() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QByteArray base;
        for (int i = 1; i <= 40; ++i)
            base += QStringLiteral("line %1\n").arg(i).toUtf8();
        QVERIFY(repo.write(QStringLiteral("a.txt"), base));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("base")));
        QByteArray work = base;
        work.replace(QByteArrayLiteral("line 2\n"),
                     QByteArrayLiteral("line 2 edited\n"));
        work.replace(QByteArrayLiteral("line 30\n"),
                     QByteArrayLiteral("line 30 edited\n"));
        QVERIFY(repo.write(QStringLiteral("a.txt"), work));

        QTemporaryDir backup;
        QVERIFY(backup.isValid());

        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setBackupRoot(backup.path()));
        QVERIFY(h.setProjectRoot(repo.path()));
        QTRY_COMPARE_WITH_TIMEOUT(h.call("unstagedCount").toInt(), 1, 15000);
        QCOMPARE(h.call("stagedCount").toInt(), 0);

        QVERIFY(h.openWorkingFile(QStringLiteral("a.txt"), false, false));
        QTRY_VERIFY(h.call("workingDialogVisible").toBool());
        QObject* dialog = h.call("workingDialogItem").value<QObject*>();
        QVERIFY(dialog);
        QTRY_VERIFY_WITH_TIMEOUT(dialog->property("hunkCount").toInt() == 2, 15000);
        QCOMPARE(dialog->property("hunkError").toString(), QString());
        QVERIFY2(dialog->property("canPatchHunks").toBool(),
                 "two hunks must be offered for partial staging");

        QTest::qWait(200);
        (void)h.window->grabWindow();
        QTest::qWait(200);
        // The hunk section must actually occupy space: a zero-height Flickable
        // would still report the model, so check the rows themselves.
        QList<QQuickItem*> hunkRows;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("gitHunkRow"), &hunkRows);
        QCOMPARE(hunkRows.size(), 2);
        for (QQuickItem* row : hunkRows) {
            QVERIFY2(row->isVisible(), "hunk row not rendered");
            QVERIFY2(row->width() > 200.0,
                     qPrintable(QStringLiteral("hunk row collapsed: %1 wide")
                                    .arg(row->width())));
            QVERIFY2(row->height() >= 30.0,
                     qPrintable(QStringLiteral("hunk row collapsed: %1 tall")
                                    .arg(row->height())));
        }
        captureIfRequested(h.window, QStringLiteral("p2_hunks"));

        // Stage only the first hunk.
        QVERIFY2(clickItemWhenReady(h.window, QStringLiteral("gitHunkStageButton")),
                 "the hunk row must offer a stage button");
        QTRY_VERIFY_WITH_TIMEOUT(!h.call("operationBusy").toBool(), 15000);
        QVERIFY2(h.call("operationError").toString().isEmpty(),
                 qPrintable(h.call("operationError").toString()));
        QTRY_COMPARE_WITH_TIMEOUT(h.call("stagedCount").toInt(), 1, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(h.call("unstagedCount").toInt(), 1, 15000);
        // The dialog reloaded itself: one hunk left in the worktree view.
        QTRY_COMPARE_WITH_TIMEOUT(dialog->property("hunkCount").toInt(), 1, 15000);

        const QString stagedDiff = repo.output(
            {QStringLiteral("diff"), QStringLiteral("--cached")});
        QVERIFY2(stagedDiff.contains(QStringLiteral("line 2 edited")),
                 qPrintable(stagedDiff));
        QVERIFY2(!stagedDiff.contains(QStringLiteral("line 30 edited")),
                 "the second hunk must stay unstaged");

        // Switch to the staged view and take that hunk back out.
        QVERIFY(h.closeWorkingDialog());
        QVERIFY(h.openWorkingFile(QStringLiteral("a.txt"), true, false));
        QTRY_VERIFY_WITH_TIMEOUT(dialog->property("hunkCount").toInt() == 1, 15000);
        QTest::qWait(200);
        (void)h.window->grabWindow();
        QTest::qWait(200);
        QVERIFY2(clickItemWhenReady(h.window, QStringLiteral("gitHunkStageButton")),
                 "a staged hunk must offer an unstage button");
        QTRY_VERIFY_WITH_TIMEOUT(!h.call("operationBusy").toBool(), 15000);
        QTRY_COMPARE_WITH_TIMEOUT(h.call("stagedCount").toInt(), 0, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(h.call("unstagedCount").toInt(), 1, 15000);
        QCOMPARE(h.call("operationError").toString(), QString());
    }

    void branchViewShowsTrackingAndStaysReadOnly() {
        TempRepo origin;
        QVERIFY(origin.valid() && origin.init());
        QVERIFY(origin.write(QStringLiteral("a.txt"), QByteArrayLiteral("base\n")));
        QVERIFY(origin.addAll() && origin.commit(QStringLiteral("base")));

        TempRepo clone;
        QVERIFY(clone.valid());
        QVERIFY(clone.run({QStringLiteral("clone"), QStringLiteral("-q"),
                           origin.path(), QStringLiteral(".")}));
        QVERIFY(clone.run({QStringLiteral("config"), QStringLiteral("user.name"),
                           QStringLiteral("Test User")}));
        QVERIFY(clone.run({QStringLiteral("config"), QStringLiteral("user.email"),
                           QStringLiteral("test@example.com")}));
        const QString branch = clone.output({QStringLiteral("rev-parse"),
                                            QStringLiteral("--abbrev-ref"),
                                            QStringLiteral("HEAD")}).trimmed();
        QVERIFY(!branch.isEmpty());
        QVERIFY(clone.write(QStringLiteral("b.txt"), QByteArrayLiteral("local\n")));
        QVERIFY(clone.addAll() && clone.commit(QStringLiteral("local only")));
        QVERIFY(origin.write(QStringLiteral("c.txt"), QByteArrayLiteral("remote\n")));
        QVERIFY(origin.addAll() && origin.commit(QStringLiteral("remote only")));
        QVERIFY(clone.run({QStringLiteral("fetch"), QStringLiteral("--quiet"),
                           QStringLiteral("origin")}));
        QVERIFY(clone.run({QStringLiteral("branch"), QStringLiteral("side")}));
        QVERIFY(clone.run({QStringLiteral("tag"), QStringLiteral("v9-test")}));

        QTemporaryDir backup;
        QVERIFY(backup.isValid());

        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setBackupRoot(backup.path()));
        QVERIFY(h.setProjectRoot(clone.path()));
        QTRY_VERIFY_WITH_TIMEOUT(h.call("isRepo").toBool(), 15000);

        // The badge comes from the refresh that activated the panel.
        QTRY_VERIFY_WITH_TIMEOUT(
            h.call("trackingBadge").toString().contains(QStringLiteral("↑"))
                && h.call("trackingBadge").toString().contains(QStringLiteral("↓")),
            15000);

        QTest::qWait(200);
        (void)h.window->grabWindow();
        QTest::qWait(200);

        QVERIFY(clickItem(h.window, QStringLiteral("gitBranchButton")));
        QTRY_VERIFY(h.call("branchDialogVisible").toBool());
        QTRY_COMPARE_WITH_TIMEOUT(h.call("branchRowCount").toInt(), 2, 15000);
        QObject* dialog = h.call("branchDialogItem").value<QObject*>();
        QVERIFY(dialog);
        QVERIFY2(h.call("branchDialogError").toString().isEmpty(),
                 qPrintable(h.call("branchDialogError").toString()));
        QVERIFY2(h.call("branchNames").toString().contains(branch)
                     && h.call("branchNames").toString().contains(
                         QStringLiteral("side")),
                 qPrintable(h.call("branchNames").toString()));

        // Rows must actually render, and must not offer any write action. The
        // Repeater needs a render pass per dialog opening, so re-arm the grab.
        const auto branchRows = [&h]() {
            (void)h.window->grabWindow();
            QTest::qWait(60);
            QList<QQuickItem*> items;
            collectVisualItems(h.window->contentItem(),
                               QStringLiteral("gitBranchRow"), &items);
            return items;
        };
        QList<QQuickItem*> rows;
        QTRY_COMPARE_WITH_TIMEOUT(branchRows().size(), 2, 5000);
        rows = branchRows();
        for (QQuickItem* row : rows) {
            QVERIFY2(row->isVisible(), "branch row not rendered");
            QVERIFY2(row->width() > 150.0,
                     qPrintable(QStringLiteral("branch row collapsed: %1 wide")
                                    .arg(row->width())));
        }
        QList<QQuickItem*> actions;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("gitActionConfirmButton"), &actions);
        QVERIFY2(actions.isEmpty(), "the branch view must not offer a write action");

        // Tags are listed in the same read-only view.
        const auto tagRows = [&h]() {
            (void)h.window->grabWindow();
            QTest::qWait(60);
            return visibleItemsNamed(h.window, QStringLiteral("gitTagRow"));
        };
        QTRY_COMPARE_WITH_TIMEOUT(tagRows().size(), 1, 5000);
        QCOMPARE(dialog->property("tagCount").toInt(), 1);
        captureIfRequested(h.window, QStringLiteral("p2b_branch"));
    }

    void draftOnlyFillsTheComposerAndNeverCommits() {
        TempRepo repo;
        QVERIFY2(prepareRepo(repo), "failed to prepare the temporary repository");

        QTemporaryDir backup;
        QVERIFY(backup.isValid());

        // A protocol the registry does not know fails before any request leaves
        // the process, so the panel chain is covered without a live model, a
        // proxy or a timeout race. The seam is class-level because the QML
        // engine holds its own singleton instance: an instance member would not
        // reach the object the panel calls, and the test would then send a real
        // request to whatever the user configured.
        CommitMessageDrafter::setContextProvider([]() {
            DraftContext context;
            context.profile["apiProtocol"] = QStringLiteral("no-such-protocol");
            context.profile["baseUrl"] = QStringLiteral("http://127.0.0.1:9/v1");
            context.profile["apiKey"] = QStringLiteral("test-key");
            context.profile["model"] = QStringLiteral("test-model");
            context.localOnlyMode = false;
            return context;
        });

        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setBackupRoot(backup.path()));
        QVERIFY(h.setProjectRoot(repo.path()));
        QTRY_COMPARE_WITH_TIMEOUT(h.call("stagedCount").toInt(), 1, 15000);

        QTest::qWait(200);
        (void)h.window->grabWindow();
        QTest::qWait(200);

        QVERIFY(h.call("canDraft").toBool());
        QVERIFY(clickItem(h.window, QStringLiteral("gitDraftButton")));
        QVERIFY2(h.call("draftPending").toBool(), "click must start a draft");
        QVERIFY2(!h.call("canDraft").toBool(),
                 "a second draft must be blocked while one is pending");

        QTRY_VERIFY_WITH_TIMEOUT(!h.call("draftPending").toBool(), 15000);
        QVERIFY2(!h.call("operationNotice").toString().isEmpty(),
                 "the failed draft must report why");
        // Nothing was written and nothing was committed.
        QCOMPARE(h.call("stagedCount").toInt(), 1);
        QCOMPARE(h.call("unstagedCount").toInt(), 2);
        QCOMPARE(h.call("commitMessage").toString(), QString());
        QVERIFY(!h.call("commitConfirmVisible").toBool());

        // A successful draft lands in the composer, still editable, still
        // behind the user's own 提交 click.
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "setDraftCommitMessage",
                                          Q_ARG(QVariant, QString())));
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "requestDraft"));
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "applyDraft",
                                          Q_ARG(QVariant, true),
                                          Q_ARG(QVariant, QStringLiteral("feat(git): 起草的说明")),
                                          Q_ARG(QVariant, QString())));
        QCOMPARE(h.call("commitMessage").toString(),
                 QStringLiteral("feat(git): 起草的说明"));
        QVERIFY2(h.call("operationNotice").toString().contains(
                     QStringLiteral("可继续编辑")),
                 qPrintable(h.call("operationNotice").toString()));
        QCOMPARE(h.call("stagedCount").toInt(), 1);
        QVERIFY(!h.call("commitConfirmVisible").toBool());

        // Text typed while the request was running is never overwritten.
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "setDraftCommitMessage",
                                          Q_ARG(QVariant, QStringLiteral("我自己写的"))));
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "requestDraft"));
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "setDraftCommitMessage",
                                          Q_ARG(QVariant, QStringLiteral("我改过了"))));
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "applyDraft",
                                          Q_ARG(QVariant, true),
                                          Q_ARG(QVariant, QStringLiteral("生成值")),
                                          Q_ARG(QVariant, QString())));
        QCOMPARE(h.call("commitMessage").toString(), QStringLiteral("我改过了"));
        QVERIFY2(h.call("operationNotice").toString().contains(
                     QStringLiteral("未覆盖")),
                 qPrintable(h.call("operationNotice").toString()));
        QCOMPARE(h.call("stagedCount").toInt(), 1);

        CommitMessageDrafter::setContextProvider(nullptr);
    }

    void filtersHistoryThroughThePanel() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QVERIFY(repo.write(QStringLiteral("a.txt"), QByteArrayLiteral("one\n")));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("feat: add A")));
        QVERIFY(repo.run({QStringLiteral("config"), QStringLiteral("user.name"),
                          QStringLiteral("Bob Lee")}));
        QVERIFY(repo.write(QStringLiteral("b.txt"), QByteArrayLiteral("two\n")));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("fix: repair B")));
        QVERIFY(repo.run({QStringLiteral("config"), QStringLiteral("user.name"),
                          QStringLiteral("Alice Zhang")}));
        QVERIFY(repo.write(QStringLiteral("a.txt"), QByteArrayLiteral("one\ntwo\n")));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("docs: update A")));

        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setProjectRoot(repo.path()));
        QTRY_VERIFY_WITH_TIMEOUT(h.call("isRepo").toBool(), 15000);
        QVERIFY(h.setSubTab(1));
        QTRY_COMPARE_WITH_TIMEOUT(h.call("logCount").toInt(), 3, 15000);

        QTest::qWait(200);
        (void)h.window->grabWindow();
        QTest::qWait(200);
        QQuickItem* grepField = firstItemNamed(h.window,
                                              QStringLiteral("gitLogGrepInput"));
        QVERIFY2(grepField != nullptr, "the history tab must offer a search field");

        grepField->setProperty("text", QStringLiteral("docs"));
        QVERIFY(clickItemWhenReady(h.window, QStringLiteral("gitLogFilterButton")));
        QTRY_COMPARE_WITH_TIMEOUT(h.call("logCount").toInt(), 1, 15000);
        QCOMPARE(h.call("topLogSubject").toString(), QStringLiteral("docs: update A"));
        QCOMPARE(h.call("logGrepValue").toString(), QStringLiteral("docs"));
        QVERIFY2(h.call("filterActive").toBool(), "an active filter must show as active");

        // Author filter narrows further, and both kinds combine.
        QQuickItem* authorField = firstItemNamed(h.window,
                                                 QStringLiteral("gitLogAuthorInput"));
        QVERIFY(authorField != nullptr);
        authorField->setProperty("text", QStringLiteral("bob"));
        QVERIFY(clickItemWhenReady(h.window, QStringLiteral("gitLogFilterButton")));
        QTRY_VERIFY_WITH_TIMEOUT(h.call("logCount").toInt() == 0, 15000);
        QVERIFY2(h.call("operationNotice").toString().isEmpty(),
                 "a filtered history page is not an operation");

        // Clearing restores the full page and leaves no filter behind.
        authorField->setProperty("text", QString());
        QVERIFY(clickItemWhenReady(h.window, QStringLiteral("gitLogClearButton")));
        QTRY_COMPARE_WITH_TIMEOUT(h.call("logCount").toInt(), 3, 15000);
        QCOMPARE(h.call("logGrepValue").toString(), QString());
        QCOMPARE(h.call("logAuthorValue").toString(), QString());
        QVERIFY2(!h.call("filterActive").toBool(), "clearing must drop the filter");
        captureIfRequested(h.window, QStringLiteral("p3b_filter"));
    }

    void comparesTwoPickedCommits() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QVERIFY(repo.write(QStringLiteral("a.txt"), QByteArrayLiteral("one\n")));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("feat: one")));
        QVERIFY(repo.write(QStringLiteral("b.txt"), QByteArrayLiteral("two\n")));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("feat: two")));
        QVERIFY(repo.write(QStringLiteral("c.txt"), QByteArrayLiteral("three\n")));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("feat: three")));

        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setProjectRoot(repo.path()));
        QTRY_VERIFY_WITH_TIMEOUT(h.call("isRepo").toBool(), 15000);
        QVERIFY(h.setSubTab(1));
        QTRY_COMPARE_WITH_TIMEOUT(h.call("logCount").toInt(), 3, 15000);
        const QString headSha = h.call("logShaAt", 0).toString();
        const QString firstSha = h.call("logShaAt", 2).toString();
        QVERIFY(!headSha.isEmpty() && !firstSha.isEmpty());

        const auto pickButtons = [&h]() {
            (void)h.window->grabWindow();
            QTest::qWait(60);
            return visibleItemsNamed(h.window,
                                     QStringLiteral("gitComparePickButton"));
        };
        QList<QQuickItem*> picks;
        QTRY_COMPARE_WITH_TIMEOUT(pickButtons().size(), 3, 5000);
        picks = pickButtons();

        // First click only sets the base; nothing is fetched yet.
        QVERIFY(QMetaObject::invokeMethod(picks.at(0), "clicked"));
        QTRY_VERIFY(h.call("hasCompareBase").toBool());
        QCOMPARE(h.call("compareBaseSha").toString(), headSha);
        QVERIFY(!h.call("compareVisible").toBool());

        // Second click on a different commit opens A→B.
        picks = pickButtons();
        QVERIFY(QMetaObject::invokeMethod(picks.at(2), "clicked"));
        QTRY_VERIFY(h.call("compareVisible").toBool());
        QVERIFY2(h.call("compareError").toString().isEmpty(),
                 qPrintable(h.call("compareError").toString()));
        // Between the first and the last commit, b.txt and c.txt were added.
        QTRY_COMPARE_WITH_TIMEOUT(h.call("compareFileCount").toInt(), 2, 15000);
        QCOMPARE(h.call("compareItem").value<QObject*>()->property("shaA").toString(),
                 headSha);
        QCOMPARE(h.call("compareItem").value<QObject*>()->property("shaB").toString(),
                 firstSha);
        QVERIFY2(!h.call("compareSelectedPath").toString().isEmpty(),
                 "the first difference must be selected automatically");
        QTRY_VERIFY_WITH_TIMEOUT(h.call("compareDiffLines").toInt() > 0, 15000);

        // The diff pane must have real width: an uncapped tree column would
        // stretch and leave it a few pixels (measured before).
        QTest::qWait(200);
        (void)h.window->grabWindow();
        QTest::qWait(200);
        QList<QQuickItem*> views;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("compareDiffView"), &views);
        QCOMPARE(views.size(), 1);
        QVERIFY2(views.first()->width() > 200.0,
                 qPrintable(QStringLiteral("diff pane collapsed: %1")
                                .arg(views.first()->width())));

        // Swapping reverses the direction and still lists both files.
        QVERIFY(clickItemWhenReady(h.window, QStringLiteral("compareSwapButton")));
        QObject* dialog = h.call("compareItem").value<QObject*>();
        QTRY_VERIFY_WITH_TIMEOUT(
            dialog->property("shaA").toString() == firstSha
                && dialog->property("shaB").toString() == headSha, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(h.call("compareFileCount").toInt(), 2, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(h.call("compareDiffLines").toInt() > 0, 15000);
        captureIfRequested(h.window, QStringLiteral("p3c_compare"));

        // Closing and clicking the base row again cancels the selection.
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QTRY_VERIFY(!h.call("compareVisible").toBool());
        picks = pickButtons();
        QVERIFY(QMetaObject::invokeMethod(picks.at(0), "clicked"));
        QTRY_VERIFY(!h.call("hasCompareBase").toBool());
    }

    void blamesFileLinesFromTheWorkingDiff() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QVERIFY(repo.run({QStringLiteral("config"), QStringLiteral("user.name"),
                          QStringLiteral("Ann A")}));
        QVERIFY(repo.write(QStringLiteral("a.txt"), QByteArrayLiteral("one\n")));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("first")));
        QVERIFY(repo.run({QStringLiteral("config"), QStringLiteral("user.name"),
                          QStringLiteral("Bob B")}));
        QVERIFY(repo.write(QStringLiteral("a.txt"), QByteArrayLiteral("one\ntwo\n")));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("second")));
        // One committed line, one worktree-only line, so both states show up.
        QVERIFY(repo.write(QStringLiteral("a.txt"), QByteArrayLiteral("one\ntwo edited\n")));

        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setProjectRoot(repo.path()));
        QTRY_VERIFY_WITH_TIMEOUT(h.call("isRepo").toBool(), 15000);

        QVERIFY(h.openWorkingFile(QStringLiteral("a.txt"), false, false));
        QTRY_VERIFY(h.call("workingDialogVisible").toBool());
        QTest::qWait(200);
        (void)h.window->grabWindow();
        QTest::qWait(200);

        QVERIFY(clickItemWhenReady(h.window, QStringLiteral("workingBlameButton")));
        QTRY_VERIFY(h.call("blameVisible").toBool());
        QTRY_COMPARE_WITH_TIMEOUT(h.call("blameLineCount").toInt(), 2, 15000);
        QVERIFY2(h.call("blameError").toString().isEmpty(),
                 qPrintable(h.call("blameError").toString()));
        QObject* dialog = h.call("blameItem").value<QObject*>();
        QVERIFY(dialog);
        QCOMPARE(dialog->property("path").toString(), QStringLiteral("a.txt"));

        // Rows must render (a collapsed list would still report the model).
        const auto blameRows = [&h]() {
            (void)h.window->grabWindow();
            QTest::qWait(60);
            return visibleItemsNamed(h.window, QStringLiteral("gitBlameRow"));
        };
        QList<QQuickItem*> rows;
        QTRY_COMPARE_WITH_TIMEOUT(blameRows().size(), 2, 5000);
        rows = blameRows();
        for (QQuickItem* row : rows) {
            QVERIFY2(row->isVisible(), "blame row not rendered");
            QVERIFY2(row->width() > 300.0,
                     qPrintable(QStringLiteral("blame row collapsed: %1 wide")
                                    .arg(row->width())));
        }
        captureIfRequested(h.window, QStringLiteral("p3d_blame"));

        // Clicking the committed line opens that commit's details.
        QList<QQuickItem*> areas;
        collectVisualItems(rows.at(0), QStringLiteral("blameRowArea"), &areas);
        QVERIFY2(areas.size() == 1, "each blame row needs its own click area");
        QVERIFY(clickItemObject(h.window, areas.first()));
        QTRY_VERIFY(h.call("commitDialogVisible").toBool());
        QTRY_VERIFY(!h.call("blameVisible").toBool());
        QVERIFY2(h.call("operationNotice").toString().isEmpty(),
                 "opening a commit is not a repository write");
    }

    void opensTheCommitGraphAndNarrowsToCurrentBranch() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QVERIFY(repo.write(QStringLiteral("a.txt"), QByteArrayLiteral("one\n")));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("first")));
        QVERIFY(repo.run({QStringLiteral("checkout"), QStringLiteral("-q"),
                          QStringLiteral("-b"), QStringLiteral("side")}));
        QVERIFY(repo.write(QStringLiteral("s.txt"), QByteArrayLiteral("side\n")));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("side work")));
        QVERIFY(repo.run({QStringLiteral("checkout"), QStringLiteral("-q"),
                          QStringLiteral("-")}));
        QVERIFY(repo.write(QStringLiteral("m.txt"), QByteArrayLiteral("main\n")));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("main work")));
        QVERIFY(repo.run({QStringLiteral("merge"), QStringLiteral("--no-ff"),
                          QStringLiteral("-m"), QStringLiteral("merge side"),
                          QStringLiteral("side")}));
        QVERIFY(repo.run({QStringLiteral("checkout"), QStringLiteral("-q"),
                          QStringLiteral("-b"), QStringLiteral("orphan")}));
        QVERIFY(repo.write(QStringLiteral("o.txt"), QByteArrayLiteral("orphan\n")));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("orphan work")));
        QVERIFY(repo.run({QStringLiteral("checkout"), QStringLiteral("-q"),
                          QStringLiteral("-")}));

        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setProjectRoot(repo.path()));
        QTRY_VERIFY_WITH_TIMEOUT(h.call("isRepo").toBool(), 15000);
        QVERIFY(h.setSubTab(1));
        QTRY_COMPARE_WITH_TIMEOUT(h.call("logCount").toInt(), 4, 15000);

        QTest::qWait(200);
        (void)h.window->grabWindow();
        QTest::qWait(200);
        QVERIFY(clickItemWhenReady(h.window, QStringLiteral("gitGraphButton")));
        QTRY_VERIFY(h.call("graphVisible").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(h.call("graphCommitCount").toInt() >= 4, 15000);
        QVERIFY2(h.call("graphError").toString().isEmpty(),
                 qPrintable(h.call("graphError").toString()));
        // The unmerged branch is in scope with --all: 5 commits total.
        QTRY_COMPARE_WITH_TIMEOUT(h.call("graphCommitCount").toInt(), 5, 15000);
        QVERIFY(h.call("graphAllBranches").toBool());
        QVERIFY2(h.call("graphLineCount").toInt()
                     > h.call("graphCommitCount").toInt(),
                 "the merge must draw join rows between commit rows");

        // Rows must actually render.
        const auto graphRows = [&h]() {
            (void)h.window->grabWindow();
            QTest::qWait(60);
            return visibleItemsNamed(h.window, QStringLiteral("gitGraphRow"));
        };
        QList<QQuickItem*> rows;
        QTRY_VERIFY_WITH_TIMEOUT(graphRows().size() >= 5, 5000);
        rows = graphRows();
        QVERIFY2(rows.first()->width() > 300.0,
                 qPrintable(QStringLiteral("graph row collapsed: %1 wide")
                                .arg(rows.first()->width())));
        captureIfRequested(h.window, QStringLiteral("p3d_graph"));

        // Narrowing to the current branch drops the unmerged commit.
        QVERIFY(clickItemWhenReady(h.window, QStringLiteral("graphScopeButton")));
        QTRY_VERIFY_WITH_TIMEOUT(!h.call("graphAllBranches").toBool(), 15000);
        QTRY_COMPARE_WITH_TIMEOUT(h.call("graphCommitCount").toInt(), 4, 15000);

        // Clicking a commit row opens its details and closes the graph. The
        // scope switch replaced the model, so the earlier row pointers are gone:
        // re-collect before touching them.
        QList<QQuickItem*> fresh;
        QTRY_VERIFY_WITH_TIMEOUT((fresh = graphRows(), fresh.size() >= 5), 5000);
        // Join rows ("|\", "|/") carry no commit and are deliberately not
        // clickable, so pick the first row that actually names one.
        QQuickItem* commitRow = nullptr;
        for (QQuickItem* row : fresh) {
            const QVariantMap data = row->property("modelData").toMap();
            if (data.value("hasCommit").toBool()) {
                commitRow = row;
                break;
            }
        }
        QVERIFY2(commitRow != nullptr, "at least one graph row must name a commit");
        QList<QQuickItem*> areas;
        collectVisualItems(commitRow, QStringLiteral("gitGraphRowArea"), &areas);
        QCOMPARE(areas.size(), 1);
        QVERIFY(clickItemObject(h.window, areas.first()));
        QTRY_VERIFY(h.call("commitDialogVisible").toBool());
        QTRY_VERIFY(!h.call("graphVisible").toBool());
    }
};

QTEST_MAIN(TestGitPanelQml)
#include "test_git_panel_qml.moc"