#include "GitService.h"

#include "core/tools/Sandbox.h"

#include <QDir>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QUrl>

#include <utility>

namespace NexusAI {

namespace {

constexpr int kStderrLimitBytes = 64 * 1024;

#ifdef Q_OS_WIN
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseSensitive;
#endif

QString cleanSlashes(const QString& path) {
    return QDir::fromNativeSeparators(QDir::cleanPath(path));
}

bool isWithin(const QString& path, const QString& base) {
    const QString p = cleanSlashes(path);
    const QString b = cleanSlashes(base);
    if (b.isEmpty()) return false;
    if (p.compare(b, kPathCase) == 0) return true;
    QString prefix = b;
    if (!prefix.endsWith(QLatin1Char('/'))) prefix += QLatin1Char('/');
    return p.startsWith(prefix, kPathCase);
}

// Commands that can change the repository. Their result must be authoritative,
// so a killed process (output cap or timeout) counts as a failure. Kept in one
// place rather than at every call site: a subcommand added later still gets
// the strict rule by default. `stash list`/`stash show` are read-only.
bool isWriteCommand(const QStringList& args) {
    if (args.isEmpty()) return false;
    static const QStringList writes{
        QStringLiteral("add"), QStringLiteral("reset"),
        QStringLiteral("commit"), QStringLiteral("restore"),
        QStringLiteral("apply"), QStringLiteral("checkout"),
        QStringLiteral("switch"), QStringLiteral("init"),
        QStringLiteral("rm"), QStringLiteral("mv"),
        QStringLiteral("merge"), QStringLiteral("rebase"),
        QStringLiteral("cherry-pick"), QStringLiteral("revert"),
        QStringLiteral("clean"), QStringLiteral("fetch"),
        QStringLiteral("pull"), QStringLiteral("push"),
        QStringLiteral("update-index"), QStringLiteral("update-ref"),
        QStringLiteral("prune"), QStringLiteral("gc"),
        QStringLiteral("am")};
    if (args.first() == QLatin1String("stash")) {
        return args.size() < 2
            || (args.at(1) != QLatin1String("list")
                && args.at(1) != QLatin1String("show"));
    }
    return writes.contains(args.first());
}

} // namespace

QJsonObject GitCommitInfo::toJson() const {
    QJsonObject obj;
    obj["sha"] = sha;
    obj["shortSha"] = shortSha;
    obj["author"] = author;
    obj["authorEmail"] = authorEmail;
    obj["subject"] = subject;
    obj["body"] = body;
    obj["parents"] = parents;
    obj["timestamp"] = timestamp;
    return obj;
}

QJsonObject GitFileStat::toJson() const {
    QJsonObject obj;
    obj["status"] = status;
    obj["path"] = path;
    obj["oldPath"] = oldPath;
    obj["additions"] = additions;
    obj["removals"] = removals;
    obj["binary"] = binary;
    return obj;
}

QJsonObject GitStatusEntry::toJson() const {
    QJsonObject obj;
    obj["indexStatus"] = QString(indexStatus);
    obj["worktreeStatus"] = QString(worktreeStatus);
    obj["path"] = path;
    obj["oldPath"] = oldPath;
    return obj;
}

QJsonObject GitStashEntry::toJson() const {
    QJsonObject obj;
    obj["index"] = index;
    obj["ref"] = ref;
    obj["subject"] = subject;
    obj["timestamp"] = static_cast<double>(timestamp);
    return obj;
}

QJsonObject GitHunk::toJson() const {
    QJsonObject obj;
    obj["index"] = index;
    obj["header"] = header;
    obj["additions"] = additions;
    obj["removals"] = removals;
    obj["summary"] = summary;
    obj["contextLines"] = lines.size() - additions - removals;
    return obj;
}

QJsonObject GitBranchInfo::toJson() const {
    QJsonObject obj;
    obj["name"] = name;
    obj["sha"] = sha;
    obj["upstream"] = upstream;
    obj["track"] = track;
    obj["trackState"] = trackState;
    obj["ahead"] = ahead;
    obj["behind"] = behind;
    obj["current"] = current;
    obj["timestamp"] = static_cast<double>(timestamp);
    return obj;
}

QJsonObject GitRemoteInfo::toJson() const {
    QJsonObject obj;
    obj["name"] = name;
    obj["url"] = url;
    obj["branches"] = QJsonArray::fromStringList(branches);
    obj["branchCount"] = branches.size();
    return obj;
}

QJsonObject GitTagInfo::toJson() const {
    QJsonObject obj;
    obj["name"] = name;
    obj["sha"] = sha;
    obj["target"] = target;
    obj["annotated"] = annotated;
    obj["date"] = static_cast<double>(date);
    obj["subject"] = subject;
    return obj;
}

GitService::GitService(const QString& projectRoot, int outputLimitBytes,
                       int timeoutMs)
    : m_projectRoot(projectRoot.trimmed().isEmpty()
                        ? Sandbox::currentProjectDir()
                        : projectRoot.trimmed()),
      m_outputLimitBytes(qMax(1024, outputLimitBytes)),
      m_timeoutMs(qMax(100, timeoutMs)) {}

bool GitService::isGitAvailable(QString* error) {
    QProcess process;
    process.setProgram(QStringLiteral("git"));
    process.setArguments({QStringLiteral("--version")});
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted(3000)) {
        if (error)
            *error = QStringLiteral("Failed to start git: %1")
                         .arg(process.errorString());
        return false;
    }
    if (!process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished(1000);
        if (error) *error = QStringLiteral("git --version timed out");
        return false;
    }
    const QByteArray err = process.readAllStandardError().trimmed();
    if (process.exitCode() != 0) {
        if (error)
            *error = QString::fromUtf8(err.isEmpty()
                ? QByteArray("git --version failed") : err);
        return false;
    }
    return true;
}

GitService::CommandResult GitService::run(const QStringList& args) const {
    return runWithInput(args, QByteArray());
}

GitService::CommandResult GitService::runWithInput(const QStringList& args,
                                                   const QByteArray& input) const {
    CommandResult result;
    const bool writeOperation = isWriteCommand(args);
    QProcess process;
    process.setProgram(QStringLiteral("git"));
    // Every path this service hands to git is an exact repository path, never
    // a pattern. Literal pathspecs stop `*`, `?` and `[]` inside a user file
    // name from being reinterpreted and staging/matching a different file.
    QStringList effectiveArgs;
    effectiveArgs.reserve(args.size() + 1);
    effectiveArgs << QStringLiteral("--literal-pathspecs") << args;
    process.setArguments(effectiveArgs);
    process.setWorkingDirectory(baseDir());
    process.setProcessChannelMode(QProcess::SeparateChannels);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // Reads must not refresh the index just because the app looked at history
    // or status; writes still take the locks git needs. No prompts, no pager.
    env.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    env.insert(QStringLiteral("GIT_PAGER"), QStringLiteral("cat"));
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    process.setProcessEnvironment(env);

    process.start();
    if (!process.waitForStarted(3000)) {
        result.error = QStringLiteral("Failed to start git: %1")
                           .arg(process.errorString());
        return result;
    }
    if (!input.isEmpty()) {
        process.write(input);
        process.waitForBytesWritten(3000);
    }
    process.closeWriteChannel();

    QByteArray out;
    QByteArray err;
    QElapsedTimer timer;
    timer.start();
    const auto drain = [&]() {
        out += process.readAllStandardOutput();
        err += process.readAllStandardError();
        if (out.size() > m_outputLimitBytes) {
            out.truncate(m_outputLimitBytes);
            result.truncated = true;
        }
        if (err.size() > kStderrLimitBytes) err.truncate(kStderrLimitBytes);
    };

    while (process.state() != QProcess::NotRunning) {
        const bool ready = process.waitForReadyRead(50);
        drain();
        if (result.truncated) {
            process.kill();
            process.waitForFinished(1000);
            break;
        }
        if (timer.elapsed() > m_timeoutMs) {
            result.timedOut = true;
            process.kill();
            process.waitForFinished(1000);
            break;
        }
        if (!ready && process.state() == QProcess::NotRunning) break;
    }
    process.waitForFinished(1000);
    drain();

    result.exitCode = process.exitCode();
    // Reads may cut their output and still be useful (the caller sees the
    // `truncated` flag). Writes may not: the process was killed mid-operation,
    // so the exit code and the repository state are both unknown.
    result.ok = writeOperation
        ? (result.exitCode == 0 && !result.truncated && !result.timedOut)
        : (result.exitCode == 0 || result.truncated);
    result.output = QString::fromUtf8(out);
    const QString stderrText = QString::fromUtf8(err).trimmed();
    if (result.timedOut) {
        result.ok = false;
        result.error = QStringLiteral("git timed out after %1 ms").arg(m_timeoutMs);
    } else if (writeOperation && result.truncated) {
        result.ok = false;
        result.error = QStringLiteral(
            "git output exceeded %1 bytes; the operation was stopped and may "
            "be incomplete").arg(m_outputLimitBytes);
    } else if (!result.ok) {
        result.error = stderrText.isEmpty()
            ? QStringLiteral("git exited with code %1").arg(result.exitCode)
            : stderrText;
    }
    return result;
}

