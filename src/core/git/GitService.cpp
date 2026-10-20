#include "GitService.h"

#include "core/tools/Sandbox.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>

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
    CommandResult result;
    QProcess process;
    process.setProgram(QStringLiteral("git"));
    process.setArguments(args);
    process.setWorkingDirectory(baseDir());
    process.setProcessChannelMode(QProcess::SeparateChannels);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // Read-only guarantee: git must not refresh/write the index just because
    // the app asked for history/status. No prompts, no pager.
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
    result.ok = result.exitCode == 0 || result.truncated;
    result.output = QString::fromUtf8(out);
    const QString stderrText = QString::fromUtf8(err).trimmed();
    if (result.timedOut) {
        result.ok = false;
        result.error = QStringLiteral("git timed out after %1 ms").arg(m_timeoutMs);
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

    const int count = qBound(1, limit, 500);
    const int skip = qMax(0, offset);
    QStringList args{
        QStringLiteral("log"),
        QStringLiteral("--no-color"),
        QStringLiteral("--date=unix"),
        QStringLiteral("--max-count=%1").arg(count),
        QStringLiteral("--skip=%1").arg(skip),
        QStringLiteral("--pretty=format:%H%x00%h%x00%an%x00%ae%x00%at%x00%P%x00%s")};
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

    QVector<GitFileStat> files;
    const QStringList tokens =
        names.output.split(QLatin1Char('\0'), Qt::SkipEmptyParts);
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

    // Line stats are a second pass: --numstat -z uses two extra NUL fields
    // for renames, which is easier to parse on its own than merged with
    // name-status output.
    const CommandResult nums = run(
        {QStringLiteral("show"), QStringLiteral("--format="),
         QStringLiteral("--numstat"), QStringLiteral("-z"),
         QStringLiteral("--find-renames"), QStringLiteral("-m"),
         QStringLiteral("--first-parent"), sha});
    if (nums.ok) {
        struct Counts { int additions; int removals; bool binary; };
        QHash<QString, Counts> counts;
        const QStringList numTokens =
            nums.output.split(QLatin1Char('\0'), Qt::SkipEmptyParts);
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
                ++i;                  // old path
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
    return files;
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
    return diff;
}

} // namespace NexusAI
