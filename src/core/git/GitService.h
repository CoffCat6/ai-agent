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

// One blamed line: who last touched it and in which commit. `sha` is all zeros
// for regions that exist only in the worktree, which the UI shows as uncommitted
// rather than attributing them to a real commit.
struct GitBlameLine {
    int lineNo{0};         // 1-based line number in the file
    QString sha;
    QString shortSha;
    QString author;
    qint64 timestamp{0};
    QString summary;       // subject of the blamed commit
    QString text;          // the line itself

    // git attributes worktree-only regions to an all-zero commit.
    bool uncommitted() const {
        if (sha.isEmpty()) return true;
        for (const QChar c : sha)
            if (c != QLatin1Char('0')) return false;
        return true;
    }

    QJsonObject toJson() const;
};

// Paging and filters for one history query. `grep` and `author` are matched as
// fixed, case-insensitive substrings, and both must match (git's default for
// limits of different kinds).
struct GitLogQuery {
    int limit{50};
    int offset{0};
    QString path;
    QString grep;
    QString author;
};

// One `git stash list` entry, as shown by the panel's stash picker.
struct GitStashEntry {
    int index{0};          // stash@{index} position
    QString ref;           // stash@{0}
    QString subject;       // On main: message
    qint64 timestamp{0};

    QJsonObject toJson() const;
};

// One hunk of a single file's unified diff, addressable for partial staging.
struct GitHunk {
    int index{0};          // 0-based position inside this file's diff
    QString header;        // "@@ -3,7 +3,9 @@"
    int oldStart{0};
    int oldLines{0};
    int newStart{0};
    int newLines{0};
    int additions{0};
    int removals{0};
    QString summary;       // first added or removed line, for the row label
    QStringList lines;     // hunk body, header excluded

    bool isValid() const { return !lines.isEmpty(); }
    QJsonObject toJson() const;
};

// A hunk the user picked, addressed by position *and* by the header the UI was
// showing. applyHunks refuses when the header no longer matches, so a list
// built before the file or index changed can never stage the wrong block.
struct GitHunkRef {
    int index{0};
    QString header;
};

// One tag: annotated tags carry a tagger date and their own message, lightweight
// ones just point at a commit.
struct GitTagInfo {
    QString name;
    QString sha;           // the commit the tag resolves to
    QString target;        // objectname:short of the tagged object
    bool annotated{false};
    qint64 date{0};        // tagger date for annotated, commit date otherwise
    QString subject;

    bool isValid() const { return !name.isEmpty(); }
    QJsonObject toJson() const;
};

// One row of `git log --graph` output. Rows without a commit are the pure
// graph art (the "|\", "|/" join lines) and are kept so the picture stays
// readable; `sha` is the short id git resolves unambiguously.
struct GitGraphLine {
    int index{0};
    QString art;           // e.g. "* ", "| * ", "|\"
    QString sha;
    QString date;
    QString refs;          // decorations: HEAD -> master, tag: v1, origin/main
    QString subject;
    bool hasCommit{false};

    QJsonObject toJson() const;
};

// One local branch as seen by `for-each-ref` — read-only: the panel never
// checks out, creates or deletes a branch.
struct GitBranchInfo {
    QString name;
    QString sha;
    QString upstream;          // origin/main, empty when the branch has none
    QString track;             // raw `upstream:track`: "[ahead 1, behind 2]", "[gone]"
    QString trackState;        // "ahead" | "behind" | "diverged" | "gone" | "synced"
    int ahead{0};
    int behind{0};
    bool current{false};
    qint64 timestamp{0};

    bool isValid() const { return !name.isEmpty(); }
    QJsonObject toJson() const;
};

// One configured remote with its tracking branches. `url` is always masked:
// credentials embedded in a remote URL must never reach the screen or a log.
struct GitRemoteInfo {
    QString name;
    QString url;
    QStringList branches;

    bool isValid() const { return !name.isEmpty(); }
    QJsonObject toJson() const;
};

