#include "GitViewModel.h"

#include "core/git/GitService.h"
#include "core/tools/Sandbox.h"

#include <QDesktopServices>
#include <QProcess>
#include <QProcessEnvironment>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJSEngine>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrent>

#include <memory>

namespace NexusAI {

namespace {

struct HistoryResult {
    QVariantList commits;
    QString error;
};

struct StashResult {
    QVariantList entries;
    QString error;
};

struct HunkResult {
    QVariantList hunks;
    QString error;
};

struct BranchViewResult {
    QVariantMap payload;
    QString error;
};

struct FilesResult {
    QVariantList files;
    QString error;
};

struct StatusResult {
    QVariantList entries;
    QString error;
};

struct DiffResult {
    QString diff;
    bool truncated{false};
    QString error;
};

template <typename Result, typename Work, typename Done>
void runAsync(QObject* context, Work work, Done done) {
    auto* watcher = new QFutureWatcher<Result>(context);
    QObject::connect(watcher, &QFutureWatcher<Result>::finished, context,
                     [watcher, done]() {
                         const Result result = watcher->result();
                         watcher->deleteLater();
                         done(result);
                     });
    watcher->setFuture(QtConcurrent::run(work));
}

template <typename Info>
QVariantList toVariantList(const QVector<Info>& items) {
    QVariantList out;
    out.reserve(items.size());
    for (const Info& item : items)
        out.append(item.toJson().toVariantMap());
    return out;
}

// QML hands back the hunk rows it is showing; anything that is not a positive
// index with a header is dropped rather than trusted.
QVector<GitHunkRef> toHunkRefs(const QVariantList& selected) {
    QVector<GitHunkRef> refs;
    for (const QVariant& value : selected) {
        const QVariantMap row = value.toMap();
        if (row.isEmpty()) continue;
        bool ok = false;
        const int index = row.value("index").toInt(&ok);
        if (!ok || index < 0) continue;
        GitHunkRef ref;
        ref.index = index;
        ref.header = row.value("header").toString();
        refs.append(ref);
    }
    return refs;
}

} // namespace

GitViewModel* GitViewModel::s_instance = nullptr;

GitViewModel::GitViewModel(QObject* parent) : QObject(parent) {
    const QString dataDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!dataDir.isEmpty())
        m_discardBackupRoot = dataDir + QStringLiteral("/git-discard-backup");
}

void GitViewModel::setDiscardBackupRoot(const QString& path) {
    const QString trimmed = path.trimmed();
    if (trimmed == m_discardBackupRoot) return;
    m_discardBackupRoot = trimmed;
    emit operationStateChanged();
}

GitViewModel* GitViewModel::instance() {
    if (!s_instance) s_instance = new GitViewModel(nullptr);
    return s_instance;
}

GitViewModel* GitViewModel::create(QQmlEngine*, QJSEngine*) {
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}

QString GitViewModel::effectiveRoot() const {
    return m_projectRoot.trimmed().isEmpty() ? Sandbox::currentProjectDir()
                                             : m_projectRoot;
}

void GitViewModel::setProjectRoot(const QString& projectRoot) {
    const QString trimmed = projectRoot.trimmed();
    if (trimmed == m_projectRoot) return;
    m_projectRoot = trimmed;
    // Anything still in flight belongs to the previous root.
    ++m_rootGeneration;
    ++m_repoGeneration;
    ++m_statusGeneration;
    ++m_writeGeneration;
    m_repoInfo = {};
    m_statusEntries = {};
    m_statusError.clear();
    setRepoInfoLoading(false);
    setStatusLoading(false);
    if (m_operationBusy) {
        m_operationBusy = false;
        emit operationBusyChanged();
    }
    if (!m_operationError.isEmpty()) {
        m_operationError.clear();
        emit operationStateChanged();
    }
    emit projectRootChanged();
    emit repoInfoChanged();
    emit statusChanged();
}

void GitViewModel::setRepoInfoLoading(bool loading) {
    if (m_repoInfoLoading == loading) return;
    m_repoInfoLoading = loading;
    emit repoInfoLoadingChanged();
}

void GitViewModel::setStatusLoading(bool loading) {
    if (m_statusLoading == loading) return;
    m_statusLoading = loading;
    emit statusLoadingChanged();
}