QString GitService::baseDir() const {
    return m_projectRoot.isEmpty() ? QDir::currentPath() : m_projectRoot;
}

QString GitService::repositoryRoot() const {
    if (baseDir().isEmpty() || !QFileInfo(baseDir()).isDir()) return {};
    const CommandResult r = run({QStringLiteral("rev-parse"),
                                 QStringLiteral("--show-toplevel")});
    if (!r.ok) return {};
    const QString raw = r.output.trimmed();
    if (raw.isEmpty()) return {};
    const QString canonical = QFileInfo(raw).canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? raw : canonical);
}

bool GitService::isRepository() const {
    return !repositoryRoot().isEmpty();
}

QJsonObject GitService::repoInfo() const {
    QJsonObject info;
    const QString root = repositoryRoot();
    if (root.isEmpty()) {
        info["isRepo"] = false;
        info["root"] = QString();
        info["branch"] = QString();
        info["head"] = QString();
        info["detached"] = false;
        info["dirty"] = false;
        info["error"] = QStringLiteral("Not a git repository: %1").arg(baseDir());
        return info;
    }

    info["isRepo"] = true;
    info["root"] = QDir::toNativeSeparators(root);

    const CommandResult branchResult =
        run({QStringLiteral("symbolic-ref"), QStringLiteral("--short"),
             QStringLiteral("-q"), QStringLiteral("HEAD")});
    QString branch = branchResult.ok ? branchResult.output.trimmed() : QString();
    bool detached = false;
    if (branch.isEmpty()) {
        detached = true;
        const CommandResult shortHead =
            run({QStringLiteral("rev-parse"), QStringLiteral("--short"),
                 QStringLiteral("HEAD")});
        if (shortHead.ok) branch = shortHead.output.trimmed();
    }
    info["branch"] = branch;
    info["detached"] = detached;

    const CommandResult head =
        run({QStringLiteral("rev-parse"), QStringLiteral("--verify"),
             QStringLiteral("HEAD")});
    info["head"] = head.ok ? head.output.trimmed() : QString();

    const CommandResult dirty =
        run({QStringLiteral("status"), QStringLiteral("--porcelain=v1"),
             QStringLiteral("-z")});
    info["dirty"] = dirty.ok && !dirty.output.trimmed().isEmpty();
    return info;
}

void GitService::setError(QString* error, const QString& message) {
    if (error) *error = message;
}

bool GitService::validSha(const QString& sha) {
    static const QRegularExpression re(
        QStringLiteral("^[0-9a-fA-F]{4,64}$"));
    return re.match(sha).hasMatch();
}

// Absolute paths inside the repo are accepted and converted to the
// repo-relative form git expects. Leading '-' and ':' are refused so a
// checkpoint path can never be read as an option or pathspec magic.
QString GitService::normalizePath(const QString& root, const QString& path,
                                  bool* ok) const {
    if (ok) *ok = false;
    if (path.isEmpty()) {
        if (ok) *ok = true;
        return {};
    }
    if (path.startsWith(QLatin1Char('-')) || path.startsWith(QLatin1Char(':')))
        return {};

    const QString clean = cleanSlashes(path);
    const QString rootClean = cleanSlashes(root);
    QString prefix = rootClean;
    if (!prefix.endsWith(QLatin1Char('/'))) prefix += QLatin1Char('/');

    QString abs;
    if (QDir::isAbsolutePath(clean)) {
        abs = clean;
    } else {
        if (clean == QStringLiteral("..")
                || clean.startsWith(QStringLiteral("../")))
            return {};
        abs = prefix + clean;
    }
    abs = cleanSlashes(abs);
    // 解析结果正好等于仓库根 = 调用方指的是"整个仓库"（模型传 "."、
    // "./"、"<root>"、甚至 "<root>/." 都会被 cleanPath 折叠成 root）。
    // 这在语义上等价于 path 为空，必须放行；此前 abs.size() <= prefix.size()
    // 把它误判成越界，报 "Path escapes the repository: ."。
    if (abs.compare(rootClean, kPathCase) == 0) {
        if (ok) *ok = true;
        return {};
    }
    if (!abs.startsWith(prefix, kPathCase) || abs.size() <= prefix.size())
        return {};
    if (!isWithin(abs, rootClean)) return {};
    const QString rel = abs.mid(prefix.size());
    if (rel.isEmpty() || rel == QStringLiteral(".")
            || rel.startsWith(QStringLiteral("../")))
        return {};
    if (ok) *ok = true;
    return rel;
}

QStringList GitService::normalizePaths(const QString& root,
                                       const QStringList& paths,
                                       QString* error) const {
    setError(error, QString());
    QStringList relative;
    for (const QString& path : paths) {
        bool ok = false;
        const QString rel = normalizePath(root, path, &ok);
        if (!ok) {
            setError(error, QStringLiteral("Path escapes the repository: %1")
                                .arg(path));
            return {};
        }
        if (!rel.isEmpty() && !relative.contains(rel)) relative.append(rel);
    }
    if (relative.isEmpty()) {
        setError(error, QStringLiteral("A repository path is required"));
        return {};
    }
    return relative;
}

QVector<GitCommitInfo> GitService::parseLog(const QString& output) {
    QVector<GitCommitInfo> commits;
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const QStringList fields = line.split(QLatin1Char('\0'), Qt::KeepEmptyParts);
        if (fields.size() < 7) continue;   // partial trailing line after truncation
        GitCommitInfo info;
        info.sha = fields.at(0);
        info.shortSha = fields.at(1);
        info.author = fields.at(2);
        info.authorEmail = fields.at(3);
        info.timestamp = fields.at(4).toLongLong();
        info.parents = fields.at(5);
        info.subject = fields.mid(6).join(QLatin1Char('\0'));
        if (!info.sha.isEmpty()) commits.append(info);
    }
    return commits;
}

QVector<GitCommitInfo> GitService::log(int limit, int offset,
                                       const QString& path,
                                       QString* error) const {
    GitLogQuery query;
    query.limit = limit;
    query.offset = offset;
    query.path = path;
    return log(query, error);
}

QVector<GitCommitInfo> GitService::log(const GitLogQuery& query,
                                       QString* error) const {
    setError(error, QString());
    const QString root = repositoryRoot();
    if (root.isEmpty()) {
        setError(error, QStringLiteral("Not a git repository: %1").arg(baseDir()));
        return {};
    }
    bool pathOk = true;
    const QString rel = query.path.isEmpty()
        ? QString() : normalizePath(root, query.path, &pathOk);
    if (!pathOk) {
        setError(error, QStringLiteral("Path escapes the repository: %1").arg(query.path));
        return {};
    }

    const int count = qBound(1, query.limit, 500);
    const int skip = qMax(0, query.offset);
    QStringList args{
        QStringLiteral("log"),
        QStringLiteral("--no-color"),
        QStringLiteral("--date=unix"),
        QStringLiteral("--max-count=%1").arg(count),
        QStringLiteral("--skip=%1").arg(skip),
        QStringLiteral("--pretty=format:%H%x00%h%x00%an%x00%ae%x00%at%x00%P%x00%s")};
    // Fixed strings, case-insensitively: a message search must not die on a
    // bracket the user typed, and must not turn into a regex lesson. Both kinds
    // of limit must match, which is git's default across different limit types.
    // (git log --fixed-strings needs git 2.28; older git reports the failure.)
    if (!query.grep.isEmpty() || !query.author.isEmpty()) {
        args << QStringLiteral("-F") << QStringLiteral("-i");
        if (!query.grep.isEmpty())
            args << QStringLiteral("--grep=%1").arg(query.grep);
        if (!query.author.isEmpty())
            args << QStringLiteral("--author=%1").arg(query.author);
    }
    if (!rel.isEmpty()) args << QStringLiteral("--") << rel;

    const CommandResult r = run(args);
    if (!r.ok) {
        if (r.error.contains(QStringLiteral("does not have any commits")))
            return {};
        setError(error, r.error);
        return {};
    }
    return parseLog(r.output);
}

