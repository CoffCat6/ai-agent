#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace NexusAI {

class GitService;

// Async QML facade for GitService. Every git call runs on the global thread
// pool (QProcess must never block the GUI thread); results come back through
// per-request signals carrying an opaque request id, so a dialog can ignore
// answers for a path it no longer shows.
//
// `projectRoot` pins every call to one project. An empty value falls back to
// Sandbox's active project directory for backwards compatibility with code
// that has no panel context; the right-panel Git view always sets it
// explicitly. Switching roots clears repo/status state and generation guards
// drop in-flight answers from the previous root.
class GitViewModel : public QObject {
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(GitVM)

    Q_PROPERTY(QString projectRoot READ projectRoot WRITE setProjectRoot
                   NOTIFY projectRootChanged)
    Q_PROPERTY(QVariantMap repoInfo READ repoInfo NOTIFY repoInfoChanged)
    Q_PROPERTY(bool repoInfoLoading READ repoInfoLoading
                   NOTIFY repoInfoLoadingChanged)
    Q_PROPERTY(QVariantList statusEntries READ statusEntries
                   NOTIFY statusChanged)
    Q_PROPERTY(bool statusLoading READ statusLoading
                   NOTIFY statusLoadingChanged)
    Q_PROPERTY(QString statusError READ statusError NOTIFY statusChanged)
    Q_PROPERTY(bool operationBusy READ operationBusy
                   NOTIFY operationBusyChanged)
    Q_PROPERTY(QString operationError READ operationError
                   NOTIFY operationStateChanged)
    // Untracked files are copied here before a discard deletes them. Defaults
    // to <AppData>/git-discard-backup; settable so tests stay off the real
    // profile directory.
    Q_PROPERTY(QString discardBackupRoot READ discardBackupRoot
                   WRITE setDiscardBackupRoot NOTIFY operationStateChanged)
    // Where the most recent discard wrote its backup, empty for other actions.
    Q_PROPERTY(QString lastBackupPath READ lastBackupPath
                   NOTIFY operationStateChanged)

public:
    explicit GitViewModel(QObject* parent = nullptr);

    static GitViewModel* instance();
    static GitViewModel* create(QQmlEngine*, QJSEngine*);

    QString projectRoot() const { return m_projectRoot; }
    void setProjectRoot(const QString& projectRoot);

    QVariantMap repoInfo() const { return m_repoInfo; }
    bool repoInfoLoading() const { return m_repoInfoLoading; }
    QVariantList statusEntries() const { return m_statusEntries; }
    bool statusLoading() const { return m_statusLoading; }
    QString statusError() const { return m_statusError; }
    bool operationBusy() const { return m_operationBusy; }
    QString operationError() const { return m_operationError; }
    QString discardBackupRoot() const { return m_discardBackupRoot; }
    void setDiscardBackupRoot(const QString& path);
    QString lastBackupPath() const { return m_lastBackupPath; }

    Q_INVOKABLE void refreshRepoInfo();
    Q_INVOKABLE void refreshStatus();
    // Repo-wide history page. `offset` is a commit count; responses echo it so
    // the caller can append pages without racing newer requests.
    // Repo history page. `grep` and `author` are fixed, case-insensitive
    // substrings and both must match; empty means "no filter on that kind".
    Q_INVOKABLE QString requestRepoLog(int limit = 30, int offset = 0,
                                       const QString& grep = QString(),
                                       const QString& author = QString());
    Q_INVOKABLE QString requestCommitFiles(const QString& sha);
    Q_INVOKABLE QString requestFileHistory(const QString& path, int limit = 20);
    Q_INVOKABLE QString requestCommitDiff(const QString& sha,
                                          const QString& path = QString());
    // Any-two-commits comparison, read-only like the rest of the history view.
    Q_INVOKABLE QString requestCompareFiles(const QString& shaA,
                                            const QString& shaB);
    Q_INVOKABLE QString requestCompareDiff(const QString& shaA,
                                           const QString& shaB,
                                           const QString& path = QString());
    Q_INVOKABLE QString requestWorkingDiff(const QString& path = QString(),
                                           bool staged = false);

    // Explicit user actions only. One write runs at a time; a call while
    // operationBusy returns an empty id. `action` is "stage", "unstage" or
    // "commit" and is echoed by operationFinished.
    Q_INVOKABLE QString stageFiles(const QStringList& paths);
    Q_INVOKABLE QString unstageFiles(const QStringList& paths);
    Q_INVOKABLE QString commitStaged(const QString& message);