void GitViewModel::refreshRepoInfo() {
    // A newer refresh supersedes an older one still in flight.
    const int generation = ++m_repoGeneration;
    const int rootGeneration = m_rootGeneration;
    const QString root = effectiveRoot();
    setRepoInfoLoading(true);
    runAsync<QJsonObject>(
        this,
        [root]() { return GitService(root).repoInfo(); },
        [this, generation, rootGeneration](const QJsonObject& info) {
            if (generation != m_repoGeneration
                    || rootGeneration != m_rootGeneration)
                return;
            m_repoInfo = info.toVariantMap();
            setRepoInfoLoading(false);
            emit repoInfoChanged();
        });
}

void GitViewModel::refreshStatus() {
    const int generation = ++m_statusGeneration;
    const int rootGeneration = m_rootGeneration;
    const QString root = effectiveRoot();
    m_statusError.clear();
    emit statusChanged();
    setStatusLoading(true);
    runAsync<StatusResult>(
        this,
        [root]() {
            StatusResult result;
            QString error;
            const QVector<GitStatusEntry> entries = GitService(root).status(
                QString(), &error);
            result.entries.reserve(entries.size());
            for (const GitStatusEntry& entry : entries)
                result.entries.append(entry.toJson().toVariantMap());
            result.error = error;
            return result;
        },
        [this, generation, rootGeneration](const StatusResult& result) {
            if (generation != m_statusGeneration
                    || rootGeneration != m_rootGeneration)
                return;
            m_statusEntries = result.entries;
            m_statusError = result.error;
            setStatusLoading(false);
            emit statusChanged();
        });
}

QString GitViewModel::nextRequestId() {
    return QStringLiteral("git-%1").arg(++m_nextRequestId);
}

QString GitViewModel::requestRepoLog(int limit, int offset,
                                     const QString& grep,
                                     const QString& author) {
    const QString requestId = nextRequestId();
    const int rootGeneration = m_rootGeneration;
    const QString root = effectiveRoot();
    GitLogQuery query;
    query.limit = qBound(1, limit, 200);
    query.offset = qMax(0, offset);
    query.grep = grep.trimmed();
    query.author = author.trimmed();
    const int skip = query.offset;
    runAsync<HistoryResult>(
        this,
        [root, query]() {
            HistoryResult result;
            QString error;
            const QVector<GitCommitInfo> commits = GitService(root).log(query, &error);
            result.commits = toVariantList(commits);
            result.error = error;
            return result;
        },
        [this, requestId, skip, rootGeneration](const HistoryResult& result) {
            if (rootGeneration != m_rootGeneration) return;
            emit repoLogReady(requestId, result.commits, skip, result.error);
        });
    return requestId;
}

QString GitViewModel::requestCommitFiles(const QString& sha) {
    const QString requestId = nextRequestId();
    const int rootGeneration = m_rootGeneration;
    const QString root = effectiveRoot();
    runAsync<FilesResult>(
        this,
        [root, sha]() {
            FilesResult result;
            QString error;
            const QVector<GitFileStat> files =
                GitService(root).commitFiles(sha, &error);
            result.files = toVariantList(files);
            result.error = error;
            return result;
        },
        [this, requestId, sha, rootGeneration](const FilesResult& result) {
            if (rootGeneration != m_rootGeneration) return;
            emit commitFilesReady(requestId, sha, result.files, result.error);
        });
    return requestId;
}

QString GitViewModel::requestFileHistory(const QString& path, int limit) {
    const QString requestId = nextRequestId();
    const int capped = qBound(1, limit, 200);
    const int rootGeneration = m_rootGeneration;
    const QString root = effectiveRoot();
    runAsync<HistoryResult>(
        this,
        [root, path, capped]() {
            HistoryResult result;
            QString error;
            const QVector<GitCommitInfo> commits =
                GitService(root).fileHistory(path, capped, &error);
            result.commits = toVariantList(commits);
            result.error = error;
            return result;
        },
        [this, requestId, path, rootGeneration](const HistoryResult& result) {
            if (rootGeneration != m_rootGeneration) return;
            emit fileHistoryReady(requestId, path, result.commits, result.error);
        });
    return requestId;
}

QString GitViewModel::requestCompareFiles(const QString& shaA,
                                          const QString& shaB) {
    const QString requestId = nextRequestId();
    const int rootGeneration = m_rootGeneration;
    const QString root = effectiveRoot();
    runAsync<FilesResult>(
        this,
        [root, shaA, shaB]() {
            FilesResult result;
            QString error;
            const QVector<GitFileStat> files =
                GitService(root).compareFiles(shaA, shaB, &error);
            result.files = toVariantList(files);
            result.error = error;
            return result;
        },
        [this, requestId, shaA, shaB,
         rootGeneration](const FilesResult& result) {
            if (rootGeneration != m_rootGeneration) return;
            emit compareFilesReady(requestId, shaA, shaB, result.files,
                                   result.error);
        });
    return requestId;
}