QVector<GitCommitInfo> GitService::fileHistory(const QString& path, int limit,
                                               QString* error) const {
    setError(error, QString());
    if (path.isEmpty()) {
        setError(error, QStringLiteral("A path is required"));
        return {};
    }
    const QString root = repositoryRoot();
    if (root.isEmpty()) {
        setError(error, QStringLiteral("Not a git repository: %1").arg(baseDir()));
        return {};
    }
    bool pathOk = true;
    const QString rel = normalizePath(root, path, &pathOk);
    if (!pathOk) {
        setError(error, QStringLiteral("Path escapes the repository: %1").arg(path));
        return {};
    }

    const int count = qBound(1, limit, 500);
    const QStringList args{
        QStringLiteral("log"),
        QStringLiteral("--no-color"),
        QStringLiteral("--date=unix"),
        QStringLiteral("--follow"),
        QStringLiteral("--max-count=%1").arg(count),
        QStringLiteral("--pretty=format:%H%x00%h%x00%an%x00%ae%x00%at%x00%P%x00%s"),
        QStringLiteral("--"),
        rel};

    const CommandResult r = run(args);
    if (!r.ok) {
        if (r.error.contains(QStringLiteral("does not have any commits")))
            return {};
        setError(error, r.error);
        return {};
    }
    return parseLog(r.output);
}

GitCommitInfo GitService::commit(const QString& sha, QString* error) const {
    setError(error, QString());
    if (!validSha(sha)) {
        setError(error, QStringLiteral("Invalid commit id: %1").arg(sha));
        return {};
    }
    if (repositoryRoot().isEmpty()) {
        setError(error, QStringLiteral("Not a git repository: %1").arg(baseDir()));
        return {};
    }

    const QStringList args{
        QStringLiteral("show"),
        QStringLiteral("-s"),
        QStringLiteral("--no-color"),
        QStringLiteral("--date=unix"),
        QStringLiteral("--pretty=format:%H%x00%h%x00%an%x00%ae%x00%at%x00%P%x00%s%x00%b"),
        sha};
    const CommandResult r = run(args);
    if (!r.ok) {
        setError(error, r.error);
        return {};
    }

    // Six fixed fields, then subject, then the (possibly multi-line) body.
    QStringList fields;
    int pos = 0;
    for (int i = 0; i < 6; ++i) {
        const int sep = r.output.indexOf(QLatin1Char('\0'), pos);
        if (sep < 0) {
            setError(error, QStringLiteral("Unparseable git show output"));
            return {};
        }
        fields << r.output.mid(pos, sep - pos);
        pos = sep + 1;
    }
    const int subjectSep = r.output.indexOf(QLatin1Char('\0'), pos);
    if (subjectSep < 0) {
        setError(error, QStringLiteral("Unparseable git show output"));
        return {};
    }

    GitCommitInfo info;
    info.sha = fields.at(0);
    info.shortSha = fields.at(1);
    info.author = fields.at(2);
    info.authorEmail = fields.at(3);
    info.timestamp = fields.at(4).toLongLong();
    info.parents = fields.at(5);
    info.subject = r.output.mid(pos, subjectSep - pos);
    info.body = r.output.mid(subjectSep + 1);
    while (info.body.endsWith(QLatin1Char('\n')))
        info.body.chop(1);
    return info;
}

QVector<GitFileStat> GitService::parseNameStatus(const QString& output) {
    QVector<GitFileStat> files;
    const QStringList tokens = output.split(QLatin1Char('\0'), Qt::SkipEmptyParts);
    for (int i = 0; i < tokens.size();) {
        const QString status = tokens.at(i++);
        if (status.isEmpty()) continue;
        GitFileStat stat;
        stat.status = status;
        const QChar kind = status.at(0);
        if (kind == QLatin1Char('R') || kind == QLatin1Char('C')) {
            if (i + 1 >= tokens.size()) break;
            stat.oldPath = tokens.at(i++);
            stat.path = tokens.at(i++);
        } else {
            if (i >= tokens.size()) break;
            stat.path = tokens.at(i++);
        }
        if (!stat.path.isEmpty()) files.append(stat);
    }
    return files;
}

void GitService::applyNumStat(QVector<GitFileStat>& files,
                              const QString& output) {
    struct Counts { int additions; int removals; bool binary; };
    QHash<QString, Counts> counts;
    const QStringList numTokens = output.split(QLatin1Char('\0'), Qt::SkipEmptyParts);
    for (int i = 0; i < numTokens.size();) {
        const QString token = numTokens.at(i++);
        const int tab1 = token.indexOf(QLatin1Char('\t'));
        if (tab1 < 0) continue;
        const int tab2 = token.indexOf(QLatin1Char('\t'), tab1 + 1);
        if (tab2 < 0) continue;
        const QString adds = token.mid(0, tab1);
        const QString dels = token.mid(tab1 + 1, tab2 - tab1 - 1);
        QString key = token.mid(tab2 + 1);
        if (key.isEmpty() && i + 1 < numTokens.size()) {
            ++i;                     // old path
            key = numTokens.at(i++); // new path
        }
        const bool binary = adds == QStringLiteral("-")
                         || dels == QStringLiteral("-");
        counts.insert(key, {binary ? 0 : adds.toInt(),
                            binary ? 0 : dels.toInt(), binary});
    }
    for (GitFileStat& file : files) {
        const auto it = counts.constFind(file.path);
        if (it != counts.constEnd()) {
            file.additions = it->additions;
            file.removals = it->removals;
            file.binary = it->binary;
        }
    }
}

QVector<GitFileStat> GitService::commitFiles(const QString& sha,
                                             QString* error) const {
    setError(error, QString());
    if (!validSha(sha)) {
        setError(error, QStringLiteral("Invalid commit id: %1").arg(sha));
        return {};
    }
    if (repositoryRoot().isEmpty()) {
        setError(error, QStringLiteral("Not a git repository: %1").arg(baseDir()));
        return {};
    }

    const CommandResult names = run(
        {QStringLiteral("show"), QStringLiteral("--format="),
         QStringLiteral("--name-status"), QStringLiteral("-z"),
         QStringLiteral("--find-renames"), QStringLiteral("-m"),
         QStringLiteral("--first-parent"), sha});
    if (!names.ok) {
        setError(error, names.error);
        return {};
    }
    QVector<GitFileStat> files = parseNameStatus(names.output);

    // Line stats are a second pass: --numstat -z uses two extra NUL fields
    // for renames, which is easier to parse on its own than merged with
    // name-status output.
    const CommandResult nums = run(
        {QStringLiteral("show"), QStringLiteral("--format="),
         QStringLiteral("--numstat"), QStringLiteral("-z"),
         QStringLiteral("--find-renames"), QStringLiteral("-m"),
         QStringLiteral("--first-parent"), sha});
    if (nums.ok)
        applyNumStat(files, nums.output);
    return files;
}

