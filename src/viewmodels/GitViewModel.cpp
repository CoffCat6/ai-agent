#include "GitViewModel.h"

#include "core/git/GitService.h"

#include <QFutureWatcher>
#include <QJSEngine>
#include <QJsonObject>
#include <QtConcurrent>

namespace NexusAI {

namespace {

struct HistoryResult {
    QVariantList commits;
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

} // namespace

GitViewModel* GitViewModel::s_instance = nullptr;

GitViewModel::GitViewModel(QObject* parent) : QObject(parent) {}

GitViewModel* GitViewModel::instance() {
    if (!s_instance) s_instance = new GitViewModel(nullptr);
    return s_instance;
}

GitViewModel* GitViewModel::create(QQmlEngine*, QJSEngine*) {
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}

void GitViewModel::setRepoInfoLoading(bool loading) {
    if (m_repoInfoLoading == loading) return;
    m_repoInfoLoading = loading;
    emit repoInfoLoadingChanged();
}

void GitViewModel::refreshRepoInfo() {
    // A newer refresh supersedes an older one still in flight.
    const int generation = ++m_repoGeneration;
    setRepoInfoLoading(true);
    runAsync<QJsonObject>(
        this,
        []() { return GitService().repoInfo(); },
        [this, generation](const QJsonObject& info) {
            if (generation != m_repoGeneration) return;
            m_repoInfo = info.toVariantMap();
            setRepoInfoLoading(false);
            emit repoInfoChanged();
        });
}

QString GitViewModel::nextRequestId() {
    return QStringLiteral("git-%1").arg(++m_nextRequestId);
}

QString GitViewModel::requestFileHistory(const QString& path, int limit) {
    const QString requestId = nextRequestId();
    const int capped = qBound(1, limit, 200);
    runAsync<HistoryResult>(
        this,
        [path, capped]() {
            HistoryResult result;
            QString error;
            const QVector<GitCommitInfo> commits =
                GitService().fileHistory(path, capped, &error);
            result.commits.reserve(commits.size());
            for (const GitCommitInfo& commit : commits)
                result.commits.append(commit.toJson().toVariantMap());
            result.error = error;
            return result;
        },
        [this, requestId, path](const HistoryResult& result) {
            emit fileHistoryReady(requestId, path, result.commits, result.error);
        });
    return requestId;
}

QString GitViewModel::requestCommitDiff(const QString& sha, const QString& path) {
    const QString requestId = nextRequestId();
    runAsync<DiffResult>(
        this,
        [sha, path]() {
            DiffResult result;
            GitService git;
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
        [this, requestId, sha, path](const DiffResult& result) {
            emit commitDiffReady(requestId, sha, path, result.diff,
                                 result.truncated, result.error);
        });
    return requestId;
}

QString GitViewModel::requestWorkingDiff(const QString& path, bool staged) {
    const QString requestId = nextRequestId();
    runAsync<DiffResult>(
        this,
        [path, staged]() {
            DiffResult result;
            result.diff = GitService().workingDiff(path, staged,
                                                   &result.truncated,
                                                   &result.error);
            if (!result.error.isEmpty()) result.diff.clear();
            return result;
        },
        [this, requestId, path, staged](const DiffResult& result) {
            emit workingDiffReady(requestId, path, staged, result.diff,
                                  result.truncated, result.error);
        });
    return requestId;
}

} // namespace NexusAI