QString GitViewModel::requestCompareDiff(const QString& shaA,
                                         const QString& shaB,
                                         const QString& path) {
    const QString requestId = nextRequestId();
    const int rootGeneration = m_rootGeneration;
    const QString root = effectiveRoot();
    runAsync<DiffResult>(
        this,
        [root, shaA, shaB, path]() {
            DiffResult result;
            result.diff = GitService(root).compareDiff(
                shaA, shaB, path, &result.truncated, &result.error);
            if (!result.error.isEmpty()) result.diff.clear();
            return result;
        },
        [this, requestId, shaA, shaB, path,
         rootGeneration](const DiffResult& result) {
            if (rootGeneration != m_rootGeneration) return;
            emit compareDiffReady(requestId, shaA, shaB, path, result.diff,
                                  result.truncated, result.error);
        });
    return requestId;
}

QString GitViewModel::requestCommitDiff(const QString& sha, const QString& path) {
    const QString requestId = nextRequestId();
    const int rootGeneration = m_rootGeneration;
    const QString root = effectiveRoot();
    runAsync<DiffResult>(
        this,
        [root, sha, path]() {
            DiffResult result;
            GitService git(root);
            result.diff = git.commitDiff(sha, path, &result.truncated,
                                         &result.error);
            // Pre-rename commits do not match the current path. Fall back to
            // the whole commit so history rows still show their change.
            if (result.error.isEmpty() && !path.isEmpty()
                    && result.diff.trimmed().isEmpty()) {
                bool wholeTruncated = false;
                QString wholeError;
                const QString whole = git.commitDiff(sha, QString(),
                                                     &wholeTruncated,
                                                     &wholeError);
                if (wholeError.isEmpty() && !whole.trimmed().isEmpty()) {
                    result.diff = whole;
                    result.truncated = wholeTruncated;
                }
            }
            if (!result.error.isEmpty()) result.diff.clear();
            return result;
        },
        [this, requestId, sha, path, rootGeneration](const DiffResult& result) {
            if (rootGeneration != m_rootGeneration) return;
            emit commitDiffReady(requestId, sha, path, result.diff,
                                 result.truncated, result.error);
        });
    return requestId;
}

QString GitViewModel::startWriteOperation(
    const QString& action, const QString& root,
    const std::function<bool(GitService&, QString*)>& work,
    const std::function<void()>& afterDone) {
    if (root.isEmpty()) {
        m_operationError = QStringLiteral("No active project to run git in");
        emit operationStateChanged();
        return {};
    }
    if (m_operationBusy) {
        m_operationError = QStringLiteral("Another git operation is still running");
        emit operationStateChanged();
        return {};
    }

    const QString requestId = nextRequestId();
    const int rootGeneration = m_rootGeneration;
    const quint64 writeGeneration = ++m_writeGeneration;
    m_operationBusy = true;
    m_operationError.clear();
    if (!m_lastBackupPath.isEmpty()) m_lastBackupPath.clear();
    emit operationBusyChanged();
    emit operationStateChanged();

    struct WriteResult {
        bool success{false};
        QString error;
    };
    runAsync<WriteResult>(
        this,
        [root, work]() {
            WriteResult result;
            // Writes can wait on hooks or large indexes; give them more time
            // than read-only inspection.
            GitService git(root, 2 * 1024 * 1024, 60000);
            result.success = work(git, &result.error);
            return result;
        },
        [this, requestId, action, rootGeneration, writeGeneration,
         afterDone](const WriteResult& result) {
            if (rootGeneration != m_rootGeneration
                    || writeGeneration != m_writeGeneration)
                return;
            m_operationBusy = false;
            m_operationError = result.error;
            emit operationBusyChanged();
            emit operationStateChanged();
            if (result.success && afterDone) afterDone();
            if (result.success) {
                refreshStatus();
                refreshRepoInfo();
            }
            emit operationFinished(requestId, action, result.success,
                                   result.error);
        });
    return requestId;
}

QString GitViewModel::stageFiles(const QStringList& paths) {
    return startWriteOperation(
        QStringLiteral("stage"), effectiveRoot(),
        [paths](GitService& git, QString* error) {
            return git.stage(paths, error);
        });
}