QVector<GitFileStat> GitService::compareFiles(const QString& shaA,
                                              const QString& shaB,
                                              QString* error) const {
    setError(error, QString());
    if (!validSha(shaA) || !validSha(shaB)) {
        setError(error, QStringLiteral("Invalid commit id"));
        return {};
    }
    if (repositoryRoot().isEmpty()) {
        setError(error, QStringLiteral("Not a git repository: %1").arg(baseDir()));
        return {};
    }

    const CommandResult names = run(
        {QStringLiteral("diff"), QStringLiteral("--name-status"),
         QStringLiteral("-z"), QStringLiteral("--find-renames"),
         shaA, shaB});
    if (!names.ok) {
        setError(error, names.error);
        return {};
    }
    QVector<GitFileStat> files = parseNameStatus(names.output);

    const CommandResult nums = run(
        {QStringLiteral("diff"), QStringLiteral("--numstat"),
         QStringLiteral("-z"), QStringLiteral("--find-renames"),
         shaA, shaB});
    if (nums.ok)
        applyNumStat(files, nums.output);
    return files;
}

QJsonObject GitBlameLine::toJson() const {
    QJsonObject obj;
    obj["lineNo"] = lineNo;
    obj["sha"] = sha;
    obj["shortSha"] = shortSha;
    obj["author"] = author;
    obj["timestamp"] = static_cast<double>(timestamp);
    obj["summary"] = summary;
    obj["text"] = text;
    obj["uncommitted"] = uncommitted();
    return obj;
}

QVector<GitBlameLine> GitService::parseBlame(const QString& output) {
    QVector<GitBlameLine> lines;
    // Measured shape of `git blame --line-porcelain`: a header line
    // "<sha> <origLine> <finalLine> [<count>]", then key/value metadata, then a
    // tab-prefixed content line that closes the record.
    static const QRegularExpression headerRe(
        QStringLiteral("^([0-9a-f]{40}) (\\d+) (\\d+)(?: (\\d+))?$"));
    GitBlameLine current;
    bool inRecord = false;
    const QStringList rows = output.split(QLatin1Char('\n'));
    for (const QString& row : rows) {
        const QRegularExpressionMatch match = headerRe.match(row);
        if (match.hasMatch()) {
            if (inRecord && current.lineNo > 0) lines.append(current);
            current = GitBlameLine();
            current.sha = match.captured(1);
            current.shortSha = current.sha.left(7);
            current.lineNo = match.captured(3).toInt();
            inRecord = true;
            continue;
        }
        if (!inRecord) continue;
        if (row.startsWith(QStringLiteral("author "))) {
            current.author = row.mid(7).trimmed();
        } else if (row.startsWith(QStringLiteral("author-time "))) {
            current.timestamp = row.mid(12).trimmed().toLongLong();
        } else if (row.startsWith(QStringLiteral("summary "))) {
            current.summary = row.mid(8).trimmed();
        } else if (row.startsWith(QLatin1Char('\t'))) {
            current.text = row.mid(1);
            lines.append(current);
            current = GitBlameLine();
            inRecord = false;
        }
    }
    return lines;
}

QVector<GitBlameLine> GitService::blame(const QString& path,
                                        QString* error) const {
    setError(error, QString());
    const QString root = repositoryRoot();
    if (root.isEmpty()) {
        setError(error, QStringLiteral("Not a git repository: %1").arg(baseDir()));
        return {};
    }
    bool ok = false;
    const QString rel = normalizePath(root, path, &ok);
    if (!ok) {
        setError(error, QStringLiteral("Path is outside the repository: %1").arg(path));
        return {};
    }

    const CommandResult r = run({QStringLiteral("blame"),
                                 QStringLiteral("--line-porcelain"),
                                 QStringLiteral("--"), rel});
    if (!r.ok) {
        setError(error, r.error.isEmpty()
               ? QStringLiteral("git blame failed for %1").arg(rel) : r.error);
        return {};
    }
    // Half a file is worse than no file: the remaining lines would be attributed
    // to whoever touched them last, silently missing the rest.
    if (r.truncated) {
        setError(error, QStringLiteral(
                "This file is too large to blame line by line"));
        return {};
    }
    const QVector<GitBlameLine> lines = parseBlame(r.output);
    if (lines.isEmpty()) {
        setError(error, QStringLiteral(
                "%1 is not tracked yet, so it has no blame").arg(rel));
    }
    return lines;
}

QJsonObject GitGraphLine::toJson() const {
    QJsonObject obj;
    obj["index"] = index;
    obj["art"] = art;
    obj["sha"] = sha;
    obj["date"] = date;
    obj["refs"] = refs;
    obj["subject"] = subject;
    obj["hasCommit"] = hasCommit;
    return obj;
}

QVector<GitGraphLine> GitService::graph(int limit, bool allBranches,
                                       QString* error) const {
    setError(error, QString());
    QVector<GitGraphLine> out;
    if (repositoryRoot().isEmpty()) {
        setError(error, QStringLiteral("Not a git repository: %1").arg(baseDir()));
        return out;
    }

    // A NUL that git emits itself marks "the graph art ends here", so the
    // punctuation prefix never has to be guessed from the text.
    const int count = qBound(1, limit, 300);
    QStringList args{QStringLiteral("log"), QStringLiteral("--graph"),
                     QStringLiteral("--date=short"),
                     QStringLiteral("--pretty=format:%x00%h%x00%ad%x00%D%x00%s"),
                     QStringLiteral("-n"), QString::number(count)};
    if (allBranches) args << QStringLiteral("--all");
    const CommandResult r = run(args);
    if (!r.ok) {
        if (r.error.contains(QStringLiteral("does not have any commits")))
            return out;
        setError(error, r.error);
        return {};
    }
    if (r.truncated) {
        setError(error, QStringLiteral(
                "The graph is too large to render; narrow it down and retry"));
        return {};
    }

    const QStringList rows = r.output.split(QLatin1Char('\n'));
    for (const QString& row : rows) {
        GitGraphLine line;
        line.index = out.size();
        const int mark = row.indexOf(QLatin1Char('\0'));
        if (mark < 0) {
            line.art = row;               // pure graph art ("|\\", "|/")
            out.push_back(line);
            continue;
        }
        line.art = row.left(mark);
        const QStringList fields =
            row.mid(mark + 1).split(QLatin1Char('\0'), Qt::KeepEmptyParts);
        if (fields.size() >= 4) {
            line.sha = fields.at(0).trimmed();
            line.date = fields.at(1).trimmed();
            line.refs = fields.at(2).trimmed();
            line.subject = fields.mid(3).join(QLatin1Char('\0'));
            line.hasCommit = !line.sha.isEmpty();
        }
        out.push_back(line);
    }
    return out;
}

QVector<GitTagInfo> GitService::tags(QString* error) const {
    setError(error, QString());
    QVector<GitTagInfo> out;
    if (repositoryRoot().isEmpty()) {
        setError(error, QStringLiteral("Not a git repository: %1").arg(baseDir()));
        return out;
    }

    // Measured field behaviour: `objecttype` is "tag" only for annotated tags,
    // `*objectname` (peeled) is filled for annotated and empty for lightweight,
    // and `creatordate` is the tagger date or the commit date accordingly - so
    // one pass answers both kinds. Both the full and the short commit id are
    // taken: the full one compares against log rows, the short one is display.
    const QString sep = QString::fromLatin1("\x1f");
    const QString format = QStringLiteral("--format=%(refname:short)")
            + sep + QStringLiteral("%(objecttype)")
            + sep + QStringLiteral("%(objectname)")
            + sep + QStringLiteral("%(*objectname)")
            + sep + QStringLiteral("%(creatordate:unix)")
            + sep + QStringLiteral("%(contents:subject)") + sep;
    const CommandResult r = run({QStringLiteral("for-each-ref"), format,
                                 QStringLiteral("refs/tags")});
    if (!r.ok) {
        setError(error, r.error);
        return out;
    }

    const QStringList lines = r.output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const QStringList fields = line.split(sep, Qt::KeepEmptyParts);
        if (fields.size() < 6) continue;
        GitTagInfo info;
        info.name = fields.at(0).trimmed();
        info.annotated = fields.at(1).trimmed() == QLatin1String("tag");
        const QString direct = fields.at(2).trimmed();
        const QString peeled = fields.at(3).trimmed();
        info.sha = peeled.isEmpty() ? direct : peeled;
        info.target = info.sha.left(7);
        info.date = fields.at(4).trimmed().toLongLong();
        info.subject = fields.at(5).trimmed();
        if (info.isValid())
            out.push_back(info);
    }
    return out;
}

