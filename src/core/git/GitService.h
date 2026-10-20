#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace NexusAI {

// One commit as listed by `git log` (metadata only, no diff).
struct GitCommitInfo {
    QString sha;
    QString shortSha;
    QString author;
    QString authorEmail;
    QString subject;
    QString body;
    QString parents;   // space-separated parent SHAs
    qint64 timestamp{0};

    bool isValid() const { return !sha.isEmpty(); }
    QJsonObject toJson() const;
};

// One file entry of a commit (or a working-tree status entry).
struct GitFileStat {
    QString status;    // A, M, D, R###, C###, T, U, X
    QString path;      // post-image path
    QString oldPath;   // set for renames/copies
    int additions{0};
    int removals{0};
    bool binary{false};

    bool isValid() const { return !path.isEmpty(); }
    QJsonObject toJson() const;
};

struct GitStatusEntry {
    QChar indexStatus;     // staged status ('M', 'A', 'R', '?', ...)
    QChar worktreeStatus;  // unstaged status
    QString path;
    QString oldPath;       // set for renames/copies

    bool isValid() const { return !path.isEmpty(); }
    QJsonObject toJson() const;
};

// Read-only git history service.
//
// The app never auto-commits: this service only inspects an existing
// repository for the file-change views. All commands run with an argument
// list (no shell), `GIT_OPTIONAL_LOCKS=0` (git must not touch the index),
// a hard timeout and a bounded output buffer. Revision arguments are
// restricted to hex SHAs and repository-relative paths are validated before
// they reach git, so checkpoint/UI data cannot turn into option injection.
class GitService {
public:
    // `projectRoot` empty resolves to the active project, then to the
    // process working directory.
    explicit GitService(const QString& projectRoot = QString(),
                        int outputLimitBytes = 2 * 1024 * 1024,
                        int timeoutMs = 8000);

    static bool isGitAvailable(QString* error = nullptr);

    // Repository detection. A subdirectory of a repo resolves to its root.
    bool isRepository() const;
    QString repositoryRoot() const;
    // {isRepo, root, branch, detached, head, dirty, error}
    QJsonObject repoInfo() const;

    // History. `path` (repo-relative or absolute inside the repo) filters to
    // commits touching it. fileHistory additionally follows renames.
    QVector<GitCommitInfo> log(int limit = 50, int offset = 0,
                               const QString& path = QString(),
                               QString* error = nullptr) const;
    QVector<GitCommitInfo> fileHistory(const QString& path, int limit = 20,
                                       QString* error = nullptr) const;
    GitCommitInfo commit(const QString& sha, QString* error = nullptr) const;
    QVector<GitFileStat> commitFiles(const QString& sha,
                                     QString* error = nullptr) const;
    // Bounded unified diff for one commit, optionally one path.
    QString commitDiff(const QString& sha, const QString& path = QString(),
                       bool* truncated = nullptr, QString* error = nullptr) const;

    // Working tree: structured status and bounded diffs. `staged` selects
    // the index-vs-HEAD diff; otherwise the worktree-vs-index diff.
    QVector<GitStatusEntry> status(const QString& path = QString(),
                                   QString* error = nullptr) const;
    // Porcelain text for the existing git_status tool (empty = clean).
    // Returns a null QString on failure so callers can distinguish clean.
    QString statusText(const QString& path = QString(),
                       QString* error = nullptr) const;
    QString workingDiff(const QString& path = QString(), bool staged = false,
                        bool* truncated = nullptr, QString* error = nullptr) const;

private:
    struct CommandResult {
        bool ok{false};
        int exitCode{-1};
        QString output;
        QString error;
        bool truncated{false};
        bool timedOut{false};
    };

    CommandResult run(const QStringList& args) const;
    QString baseDir() const;
    QString normalizePath(const QString& root, const QString& path,
                          bool* ok) const;
    static bool validSha(const QString& sha);
    static QVector<GitCommitInfo> parseLog(const QString& output);
    static void setError(QString* error, const QString& message);

    QString m_projectRoot;
    int m_outputLimitBytes;
    int m_timeoutMs;
};

} // namespace NexusAI