    // Recovery actions, each behind its own confirmation in the panel. They
    // share the one-write-at-a-time gate above; `action` is "discard", "amend",
    // "undo", "stashPush" or "stashPop". GitService refuses history rewriting
    // once HEAD is published on a remote.
    Q_INVOKABLE QString discardPath(const QString& path);
    Q_INVOKABLE QString amendLast(const QString& message);
    Q_INVOKABLE QString undoLastCommit();
    Q_INVOKABLE QString stashPush(const QString& message);
    Q_INVOKABLE QString stashPop(int index);
    Q_INVOKABLE QString requestStashList();

    // Partial staging of one file. `path` is repo-relative as the panel reports
    // it; `selected` carries the hunk rows the UI is showing, each as
    // {index, header}. The header is re-checked against a freshly parsed diff
    // before anything is written, so a stale list cannot stage the wrong block.
    Q_INVOKABLE QString requestDiffHunks(const QString& path, bool staged = false);
    Q_INVOKABLE QString stageHunks(const QString& path,
                                   const QVariantList& selected);
    Q_INVOKABLE QString unstageHunks(const QString& path,
                                     const QVariantList& selected);

    // Read-only branch view: {branches: [...], remotes: [...]} built from refs
    // already in the repository. Nothing here fetches, checks out or pushes.
    Q_INVOKABLE QString requestBranchView();
    // Per-line attribution for one tracked file (read-only).
    Q_INVOKABLE QString requestBlame(const QString& path);
    // Multi-branch commit graph from local refs (read-only, never fetches).
    Q_INVOKABLE QString requestGraph(int limit = 80, bool allBranches = true);

    // File-level helpers for the Git panel: a repository-contained absolute
    // path (empty when the input would escape the project) and opening it with
    // the OS handler for that file type.
    Q_INVOKABLE QString absolutePath(const QString& path) const;
    Q_INVOKABLE bool openInDefaultApp(const QString& path);

signals:
    void projectRootChanged();
    void repoInfoChanged();
    void repoInfoLoadingChanged();
    void statusChanged();
    void statusLoadingChanged();
    void operationBusyChanged();
    void operationStateChanged();

    void repoLogReady(const QString& requestId, const QVariantList& commits,
                      int offset, const QString& error);
    void commitFilesReady(const QString& requestId, const QString& sha,
                          const QVariantList& files, const QString& error);
    void fileHistoryReady(const QString& requestId, const QString& path,
                          const QVariantList& commits, const QString& error);
    void commitDiffReady(const QString& requestId, const QString& sha,
                         const QString& path, const QString& diff,
                         bool truncated, const QString& error);
    void workingDiffReady(const QString& requestId, const QString& path,
                          bool staged, const QString& diff,
                          bool truncated, const QString& error);
    void operationFinished(const QString& requestId, const QString& action,
                           bool success, const QString& error);
    void stashListReady(const QString& requestId, const QVariantList& entries,
                        const QString& error);
    void diffHunksReady(const QString& requestId, const QString& path,
                        bool staged, const QVariantList& hunks,
                        const QString& error);
    void branchViewReady(const QString& requestId, const QVariantMap& payload,
                         const QString& error);
    void compareFilesReady(const QString& requestId, const QString& shaA,
                           const QString& shaB, const QVariantList& files,
                           const QString& error);
    void compareDiffReady(const QString& requestId, const QString& shaA,
                          const QString& shaB, const QString& path,
                          const QString& diff, bool truncated,
                          const QString& error);
    void blameReady(const QString& requestId, const QString& path,
                    const QVariantList& lines, const QString& error);
    void graphReady(const QString& requestId, const QVariantList& lines,
                    const QString& error);

private:
    QString effectiveRoot() const;
    QString nextRequestId();
    void setRepoInfoLoading(bool loading);
    void setStatusLoading(bool loading);
    QString startWriteOperation(
        const QString& action, const QString& root,
        const std::function<bool(GitService&, QString*)>& work,
        const std::function<void()>& afterDone = {});

    QString m_projectRoot;
    QVariantMap m_repoInfo;
    QVariantList m_statusEntries;
    QString m_statusError;
    QString m_discardBackupRoot;
    QString m_lastBackupPath;
    bool m_repoInfoLoading{false};
    bool m_statusLoading{false};
    bool m_operationBusy{false};
    QString m_operationError;
    int m_repoGeneration{0};
    int m_statusGeneration{0};
    int m_rootGeneration{0};
    quint64 m_writeGeneration{0};
    quint64 m_nextRequestId{0};

    static GitViewModel* s_instance;
};

} // namespace NexusAI