QString GitService::compareDiff(const QString& shaA, const QString& shaB,
                                const QString& path, bool* truncated,
                                QString* error) const {
    setError(error, QString());
    if (truncated) *truncated = false;
    if (!validSha(shaA) || !validSha(shaB)) {
        setError(error, QStringLiteral("Invalid commit id"));
        return {};
    }
    const QString root = repositoryRoot();
    if (root.isEmpty()) {
        setError(error, QStringLiteral("Not a git repository: %1").arg(baseDir()));
        return {};
    }
    bool pathOk = true;
    const QString rel = path.isEmpty()
        ? QString() : normalizePath(root, path, &pathOk);
    if (!pathOk) {
        setError(error, QStringLiteral("Path escapes the repository: %1").arg(path));
        return {};
    }

    QStringList args{QStringLiteral("diff"), QStringLiteral("--no-color"),
                     QStringLiteral("--no-ext-diff"), QStringLiteral("--unified=3"),
                     shaA, shaB};
    if (!rel.isEmpty()) args << QStringLiteral("--") << rel;
    const CommandResult r = run(args);
    if (!r.ok) {
        setError(error, r.error);
        return {};
    }
    if (truncated) *truncated = r.truncated;
    return r.output;
}

QString GitService::commitDiff(const QString& sha, const QString& path,
                               bool* truncated, QString* error) const {
    setError(error, QString());
    if (truncated) *truncated = false;
    if (!validSha(sha)) {
        setError(error, QStringLiteral("Invalid commit id: %1").arg(sha));
        return {};
    }
    const QString root = repositoryRoot();
    if (root.isEmpty()) {
        setError(error, QStringLiteral("Not a git repository: %1").arg(baseDir()));
        return {};
    }
    bool pathOk = true;
    const QString rel = path.isEmpty()
        ? QString() : normalizePath(root, path, &pathOk);
    if (!pathOk) {
        setError(error, QStringLiteral("Path escapes the repository: %1").arg(path));
        return {};
    }

    QStringList args{
        QStringLiteral("show"),
        QStringLiteral("--format="),
        QStringLiteral("--no-color"),
        QStringLiteral("--no-ext-diff"),
        QStringLiteral("--no-textconv"),
        QStringLiteral("--unified=3"),
        QStringLiteral("-m"),
        QStringLiteral("--first-parent"),
        sha};
    if (!rel.isEmpty()) args << QStringLiteral("--") << rel;

    const CommandResult r = run(args);
    if (!r.ok) {
        setError(error, r.error);
        return {};
    }
    if (truncated) *truncated = r.truncated;
    QString diff = r.output;
    if (diff.isNull()) diff = QStringLiteral("");
    return diff;
}

QVector<GitStatusEntry> GitService::status(const QString& path,
                                           QString* error) const {
    setError(error, QString());
    const QString root = repositoryRoot();
    if (root.isEmpty()) {
        setError(error, QStringLiteral("Not a git repository: %1").arg(baseDir()));
        return {};
    }
    bool pathOk = true;
    const QString rel = path.isEmpty()
        ? QString() : normalizePath(root, path, &pathOk);
    if (!pathOk) {
        setError(error, QStringLiteral("Path escapes the repository: %1").arg(path));
        return {};
    }

    QStringList args{
        QStringLiteral("status"), QStringLiteral("--porcelain=v1"),
        QStringLiteral("-z"), QStringLiteral("--untracked-files=all"),
        QStringLiteral("--renames")};
    if (!rel.isEmpty()) args << QStringLiteral("--") << rel;
    const CommandResult r = run(args);
    if (!r.ok) {
        setError(error, r.error);
        return {};
    }

    QVector<GitStatusEntry> entries;
    const QStringList tokens =
        r.output.split(QLatin1Char('\0'), Qt::SkipEmptyParts);
    for (int i = 0; i < tokens.size();) {
        const QString token = tokens.at(i++);
        if (token.size() < 4) continue;
        GitStatusEntry entry;
        entry.indexStatus = token.at(0);
        entry.worktreeStatus = token.at(1);
        entry.path = token.mid(3);
        const bool renamed = entry.indexStatus == QLatin1Char('R')
                          || entry.indexStatus == QLatin1Char('C')
                          || entry.worktreeStatus == QLatin1Char('R')
                          || entry.worktreeStatus == QLatin1Char('C');
        if (renamed && i < tokens.size())
            entry.oldPath = tokens.at(i++);
        if (!entry.path.isEmpty()) entries.append(entry);
    }
    return entries;
}

QString GitService::statusText(const QString& path, QString* error) const {
    setError(error, QString());
    const QString root = repositoryRoot();
    if (root.isEmpty()) {
        setError(error, QStringLiteral("Not a git repository: %1").arg(baseDir()));
        return {};
    }
    bool pathOk = true;
    const QString rel = path.isEmpty()
        ? QString() : normalizePath(root, path, &pathOk);
    if (!pathOk) {
        setError(error, QStringLiteral("Path escapes the repository: %1").arg(path));
        return {};
    }

    QStringList args{QStringLiteral("status"), QStringLiteral("--porcelain=v1"),
                     QStringLiteral("-b")};
    if (!rel.isEmpty()) args << QStringLiteral("--") << rel;
    const CommandResult r = run(args);
    if (!r.ok) {
        setError(error, r.error);
        return {};
    }
    QString output = r.output;
    if (output.isNull()) output = QStringLiteral("");
    return output;
}

QString GitService::workingDiff(const QString& path, bool staged,
                                bool* truncated, QString* error) const {
    setError(error, QString());
    if (truncated) *truncated = false;
    const QString root = repositoryRoot();
    if (root.isEmpty()) {
        setError(error, QStringLiteral("Not a git repository: %1").arg(baseDir()));
        return {};
    }
    bool pathOk = true;
    const QString rel = path.isEmpty()
        ? QString() : normalizePath(root, path, &pathOk);
    if (!pathOk) {
        setError(error, QStringLiteral("Path escapes the repository: %1").arg(path));
        return {};
    }

    QStringList args{
        QStringLiteral("diff"),
        QStringLiteral("--no-color"),
        QStringLiteral("--no-ext-diff"),
        QStringLiteral("--no-textconv"),
        QStringLiteral("--unified=3")};
    if (staged) args << QStringLiteral("--staged");
    if (!rel.isEmpty()) args << QStringLiteral("--") << rel;

    const CommandResult r = run(args);
    if (!r.ok) {
        setError(error, r.error);
        return {};
    }
    if (truncated) *truncated = r.truncated;
    QString diff = r.output;
    if (diff.isNull()) diff = QStringLiteral("");
    if (diff.isEmpty() && !staged && !rel.isEmpty()) {
        // `git diff` ignores untracked files. Show their content as an
        // all-additions comparison so new files can be reviewed before the
        // first `git add`.
        const CommandResult tracked =
            run({QStringLiteral("ls-files"), QStringLiteral("--error-unmatch"),
                 QStringLiteral("--"), rel});
        if (!tracked.ok) {
            const QString absPath = QDir(root).filePath(rel);
            const QFileInfo info(absPath);
            if (info.isFile()) {
                QFile file(absPath);
                if (!file.open(QIODevice::ReadOnly)) {
                    setError(error, QStringLiteral("Cannot read %1: %2")
                                        .arg(path, file.errorString()));
                    return {};
                }
                const qint64 limit = static_cast<qint64>(m_outputLimitBytes);
                const bool overLimit = info.size() > limit;
                const QByteArray bytes = file.read(limit);
                if (bytes.contains('\0')) {
                    setError(error, QStringLiteral("Binary file: %1").arg(path));
                    return {};
                }
                QString text = QString::fromUtf8(bytes);
                if (overLimit) {
                    if (truncated) *truncated = true;
                } else if (text.endsWith(QLatin1Char('\n'))) {
                    text.chop(1);
                }
                if (!text.isEmpty()) {
                    const QStringList lines =
                        text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
                    diff = QStringLiteral(
                        "--- /dev/null\n+++ b/%1\n@@ -0,0 +1,%2 @@\n")
                        .arg(rel).arg(lines.size());
                    for (const QString& line : std::as_const(lines))
                        diff += QLatin1Char('+') + line + QLatin1Char('\n');
                }
            }
        }
    }
    return diff;
}