QString GitViewModel::unstageFiles(const QStringList& paths) {
    return startWriteOperation(
        QStringLiteral("unstage"), effectiveRoot(),
        [paths](GitService& git, QString* error) {
            return git.unstage(paths, error);
        });
}

QString GitViewModel::commitStaged(const QString& message) {
    return startWriteOperation(
        QStringLiteral("commit"), effectiveRoot(),
        [message](GitService& git, QString* error) {
            return git.commitStaged(message, error);
        });
}

QString GitViewModel::discardPath(const QString& path) {
    const QString backupRoot = m_discardBackupRoot;
    auto backup = std::make_shared<QString>();
    return startWriteOperation(
        QStringLiteral("discard"), effectiveRoot(),
        [path, backupRoot, backup](GitService& git, QString* error) {
            return git.discardPath(path, backupRoot, backup.get(), error);
        },
        [this, backup]() {
            m_lastBackupPath = *backup;
            emit operationStateChanged();
        });
}

QString GitViewModel::amendLast(const QString& message) {
    return startWriteOperation(
        QStringLiteral("amend"), effectiveRoot(),
        [message](GitService& git, QString* error) {
            return git.amendLast(message, error);
        });
}

QString GitViewModel::undoLastCommit() {
    return startWriteOperation(
        QStringLiteral("undo"), effectiveRoot(),
        [](GitService& git, QString* error) {
            return git.undoLastCommit(error);
        });
}

QString GitViewModel::stashPush(const QString& message) {
    return startWriteOperation(
        QStringLiteral("stashPush"), effectiveRoot(),
        [message](GitService& git, QString* error) {
            return git.stashPush(message, error);
        });
}

QString GitViewModel::stashPop(int index) {
    return startWriteOperation(
        QStringLiteral("stashPop"), effectiveRoot(),
        [index](GitService& git, QString* error) {
            return git.stashPop(index, error);
        });
}

QString GitViewModel::requestStashList() {
    const QString requestId = nextRequestId();
    const int rootGeneration = m_rootGeneration;
    const QString root = effectiveRoot();
    runAsync<StashResult>(
        this,
        [root]() {
            StashResult result;
            QString error;
            const QVector<GitStashEntry> entries =
                GitService(root).stashList(&error);
            result.entries = toVariantList(entries);
            result.error = error;
            return result;
        },
        [this, requestId, rootGeneration](const StashResult& result) {
            if (rootGeneration != m_rootGeneration) return;
            emit stashListReady(requestId, result.entries, result.error);
        });
    return requestId;
}

QString GitViewModel::requestDiffHunks(const QString& path, bool staged) {
    const QString requestId = nextRequestId();
    const int rootGeneration = m_rootGeneration;
    const QString root = effectiveRoot();
    runAsync<HunkResult>(
        this,
        [root, path, staged]() {
            HunkResult result;
            QString error;
            const QVector<GitHunk> hunks =
                GitService(root).diffHunks(path, staged, &error);
            result.hunks = toVariantList(hunks);
            result.error = error;
            return result;
        },
        [this, requestId, path, staged,
         rootGeneration](const HunkResult& result) {
            if (rootGeneration != m_rootGeneration) return;
            emit diffHunksReady(requestId, path, staged, result.hunks,
                                result.error);
        });
    return requestId;
}

QString GitViewModel::requestBlame(const QString& path) {
    const QString requestId = nextRequestId();
    const int rootGeneration = m_rootGeneration;
    const QString root = effectiveRoot();
    runAsync<HunkResult>(
        this,
        [root, path]() {
            HunkResult result;
            QString error;
            const QVector<GitBlameLine> lines =
                GitService(root).blame(path, &error);
            result.hunks = toVariantList(lines);
            result.error = error;
            return result;
        },
        [this, requestId, path, rootGeneration](const HunkResult& result) {
            if (rootGeneration != m_rootGeneration) return;
            emit blameReady(requestId, path, result.hunks, result.error);
        });
    return requestId;
}

QString GitViewModel::requestGraph(int limit, bool allBranches) {
    const QString requestId = nextRequestId();
    const int rootGeneration = m_rootGeneration;
    const QString root = effectiveRoot();
    const int capped = qBound(1, limit, 300);
    runAsync<HunkResult>(
        this,
        [root, capped, allBranches]() {
            HunkResult result;
            QString error;
            const QVector<GitGraphLine> lines =
                GitService(root).graph(capped, allBranches, &error);
            result.hunks = toVariantList(lines);
            result.error = error;
            return result;
        },
        [this, requestId, rootGeneration](const HunkResult& result) {
            if (rootGeneration != m_rootGeneration) return;
            emit graphReady(requestId, result.hunks, result.error);
        });
    return requestId;
}