// Git inspection plus a small set of explicitly user-triggered writes.
//
// Reads never mutate the repository: they run with `GIT_OPTIONAL_LOCKS=0`.
// Writes (stage/unstage/commit/discard/amend/reset/stash) only happen when the
// user confirms an action in the UI — nothing here runs automatically, history
// rewriting refuses to touch commits already published on a remote, and
// hooks are never skipped. Every command runs as an argument list (no shell)
// with a hard timeout and a bounded output buffer, and revision arguments are
// restricted to hex SHAs and repository-relative paths are validated before
// they reach git, so checkpoint/UI data cannot become option injection.
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
    QVector<GitCommitInfo> log(const GitLogQuery& query,
                               QString* error = nullptr) const;
    QVector<GitCommitInfo> fileHistory(const QString& path, int limit = 20,
                                       QString* error = nullptr) const;
    GitCommitInfo commit(const QString& sha, QString* error = nullptr) const;
    QVector<GitFileStat> commitFiles(const QString& sha,
                                     QString* error = nullptr) const;
    // Files changed between two commits (A..B), for the compare view.
    QVector<GitFileStat> compareFiles(const QString& shaA, const QString& shaB,
                                      QString* error = nullptr) const;
    // Bounded unified diff for one commit, optionally one path.
    QString commitDiff(const QString& sha, const QString& path = QString(),
                       bool* truncated = nullptr, QString* error = nullptr) const;
    // Bounded unified diff between two commits, optionally for one path.
    QString compareDiff(const QString& shaA, const QString& shaB,
                        const QString& path = QString(),
                        bool* truncated = nullptr,
                        QString* error = nullptr) const;

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

    // Write operations. These run only after an explicit user action; the
    // service never commits or stages on its own. Paths are validated like the
    // read APIs, so UI data cannot turn into option injection.
    bool stage(const QStringList& paths, QString* error = nullptr);
    bool unstage(const QStringList& paths, QString* error = nullptr);
    bool commitStaged(const QString& message, QString* error = nullptr);

    // Discard worktree changes for one path. Tracked files go through
    // `git restore` (index content is kept); untracked files are copied under
    // `backupRoot` first and the copy destination is reported in `backupPath`,
    // so a mistaken click stays recoverable. An empty backupRoot makes the
    // untracked case refuse instead of deleting.
    bool discardPath(const QString& path, const QString& backupRoot,
                     QString* backupPath = nullptr, QString* error = nullptr);

    // History rewriting. Both refuse when HEAD is contained in a
    // remote-tracking branch, because a published commit must not be replaced
    // from inside the app. Hooks still run (no --no-verify).
    bool amendLast(const QString& message, QString* error = nullptr);
    // `git reset --soft HEAD^`: the commit disappears, its changes stay
    // staged. Merge commits and the root commit are refused.
    bool undoLastCommit(QString* error = nullptr);

    // Stash of tracked changes only (`git stash push` without -u).
    bool stashPush(const QString& message, QString* error = nullptr);
    QVector<GitStashEntry> stashList(QString* error = nullptr) const;
    bool stashPop(int index, QString* error = nullptr);

    // Partial staging, the `git add -p` equivalent without the interactive
    // loop. `diffHunks` lists one path's hunks from the worktree-vs-index diff
    // (`staged` false) or the index-vs-HEAD diff (true). `applyHunks` re-reads
    // that same diff and writes only the requested hunks to the index with
    // `git apply --cached` (plus `--reverse` to take staged hunks back out),
    // so a patch that no longer matches fails atomically instead of half
    // applying. Files git cannot patch partially — added, deleted, renamed or
    // binary — are refused here with a reason.
    QVector<GitHunk> diffHunks(const QString& path, bool staged,
                               QString* error = nullptr) const;
    bool applyHunks(const QString& path, const QVector<GitHunkRef>& selected,
                    bool staged, bool reverse, QString* error = nullptr);

    // Read-only branch view: local branches with their upstream and ahead or
    // behind counts, plus the configured remotes and their tracking refs.
    // Everything comes from refs already in the repository — no network access,
    // no fetch, and no branch switching anywhere in this service.
    QVector<GitBranchInfo> branches(QString* error = nullptr) const;
    QVector<GitRemoteInfo> remotes(QString* error = nullptr) const;
    QVector<GitTagInfo> tags(QString* error = nullptr) const;

    // Per-line attribution for one tracked file at HEAD. `--line-porcelain` is
    // parsed into one record per line; a truncated or untracked file is refused
    // with a reason rather than shown half-attributed.
    QVector<GitBlameLine> blame(const QString& path, QString* error = nullptr) const;

    // Multi-branch commit graph, drawn from git's own `--graph` output. Reads
    // local refs only (`--all` includes remote-tracking refs already in the
    // repository); nothing here fetches.
    QVector<GitGraphLine> graph(int limit = 80, bool allBranches = true,
                               QString* error = nullptr) const;

    // Read-only guards used by the destructive commands above.
    bool headOnRemote(QString* branch = nullptr) const;

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
    // For commands that read a patch from stdin (`git apply`). Input is written
    // once and the channel closed, so git never waits on a terminal.
    CommandResult runWithInput(const QStringList& args,
                               const QByteArray& input) const;
    QString baseDir() const;
    QString normalizePath(const QString& root, const QString& path,
                          bool* ok) const;
    QStringList normalizePaths(const QString& root, const QStringList& paths,
                               QString* error) const;
    static bool validSha(const QString& sha);
    // Strips any user:password from a remote URL, including git's scp-style
    // `git@host:path` form that QUrl does not parse as a URL.
    static QString maskedUrl(const QString& raw);
    static void parseTrack(const QString& track, QString* state,
                           int* ahead, int* behind);
    static QVector<GitCommitInfo> parseLog(const QString& output);
    // Shared by commitFiles and compareFiles: `--name-status -z` and
    // `--numstat -z` have their own NUL quirks (renames spend two fields), so
    // the parsing lives here once rather than twice.
    static QVector<GitFileStat> parseNameStatus(const QString& output);
    static void applyNumStat(QVector<GitFileStat>& files, const QString& output);
    static QVector<GitHunk> parseHunks(const QString& diff, QString* error);
    static QVector<GitBlameLine> parseBlame(const QString& output);
    static void setError(QString* error, const QString& message);

    QString m_projectRoot;
    int m_outputLimitBytes;
    int m_timeoutMs;
};

} // namespace NexusAI