bool GitService::stage(const QStringList& paths, QString* error) {
    setError(error, QString());
    const QString root = repositoryRoot();
    if (root.isEmpty()) {
        setError(error, QStringLiteral("Not a git repository: %1").arg(baseDir()));
        return false;
    }
    const QStringList rels = normalizePaths(root, paths, error);
    if (rels.isEmpty()) return false;

    QStringList args{QStringLiteral("add"), QStringLiteral("--")};
    args << rels;
    const CommandResult r = run(args);
    if (!r.ok) {
        setError(error, r.error);
        return false;
    }
    return true;
}

bool GitService::unstage(const QStringList& paths, QString* error) {
    setError(error, QString());
    const QString root = repositoryRoot();
    if (root.isEmpty()) {
        setError(error, QStringLiteral("Not a git repository: %1").arg(baseDir()));
        return false;
    }
    const QStringList rels = normalizePaths(root, paths, error);
    if (rels.isEmpty()) return false;

    // `reset HEAD -- <paths>` also works on an unborn branch (no commits yet),
    // where `restore --staged` fails to resolve HEAD.
    QStringList args{QStringLiteral("reset"), QStringLiteral("-q"),
                     QStringLiteral("HEAD"), QStringLiteral("--")};
    args << rels;
    const CommandResult r = run(args);
    if (!r.ok) {
        setError(error, r.error);
        return false;
    }
    return true;
}

bool GitService::commitStaged(const QString& message, QString* error) {
    setError(error, QString());
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty()) {
        setError(error, QStringLiteral("Commit message is required"));
        return false;
    }
    const QString root = repositoryRoot();
    if (root.isEmpty()) {
        setError(error, QStringLiteral("Not a git repository: %1").arg(baseDir()));
        return false;
    }

    const CommandResult staged = run(
        {QStringLiteral("diff"), QStringLiteral("--cached"),
         QStringLiteral("--name-only"), QStringLiteral("--")});
    if (!staged.ok) {
        setError(error, staged.error);
        return false;
    }
    if (staged.output.trimmed().isEmpty()) {
        setError(error, QStringLiteral("Nothing is staged to commit"));
        return false;
    }

    // No --no-verify: repository hooks are part of the project's workflow.
    const CommandResult r =
        run({QStringLiteral("commit"), QStringLiteral("-m"), trimmed});
    if (!r.ok) {
        setError(error, r.error);
        return false;
    }
    return true;
}

bool GitService::headOnRemote(QString* branch) const {
    if (branch) branch->clear();
    if (repositoryRoot().isEmpty()) return false;

    const CommandResult r = run({QStringLiteral("branch"),
                                 QStringLiteral("--remotes"),
                                 QStringLiteral("--contains"),
                                 QStringLiteral("HEAD")});
    if (!r.ok) return false;
    const QString text = r.output.trimmed();
    if (text.isEmpty()) return false;

    if (branch != nullptr) {
        const QStringList lines =
            text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        // `git branch -r` indents each line; the first usable one is enough to
        // explain which remote branch already holds the commit.
        for (const QString& line : lines) {
            const QString name = line.trimmed();
            if (!name.isEmpty()) {
                *branch = name;
                break;
            }
        }
        if (branch->isEmpty()) *branch = QStringLiteral("a remote branch");
    }
    return true;
}

bool GitService::amendLast(const QString& message, QString* error) {
    setError(error, QString());
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty()) {
        setError(error, QStringLiteral("Commit message is required"));
        return false;
    }
    if (repositoryRoot().isEmpty()) {
        setError(error, QStringLiteral("Not a git repository: %1").arg(baseDir()));
        return false;
    }

    const CommandResult head = run({QStringLiteral("rev-parse"),
                                    QStringLiteral("--verify"),
                                    QStringLiteral("--quiet"),
                                    QStringLiteral("HEAD")});
    if (!head.ok) {
        setError(error, QStringLiteral("There is no commit to amend"));
        return false;
    }

    QString remoteBranch;
    if (headOnRemote(&remoteBranch)) {
        setError(error, QStringLiteral(
                "The last commit is already published on %1; rewriting it here "
                "would diverge from the remote").arg(remoteBranch));
        return false;
    }

    // Amends HEAD with the current index, hooks included.
    const CommandResult r =
        run({QStringLiteral("commit"), QStringLiteral("--amend"),
             QStringLiteral("-m"), trimmed});
    if (!r.ok) {
        setError(error, r.error);
        return false;
    }
    return true;
}

bool GitService::undoLastCommit(QString* error) {
    setError(error, QString());
    if (repositoryRoot().isEmpty()) {
        setError(error, QStringLiteral("Not a git repository: %1").arg(baseDir()));
        return false;
    }

    const CommandResult head = run({QStringLiteral("rev-parse"),
                                    QStringLiteral("--verify"),
                                    QStringLiteral("--quiet"),
                                    QStringLiteral("HEAD")});
    if (!head.ok) {
        setError(error, QStringLiteral("There is no commit to undo"));
        return false;
    }

    QString remoteBranch;
    if (headOnRemote(&remoteBranch)) {
        setError(error, QStringLiteral(
                "The last commit is already published on %1; rewriting it here "
                "would diverge from the remote").arg(remoteBranch));
        return false;
    }

    // "<sha> <parent>...": one token means the root commit, three or more a
    // merge. Neither has a `HEAD^` undo whose meaning we can promise, so both
    // are refused.
    const CommandResult parents =
        run({QStringLiteral("rev-list"), QStringLiteral("--parents"),
             QStringLiteral("-n"), QStringLiteral("1"),
             QStringLiteral("HEAD")});
    if (!parents.ok) {
        setError(error, parents.error);
        return false;
    }
    const QStringList tokens =
        parents.output.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (tokens.size() < 2) {
        setError(error, QStringLiteral(
                "This is the repository's first commit; undoing it would leave "
                "the branch unborn"));
        return false;
    }
    if (tokens.size() > 2) {
        setError(error, QStringLiteral(
                "The last commit is a merge; undoing it here is not supported"));
        return false;
    }

    // --soft keeps both the index and the worktree, so the undone changes stay
    // staged instead of disappearing.
    const CommandResult r = run({QStringLiteral("reset"),
                                 QStringLiteral("--soft"),
                                 QStringLiteral("HEAD^")});
    if (!r.ok) {
        setError(error, r.error);
        return false;
    }
    return true;
}

bool GitService::stashPush(const QString& message, QString* error) {
    setError(error, QString());
    if (repositoryRoot().isEmpty()) {
        setError(error, QStringLiteral("Not a git repository: %1").arg(baseDir()));
        return false;
    }

    // `git stash push` exits 0 with "No local changes to save" and creates
    // nothing, so the tracked-changes check happens here and is reported.
    const CommandResult worktree = run({QStringLiteral("diff"),
                                         QStringLiteral("--quiet")});
    const CommandResult index = run({QStringLiteral("diff"),
                                     QStringLiteral("--cached"),
                                     QStringLiteral("--quiet")});
    if (worktree.ok && index.ok) {
        setError(error, QStringLiteral(
                "There are no tracked changes to stash"));
        return false;
    }

    QStringList args{QStringLiteral("stash"), QStringLiteral("push")};
    const QString trimmed = message.trimmed();
    if (!trimmed.isEmpty())
        args << QStringLiteral("-m") << trimmed;
    const CommandResult r = run(args);
    if (!r.ok) {
        setError(error, r.error);
        return false;
    }
    return true;
}