QString GitViewModel::requestBranchView() {
    const QString requestId = nextRequestId();
    const int rootGeneration = m_rootGeneration;
    const QString root = effectiveRoot();
    runAsync<BranchViewResult>(
        this,
        [root]() {
            BranchViewResult result;
            GitService git(root);
            QString error;
            const QVector<GitBranchInfo> branches = git.branches(&error);
            if (!error.isEmpty()) {
                result.error = error;
                return result;
            }
            const QVector<GitRemoteInfo> remotes = git.remotes(&error);
            if (!error.isEmpty()) {
                result.error = error;
                return result;
            }
            result.payload["branches"] = toVariantList(branches);
            result.payload["remotes"] = toVariantList(remotes);
            const QVector<GitTagInfo> tagList = git.tags(&error);
            if (!error.isEmpty()) {
                result.error = error;
                return result;
            }
            result.payload["tags"] = toVariantList(tagList);
            return result;
        },
        [this, requestId, rootGeneration](const BranchViewResult& result) {
            if (rootGeneration != m_rootGeneration) return;
            emit branchViewReady(requestId, result.payload, result.error);
        });
    return requestId;
}

QString GitViewModel::stageHunks(const QString& path,
                                 const QVariantList& selected) {
    const QVector<GitHunkRef> refs = toHunkRefs(selected);
    return startWriteOperation(
        QStringLiteral("stageHunk"), effectiveRoot(),
        [path, refs](GitService& git, QString* error) {
            return git.applyHunks(path, refs, false, false, error);
        });
}

QString GitViewModel::unstageHunks(const QString& path,
                                   const QVariantList& selected) {
    const QVector<GitHunkRef> refs = toHunkRefs(selected);
    return startWriteOperation(
        QStringLiteral("unstageHunk"), effectiveRoot(),
        [path, refs](GitService& git, QString* error) {
            return git.applyHunks(path, refs, true, true, error);
        });
}

QString GitViewModel::absolutePath(const QString& path) const {
    const QString root = effectiveRoot().trimmed();
    const QString wanted = path.trimmed();
    if (root.isEmpty() || wanted.isEmpty()) return {};
    // Same rejection as GitService: a leading dash could become an option.
    if (wanted.startsWith(QLatin1Char('-'))
            || wanted.startsWith(QLatin1Char(':')))
        return {};

    QString prefix = QDir::cleanPath(QDir::fromNativeSeparators(root));
    if (!prefix.endsWith(QLatin1Char('/'))) prefix += QLatin1Char('/');
    const QString clean = QDir::fromNativeSeparators(wanted);
    const QString absolute = QDir::cleanPath(
        QDir::isAbsolutePath(clean) ? clean : prefix + clean);

#ifdef Q_OS_WIN
    const Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    // cleanPath already resolved any "..", so anything escaping the project
    // simply stops matching the prefix.
    if (!absolute.startsWith(prefix, sensitivity)
            || absolute.size() <= prefix.size())
        return {};
    return QDir::toNativeSeparators(absolute);
}

bool GitViewModel::openInDefaultApp(const QString& path) {
    const QString absolute = absolutePath(path);
    if (absolute.isEmpty() || !QFileInfo::exists(absolute)) return false;
    // Same ELECTRON_RUN_AS_NODE scrub as ProjectFileViewModel: a handler
    // inherited from a VSCode-family terminal must not boot as Node.
    QProcessEnvironment::systemEnvironment().remove(
        QStringLiteral("ELECTRON_RUN_AS_NODE"));
    return QDesktopServices::openUrl(QUrl::fromLocalFile(absolute));
}

QString GitViewModel::requestWorkingDiff(const QString& path, bool staged) {
    const QString requestId = nextRequestId();
    const int rootGeneration = m_rootGeneration;
    const QString root = effectiveRoot();
    runAsync<DiffResult>(
        this,
        [root, path, staged]() {
            DiffResult result;
            result.diff = GitService(root).workingDiff(path, staged,
                                                       &result.truncated,
                                                       &result.error);
            if (!result.error.isEmpty()) result.diff.clear();
            return result;
        },
        [this, requestId, path, staged, rootGeneration](const DiffResult& result) {
            if (rootGeneration != m_rootGeneration) return;
            emit workingDiffReady(requestId, path, staged, result.diff,
                                  result.truncated, result.error);
        });
    return requestId;
}

} // namespace NexusAI