QVector<GitStashEntry> GitService::stashList(QString* error) const {
    setError(error, QString());
    QVector<GitStashEntry> entries;
    if (repositoryRoot().isEmpty()) {
        setError(error, QStringLiteral("Not a git repository: %1").arg(baseDir()));
        return entries;
    }

    const CommandResult r = run({QStringLiteral("stash"),
                                 QStringLiteral("list"),
                                 QStringLiteral("--format=%gd\x1f%s\x1f%ct")});
    if (!r.ok) {
        setError(error, r.error);
        return entries;
    }

    const QStringList lines =
        r.output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (int i = 0; i < lines.size(); ++i) {
        const QStringList fields =
            lines.at(i).trimmed().split(QLatin1Char('\x1f'), Qt::KeepEmptyParts);
        if (fields.size() < 2) continue;
        GitStashEntry entry;
        entry.index = i;
        entry.ref = fields.at(0).trimmed();
        entry.subject = fields.at(1).trimmed();
        entry.timestamp = fields.size() > 2 ? fields.at(2).trimmed().toLongLong() : 0;
        entries.push_back(entry);
    }
    return entries;
}

bool GitService::stashPop(int index, QString* error) {
    setError(error, QString());
    if (index < 0) {
        setError(error, QStringLiteral("Stash index must not be negative"));
        return false;
    }
    QString listError;
    const QVector<GitStashEntry> entries = stashList(&listError);
    if (!listError.isEmpty()) {
        setError(error, listError);
        return false;
    }
    if (index >= entries.size()) {
        setError(error, QStringLiteral(
                "There is no stash at index %1").arg(index));
        return false;
    }

    // Passed as one argument, so the braces never reach a shell.
    const CommandResult r = run({QStringLiteral("stash"),
                                 QStringLiteral("pop"),
                                 QStringLiteral("stash@{%1}").arg(index)});
    if (!r.ok) {
        setError(error, r.error);
        return false;
    }
    return true;
}

bool GitService::discardPath(const QString& path, const QString& backupRoot,
                             QString* backupPath, QString* error) {
    if (backupPath) backupPath->clear();
    setError(error, QString());

    const QString root = repositoryRoot();
    if (root.isEmpty()) {
        setError(error, QStringLiteral("Not a git repository: %1").arg(baseDir()));
        return false;
    }
    bool ok = false;
    const QString rel = normalizePath(root, path, &ok);
    if (!ok) {
        setError(error, QStringLiteral("Path is outside the repository: %1").arg(path));
        return false;
    }

    if (run({QStringLiteral("ls-files"), QStringLiteral("--error-unmatch"),
             QStringLiteral("--"), rel}).ok) {
        // Restore from the index: staged content is deliberately kept, and a
        // file deleted in the worktree comes back.
        const CommandResult r = run({QStringLiteral("restore"),
                                     QStringLiteral("--"), rel});
        if (!r.ok) {
            setError(error, r.error);
            return false;
        }
        return true;
    }

    // Untracked: git has no copy of this content, so it is backed up first and
    // the backup location is handed back to the caller to display.
    const QString absolute = cleanSlashes(QDir(root).absoluteFilePath(rel));
    const QFileInfo info(absolute);
    if (!info.exists() || !info.isFile()) {
        setError(error, QStringLiteral("Untracked path is not a file: %1").arg(rel));
        return false;
    }
    if (backupRoot.trimmed().isEmpty()) {
        setError(error, QStringLiteral(
                "Refusing to delete an untracked file without a backup location"));
        return false;
    }

    const QString destination = cleanSlashes(
        QStringLiteral("%1/%2/%3")
            .arg(backupRoot.trimmed())
            .arg(QDateTime::currentMSecsSinceEpoch())
            .arg(rel));
    if (!QDir().mkpath(QFileInfo(destination).absolutePath())) {
        setError(error, QStringLiteral("Failed to create the backup directory"));
        return false;
    }
    if (!QFile::copy(absolute, destination)) {
        setError(error, QStringLiteral("Failed to back up the file before deleting it"));
        return false;
    }
    if (QFileInfo(destination).size() != info.size()) {
        QFile::remove(destination);
        setError(error, QStringLiteral("Backup verification failed; the file was kept"));
        return false;
    }
    if (!QFile::remove(absolute)) {
        setError(error, QStringLiteral("Failed to delete %1").arg(rel));
        return false;
    }
    if (backupPath) *backupPath = destination;
    return true;
}

QVector<GitHunk> GitService::parseHunks(const QString& diff, QString* error) {
    QVector<GitHunk> hunks;
    setError(error, QString());
    if (diff.trimmed().isEmpty()) {
        setError(error, QStringLiteral("There is no diff to split into hunks"));
        return hunks;
    }

    const QStringList lines = diff.split(QLatin1Char('\n'));
    int skippedNew = 0;
    int skippedBinary = 0;
    for (const QString& line : lines) {
        if (line.startsWith(QStringLiteral("new file mode"))
                || line.startsWith(QStringLiteral("deleted file mode"))
                || line.startsWith(QStringLiteral("similarity index"))
                || line.startsWith(QStringLiteral("rename from"))
                || line.startsWith(QStringLiteral("copy from"))
                || line.startsWith(QStringLiteral("--- /dev/null"))
                || line.startsWith(QStringLiteral("+++ /dev/null")))
            ++skippedNew;
        if (line.startsWith(QStringLiteral("Binary files"))
                || line.startsWith(QStringLiteral("GIT binary patch")))
            ++skippedBinary;
    }
    if (skippedBinary > 0) {
        setError(error, QStringLiteral(
                "Binary files cannot be staged hunk by hunk"));
        return hunks;
    }
    if (skippedNew > 0) {
        setError(error, QStringLiteral(
                "Added, deleted or renamed files must be staged as a whole"));
        return hunks;
    }

    static const QRegularExpression hunkRe(
        QStringLiteral("^@@ -(\\d+)(?:,(\\d+))? \\+(\\d+)(?:,(\\d+))? @@"));

    // Row label prefers an added line, since that is what staging brings in;
    // a pure deletion falls back to the removed line.
    const auto label = [](const QString& line) {
        QString text = line.mid(1).trimmed();
        if (text.size() > 90) text = text.left(90) + QStringLiteral("…");
        return text;
    };

    GitHunk current;
    QString currentAdded;
    QString currentRemoved;
    bool inHunk = false;
    const auto flush = [&]() {
        if (!inHunk || !current.isValid()) return;
        current.summary = currentAdded.isEmpty() ? currentRemoved : currentAdded;
        hunks.push_back(current);
        current = GitHunk();
        currentAdded.clear();
        currentRemoved.clear();
        inHunk = false;
    };

    for (const QString& line : lines) {
        const QRegularExpressionMatch match = hunkRe.match(line);
        if (match.hasMatch()) {
            flush();
            current.index = hunks.size();
            current.header = line;
            current.oldStart = match.captured(1).toInt();
            current.oldLines = match.captured(2).isEmpty() ? 1
                                                           : match.captured(2).toInt();
            current.newStart = match.captured(3).toInt();
            current.newLines = match.captured(4).isEmpty() ? 1
                                                           : match.captured(4).toInt();
            inHunk = true;
            continue;
        }
        if (!inHunk) continue;
        // A new file header ends this file's hunk list.
        if (line.startsWith(QStringLiteral("diff --git "))) {
            flush();
            continue;
        }
        if (line.startsWith(QLatin1Char('+'))) {
            current.additions += 1;
            current.lines << line;
            if (currentAdded.isEmpty()) currentAdded = label(line);
        } else if (line.startsWith(QLatin1Char('-'))) {
            current.removals += 1;
            current.lines << line;
            if (currentRemoved.isEmpty()) currentRemoved = label(line);
        } else if (line.startsWith(QLatin1Char(' '))
                   || line.startsWith(QLatin1Char('\\'))
                   || line.isEmpty()) {
            current.lines << line;
        }
    }
    flush();

    if (hunks.isEmpty()) {
        setError(error, QStringLiteral("This diff has no staging hunks"));
        return hunks;
    }
    return hunks;
}

QVector<GitHunk> GitService::diffHunks(const QString& path, bool staged,
                                       QString* error) const {
    QVector<GitHunk> hunks;
    setError(error, QString());
    const QString root = repositoryRoot();
    if (root.isEmpty()) {
        setError(error, QStringLiteral("Not a git repository: %1").arg(baseDir()));
        return hunks;
    }
    bool truncated = false;
    QString diffError;
    const QString diff = workingDiff(path, staged, &truncated, &diffError);
    if (!diffError.isEmpty()) {
        setError(error, diffError);
        return hunks;
    }
    if (truncated) {
        setError(error, QStringLiteral(
                "The diff is too large to split; stage this file as a whole"));
        return hunks;
    }
    QString parseError;
    hunks = parseHunks(diff, &parseError);
    if (!parseError.isEmpty()) {
        setError(error, parseError);
        return hunks;
    }
    return hunks;
}

bool GitService::applyHunks(const QString& path,
                           const QVector<GitHunkRef>& selected,
                           bool staged, bool reverse, QString* error) {
    setError(error, QString());
    if (selected.isEmpty()) {
        setError(error, QStringLiteral("No hunks selected"));
        return false;
    }
    const QString root = repositoryRoot();
    if (root.isEmpty()) {
        setError(error, QStringLiteral("Not a git repository: %1").arg(baseDir()));
        return false;
    }
    bool ok = false;
    const QString rel = normalizePath(root, path, &ok);
    if (!ok) {
        setError(error, QStringLiteral("Path is outside the repository: %1").arg(path));
        return false;
    }

    // Re-read the diff here rather than trusting the list the UI is showing:
    // if the file changed since, the patch simply stops applying and git
    // reports it, instead of writing a half-matched hunk to the index.
    QString diffError;
    bool truncated = false;
    const QString diff = workingDiff(rel, staged, &truncated, &diffError);
    if (!diffError.isEmpty()) {
        setError(error, diffError);
        return false;
    }
    QString parseError;
    const QVector<GitHunk> hunks = parseHunks(diff, &parseError);
    if (!parseError.isEmpty()) {
        setError(error, parseError);
        return false;
    }

    QByteArray patch;
    for (const GitHunkRef& ref : selected) {
        if (ref.index < 0 || ref.index >= hunks.size()) {
            setError(error, QStringLiteral(
                    "Hunk %1 no longer exists in this diff").arg(ref.index));
            return false;
        }
        const GitHunk& hunk = hunks.at(ref.index);
        if (!ref.header.isEmpty() && ref.header != hunk.header) {
            setError(error, QStringLiteral(
                    "This file changed since the hunks were listed; "
                    "refresh and try again"));
            return false;
        }
        patch += QStringLiteral("--- a/%1\n+++ b/%1\n%2\n")
                     .arg(rel, hunk.header)
                     .toUtf8();
        for (const QString& line : hunk.lines)
            patch += line.toUtf8() + '\n';
    }

    QStringList args{QStringLiteral("apply"), QStringLiteral("--cached"),
                     QStringLiteral("--whitespace=nowarn")};
    if (reverse) args << QStringLiteral("--reverse");
    const CommandResult r = runWithInput(args, patch);
    if (!r.ok) {
        setError(error, r.error);
        return false;
    }
    return true;
}

QString GitService::maskedUrl(const QString& raw) {
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty()) return {};

    static const QRegularExpression schemeRe(
        QStringLiteral("^[a-zA-Z][a-zA-Z0-9+.-]*://"));
    if (schemeRe.match(trimmed).hasMatch()) {
        const QUrl url(trimmed);
        if (url.isValid())
            return url.toDisplayString(QUrl::RemoveUserInfo);
    }

    // git's scp-like form ([[user@]host:]path): drop the user part, keep host
    // and path. A plain local path has no '@' before its first separator.
    const int at = trimmed.indexOf(QLatin1Char('@'));
    const int slash = trimmed.indexOf(QLatin1Char('/'));
    const int colon = trimmed.indexOf(QLatin1Char(':'));
    if (at > 0 && (slash < 0 || at < slash) && (colon < 0 || at < colon))
        return trimmed.mid(at + 1);
    return trimmed;
}

void GitService::parseTrack(const QString& track, QString* state,
                           int* ahead, int* behind) {
    if (state) state->clear();
    if (ahead) *ahead = 0;
    if (behind) *behind = 0;
    if (track.isEmpty()) return;
    if (track.contains(QStringLiteral("gone"))) {
        if (state) *state = QStringLiteral("gone");
        return;
    }

    static const QRegularExpression aheadRe(
        QStringLiteral("ahead (\\d+)"));
    static const QRegularExpression behindRe(
        QStringLiteral("behind (\\d+)"));
    const QRegularExpressionMatch aheadMatch = aheadRe.match(track);
    const QRegularExpressionMatch behindMatch = behindRe.match(track);
    if (aheadMatch.hasMatch())
        *ahead = aheadMatch.captured(1).toInt();
    if (behindMatch.hasMatch())
        *behind = behindMatch.captured(1).toInt();

    if (*ahead > 0 && *behind > 0) {
        if (state) *state = QStringLiteral("diverged");
    } else if (*ahead > 0) {
        if (state) *state = QStringLiteral("ahead");
    } else if (*behind > 0) {
        if (state) *state = QStringLiteral("behind");
    } else {
        if (state) *state = QStringLiteral("synced");
    }
}

QVector<GitBranchInfo> GitService::branches(QString* error) const {
    setError(error, QString());
    QVector<GitBranchInfo> out;
    if (repositoryRoot().isEmpty()) {
        setError(error, QStringLiteral("Not a git repository: %1").arg(baseDir()));
        return out;
    }

    // One field-separated pass instead of a process per branch: ahead/behind
    // comes from `upstream:track`, which git already computes.
    const QString sep = QString::fromLatin1("\x1f");
    const QString format = QStringLiteral("--format=%(refname:short)")
            + sep + QStringLiteral("%(objectname:short)")
            + sep + QStringLiteral("%(upstream:short)")
            + sep + QStringLiteral("%(upstream:track)")
            + sep + QStringLiteral("%(committerdate:unix)")
            + sep + QStringLiteral("%(HEAD)") + sep;
    const CommandResult r = run({QStringLiteral("for-each-ref"), format,
                                 QStringLiteral("refs/heads")});
    if (!r.ok) {
        setError(error, r.error);
        return out;
    }

    const QStringList lines = r.output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const QStringList fields = line.split(sep, Qt::KeepEmptyParts);
        if (fields.size() < 6) continue;
        GitBranchInfo info;
        info.name = fields.at(0).trimmed();
        info.sha = fields.at(1).trimmed();
        info.upstream = fields.at(2).trimmed();
        info.track = fields.at(3).trimmed();
        info.timestamp = fields.at(4).trimmed().toLongLong();
        info.current = fields.at(5).trimmed() == QLatin1String("*");
        parseTrack(info.track, &info.trackState, &info.ahead, &info.behind);
        if (info.trackState.isEmpty())
            info.trackState = info.upstream.isEmpty()
                ? QStringLiteral("none") : QStringLiteral("synced");
        if (info.isValid())
            out.push_back(info);
    }
    return out;
}

QVector<GitRemoteInfo> GitService::remotes(QString* error) const {
    setError(error, QString());
    QVector<GitRemoteInfo> out;
    if (repositoryRoot().isEmpty()) {
        setError(error, QStringLiteral("Not a git repository: %1").arg(baseDir()));
        return out;
    }

    const CommandResult names = run({QStringLiteral("remote")});
    if (!names.ok) {
        setError(error, names.error);
        return out;
    }
    const QStringList remoteNames =
        names.output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& name : remoteNames) {
        const QString remote = name.trimmed();
        if (remote.isEmpty()) continue;
        GitRemoteInfo info;
        info.name = remote;

        const CommandResult url = run({QStringLiteral("config"),
                                       QStringLiteral("--get"),
                                       QStringLiteral("remote.%1.url").arg(remote)});
        info.url = maskedUrl(url.ok ? url.output : QString());

        const CommandResult refs = run({QStringLiteral("for-each-ref"),
                                        QStringLiteral("--format=%(refname:short)"),
                                        QStringLiteral("refs/remotes/%1").arg(remote)});
        if (refs.ok) {
            const QStringList lines =
                refs.output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const QString& line : lines) {
                const QString ref = line.trimmed();
                // `<remote>/HEAD` is just the symbolic default-branch pointer.
                if (ref.isEmpty() || ref.endsWith(QStringLiteral("/HEAD")))
                    continue;
                if (info.branches.size() < 200)
                    info.branches.append(ref);
            }
        }
        if (info.isValid())
            out.push_back(info);
    }
    return out;
}

} // namespace NexusAI
