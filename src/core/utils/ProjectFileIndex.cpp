#include "core/utils/ProjectFileIndex.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QVector>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include <algorithm>

namespace NexusAI::ProjectFileIndex {

namespace {

const QSet<QString>& ignoredDirNames() {
    static const QSet<QString> names{
        QStringLiteral(".git"), QStringLiteral(".svn"), QStringLiteral(".hg"),
        QStringLiteral("node_modules"), QStringLiteral("bower_components"),
        QStringLiteral("build"), QStringLiteral("out"), QStringLiteral("dist"),
        QStringLiteral("target"), QStringLiteral(".venv"), QStringLiteral("venv"),
        QStringLiteral("__pycache__"), QStringLiteral(".idea"), QStringLiteral(".vscode"),
        QStringLiteral(".gradle"), QStringLiteral(".next"), QStringLiteral(".nuxt"),
        QStringLiteral(".cache"), QStringLiteral("coverage"), QStringLiteral(".pytest_cache"),
        QStringLiteral(".mypy_cache"), QStringLiteral(".ruff_cache"),
        QStringLiteral("cmake-build-debug"), QStringLiteral("cmake-build-release")
    };
    return names;
}

const QSet<QString>& indexableSuffixes() {
    static const QSet<QString> suffixes{
        QStringLiteral("c"), QStringLiteral("cc"), QStringLiteral("cpp"),
        QStringLiteral("cxx"), QStringLiteral("h"), QStringLiteral("hh"),
        QStringLiteral("hpp"), QStringLiteral("hxx"), QStringLiteral("inl"),
        QStringLiteral("ipp"), QStringLiteral("qml"), QStringLiteral("qmltypes"),
        QStringLiteral("qrc"), QStringLiteral("ui"),
        QStringLiteral("js"), QStringLiteral("mjs"), QStringLiteral("cjs"),
        QStringLiteral("ts"), QStringLiteral("tsx"), QStringLiteral("jsx"),
        QStringLiteral("vue"), QStringLiteral("svelte"), QStringLiteral("astro"),
        QStringLiteral("py"), QStringLiteral("pyi"), QStringLiteral("java"),
        QStringLiteral("kt"), QStringLiteral("kts"), QStringLiteral("scala"),
        QStringLiteral("groovy"), QStringLiteral("clj"), QStringLiteral("cljs"),
        QStringLiteral("cljc"), QStringLiteral("edn"), QStringLiteral("rs"),
        QStringLiteral("go"), QStringLiteral("rb"), QStringLiteral("rake"),
        QStringLiteral("php"), QStringLiteral("cs"), QStringLiteral("fs"),
        QStringLiteral("fsx"), QStringLiteral("vb"), QStringLiteral("swift"),
        QStringLiteral("m"), QStringLiteral("mm"), QStringLiteral("dart"),
        QStringLiteral("lua"), QStringLiteral("pl"), QStringLiteral("pm"),
        QStringLiteral("r"), QStringLiteral("jl"), QStringLiteral("ex"),
        QStringLiteral("exs"), QStringLiteral("erl"), QStringLiteral("hrl"),
        QStringLiteral("hs"), QStringLiteral("lhs"), QStringLiteral("elm"),
        QStringLiteral("ml"), QStringLiteral("mli"), QStringLiteral("nim"),
        QStringLiteral("zig"), QStringLiteral("sh"), QStringLiteral("bash"),
        QStringLiteral("zsh"), QStringLiteral("fish"), QStringLiteral("ps1"),
        QStringLiteral("psm1"), QStringLiteral("bat"), QStringLiteral("cmd"),
        QStringLiteral("sql"), QStringLiteral("graphql"), QStringLiteral("gql"),
        QStringLiteral("proto"), QStringLiteral("thrift"),
        QStringLiteral("json"), QStringLiteral("jsonc"), QStringLiteral("json5"),
        QStringLiteral("yaml"), QStringLiteral("yml"), QStringLiteral("toml"),
        QStringLiteral("ini"), QStringLiteral("cfg"), QStringLiteral("conf"),
        QStringLiteral("properties"), QStringLiteral("xml"), QStringLiteral("plist"),
        QStringLiteral("md"), QStringLiteral("markdown"), QStringLiteral("rst"),
        QStringLiteral("adoc"), QStringLiteral("asciidoc"), QStringLiteral("txt"),
        QStringLiteral("tex"), QStringLiteral("html"), QStringLiteral("htm"),
        QStringLiteral("xhtml"), QStringLiteral("css"), QStringLiteral("scss"),
        QStringLiteral("sass"), QStringLiteral("less"), QStringLiteral("csv"),
        QStringLiteral("tsv"), QStringLiteral("log"), QStringLiteral("cmake"),
        QStringLiteral("pro"), QStringLiteral("pri"), QStringLiteral("mk"),
        QStringLiteral("make"), QStringLiteral("gradle"), QStringLiteral("bzl"),
        QStringLiteral("tf"), QStringLiteral("tfvars"), QStringLiteral("hcl")
    };
    return suffixes;
}

const QSet<QString>& indexableNames() {
    static const QSet<QString> names{
        QStringLiteral("makefile"), QStringLiteral("gnumakefile"),
        QStringLiteral("dockerfile"), QStringLiteral("containerfile"),
        QStringLiteral("build"), QStringLiteral("workspace"),
        QStringLiteral("meson.build"), QStringLiteral("cmakelists.txt"),
        QStringLiteral("license"), QStringLiteral("licence"),
        QStringLiteral("readme"), QStringLiteral("notice"), QStringLiteral("copying"),
        QStringLiteral("authors"), QStringLiteral("contributing"),
        QStringLiteral("codeowners"), QStringLiteral("changelog"),
        QStringLiteral(".gitignore"), QStringLiteral(".gitattributes"),
        QStringLiteral(".editorconfig"), QStringLiteral(".clang-format"),
        QStringLiteral(".clang-tidy"), QStringLiteral(".dockerignore"),
        QStringLiteral(".npmrc"), QStringLiteral(".nvmrc"),
        QStringLiteral(".prettierrc"), QStringLiteral(".eslintrc"),
        QStringLiteral(".babelrc"), QStringLiteral(".browserslistrc"),
        QStringLiteral(".env.example"), QStringLiteral(".env.sample"),
        QStringLiteral(".env.template")
    };
    return names;
}

void walk(const QDir& rootDir, const QDir& dir, int maxFiles, QStringList* out) {
    if (out->size() >= maxFiles) return;
    const QFileInfoList entries = dir.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks | QDir::Hidden,
        QDir::Name);
    for (const QFileInfo& info : entries) {
        if (out->size() >= maxFiles) return;
        if (info.isSymLink()) continue;
        if (info.isDir()) {
            if (!isIgnoredDir(info.fileName()))
                walk(rootDir, QDir(info.filePath()), maxFiles, out);
            continue;
        }
        if (!info.isFile() || !isIndexableFile(info.fileName()))
            continue;
        QString rel = rootDir.relativeFilePath(info.filePath());
        rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
        out->append(rel);
    }
}

struct PathResolution {
    QString canonical;
    QString relative;
    QString error;   // empty on success
    qint64 size{0};
};

// Shared by the composer check and the submit-time resolver so both agree on
// what counts as a valid reference.
PathResolution resolveTokenPath(const QDir& rootDir, const QString& raw) {
    PathResolution out;
    const QString rel = QDir::cleanPath(raw);
    if (rel.isEmpty() || rel == QLatin1String(".") || rel == QLatin1String("..")
            || rel.startsWith(QLatin1String("../"))
            || QDir::isAbsolutePath(rel)) {
        out.error = QStringLiteral("outside");
        return out;
    }
    // Drive-relative Windows paths ("C:foo") are not absolute per Qt but
    // must never be accepted either.
    if (rel.size() > 1 && rel.at(1) == QLatin1Char(':')) {
        out.error = QStringLiteral("outside");
        return out;
    }

    const QFileInfo info(rootDir.absoluteFilePath(rel));
    const QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty() || !info.isFile()) {
        out.error = QStringLiteral("missing");
        return out;
    }
    if (isSensitiveFile(canonical)) {
        out.error = QStringLiteral("sensitive");
        return out;
    }

    const QString canonicalRel = rootDir.relativeFilePath(canonical);
    if (canonicalRel == QLatin1String("..")
            || canonicalRel.startsWith(QLatin1String("../"))
            || QDir::isAbsolutePath(canonicalRel)) {
        out.error = QStringLiteral("outside");
        return out;
    }

    out.canonical = canonical;
    out.relative = canonicalRel;
    out.relative.replace(QLatin1Char('\\'), QLatin1Char('/'));
    out.size = info.size();
    return out;
}

} // namespace

bool isIgnoredDir(const QString& name) {
    if (ignoredDirNames().contains(name))
        return true;
    return name.startsWith(QLatin1String("cmake-build-"))
        || name.startsWith(QLatin1String("build-"));
}

bool isSensitiveFile(const QString& name) {
    const QString base = QFileInfo(name).fileName().toLower();
    if (base == QLatin1String(".env"))
        return true;
    if (base.startsWith(QLatin1String(".env."))
            && base != QLatin1String(".env.example")
            && base != QLatin1String(".env.sample")
            && base != QLatin1String(".env.template"))
        return true;
    if (base == QLatin1String("id_rsa") || base == QLatin1String("id_dsa")
            || base == QLatin1String("id_ecdsa") || base == QLatin1String("id_ed25519")
            || base == QLatin1String("credentials") || base == QLatin1String("secrets"))
        return true;
    const QString suffix = QFileInfo(base).suffix();
    return suffix == QLatin1String("pem") || suffix == QLatin1String("key")
        || suffix == QLatin1String("p12") || suffix == QLatin1String("pfx")
        || suffix == QLatin1String("kdbx") || suffix == QLatin1String("ppk");
}

bool isIndexableFile(const QString& name) {
    if (isSensitiveFile(name))
        return false;
    const QString lower = name.toLower();
    if (indexableNames().contains(lower))
        return true;
    if (lower.startsWith(QLatin1String("dockerfile"))
            || lower.startsWith(QLatin1String("makefile")))
        return true;
    const int dot = lower.lastIndexOf(QLatin1Char('.'));
    if (dot < 0 || dot == lower.size() - 1)
        return false;
    return indexableSuffixes().contains(lower.mid(dot + 1));
}

// ── File browser: one level at a time ─────────────────────
//
// A directory listing and a recursive index answer different questions, so
// they filter differently on purpose:
//
//   scan()      "which files may I put in front of the model?" - narrow,
//               extension whitelist, no binaries, no secrets.
//   listDir()   "what is actually in this folder?" - shows what a file
//               manager would show, minus ignored dirs and secrets.
//
// Keeping them separate is a decision, not an oversight: unifying them would
// either hide real files from the browser or feed binaries to the model.
namespace {

// QFileInfo::canonicalFilePath resolves symlinks on POSIX but leaves Windows
// junctions untouched (Qt even reports isSymLink() == false for them). Ask the
// OS for the final path of an open handle instead, which resolves both kinds
// of reparse point, so a junction inside the project cannot expose a file
// outside it.
QString canonicalForContainment(const QString& path) {
#ifdef Q_OS_WIN
    const QString native = QDir::toNativeSeparators(path);
    HANDLE handle = CreateFileW(
        reinterpret_cast<LPCWSTR>(native.utf16()), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};

    QString result;
    const DWORD needed = GetFinalPathNameByHandleW(
        handle, nullptr, 0, FILE_NAME_NORMALIZED);
    if (needed > 0) {
        QVector<wchar_t> buffer(static_cast<int>(needed) + 1);
        const DWORD written = GetFinalPathNameByHandleW(
            handle, buffer.data(), static_cast<DWORD>(buffer.size()),
            FILE_NAME_NORMALIZED);
        if (written > 0 && written < static_cast<DWORD>(buffer.size())) {
            result = QString::fromWCharArray(buffer.constData(),
                                             static_cast<int>(written));
        }
    }
    CloseHandle(handle);

    // Strip the extended-length prefix the API returns.
    if (result.startsWith(QStringLiteral("\\\\?\\UNC\\")))
        result = QStringLiteral("\\\\") + result.mid(8);
    else if (result.startsWith(QStringLiteral("\\\\?\\")))
        result = result.mid(4);
    return QDir::cleanPath(QDir::fromNativeSeparators(result));
#else
    return QFileInfo(path).canonicalFilePath();
#endif
}

// Resolve relPath under root without leaving it. Mirrors the checks in
// resolveTokenPath: absolute paths, drive-relative paths and ".." escapes are
// all refused before touching the filesystem.
bool resolveUnderRoot(const QString& root, const QString& relPath,
                      QString* absolute, QString* error) {
    const QString cleanRoot = QDir::cleanPath(QDir::fromNativeSeparators(root));
    if (cleanRoot.isEmpty() || !QDir(cleanRoot).exists()) {
        if (error) *error = QStringLiteral("no-project");
        return false;
    }

    const QString wanted = relPath.trimmed();
    QString joined;
    if (wanted.isEmpty() || wanted == QLatin1String(".")) {
        joined = cleanRoot;
    } else {
        const QString rel = QDir::cleanPath(QDir::fromNativeSeparators(wanted));
        // Absolute, drive-relative, or escaping upward.
        if (QDir::isAbsolutePath(rel) || rel == QLatin1String("..")
                || rel.startsWith(QLatin1String("../"))
                || (rel.size() > 1 && rel.at(1) == QLatin1Char(':'))) {
            if (error) *error = QStringLiteral("outside");
            return false;
        }
        joined = QDir::cleanPath(cleanRoot + QLatin1Char('/') + rel);
    }

    QString prefix = cleanRoot;
    if (!prefix.endsWith(QLatin1Char('/'))) prefix += QLatin1Char('/');
#if defined(Q_OS_WIN)
    const Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    // cleanPath already folded any "..", so an escape simply stops matching.
    // The equal-to-root case has to be allowed explicitly (root has no slash).
    if (joined != cleanRoot
            && !(joined + QLatin1Char('/')).startsWith(prefix, sensitivity)) {
        if (error) *error = QStringLiteral("outside");
        return false;
    }

    *absolute = joined;
    return true;
}

QString relativeUnder(const QString& root, const QString& absolute) {
    QString rel = QDir(QDir::cleanPath(QDir::fromNativeSeparators(root)))
                      .relativeFilePath(absolute);
    rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return rel;
}

} // namespace

QVector<DirEntry> listDir(const QString& root, const QString& relPath,
                          QString* error) {
    QVector<DirEntry> out;
    if (error) error->clear();

    QString absolute;
    if (!resolveUnderRoot(root, relPath, &absolute, error))
        return out;

    const QFileInfo dirInfo(absolute);
    if (!dirInfo.exists() || !dirInfo.isDir()) {
        if (error) *error = QStringLiteral("missing");
        return out;
    }

    const QDir dir(absolute);
    if (!dir.isReadable()) {
        if (error) *error = QStringLiteral("unreadable");
        return out;
    }

    const QFileInfoList entries = dir.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDir::Name);
    out.reserve(entries.size());
    for (const QFileInfo& info : entries) {
        // Symlinks are skipped outright: following them can loop, and one
        // pointing outside the project would smuggle foreign content into a
        // view that claims to show this project only.
        if (info.isSymLink())
            continue;
        if (info.isDir()) {
            if (isIgnoredDir(info.fileName()))
                continue;
        } else if (!info.isFile() || isSensitiveFile(info.fileName())) {
            continue;
        }

        DirEntry entry;
        entry.name = info.fileName();
        entry.relPath = relativeUnder(root, info.filePath());
        entry.isDir = info.isDir();
        out.append(entry);
    }

    // Directories before files, each alphabetically. Sorted here rather than
    // relying on QDir::Name so the two groups stay separated.
    std::sort(out.begin(), out.end(), [](const DirEntry& a, const DirEntry& b) {
        if (a.isDir != b.isDir) return a.isDir;
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return out;
}

QVector<SearchHit> searchPaths(const QString& root, const QString& query,
                               int maxResults) {
    QVector<SearchHit> out;
    if (maxResults <= 0) return out;

    const QString needle = query.trimmed().toLower();
    if (needle.isEmpty()) return out;

    QString error;
    QString rootAbs;
    if (!resolveUnderRoot(root, QString(), &rootAbs, &error))
        return out;

    // Iterative walk with an explicit stack: recursion is fine, but a
    // depth-first loop makes the result cap and elapsed-time checks simpler.
    QStringList pending{rootAbs};
    QElapsedTimer timer;
    timer.start();

    while (!pending.isEmpty() && out.size() < maxResults) {
        // A runaway walk (network drive, generated tree) must not freeze the
        // GUI thread that called us. 2s is generous for a local SSD and small
        // enough that a bad path degrades to "partial results" instead of a
        // frozen window.
        if (timer.elapsed() > 2000) break;

        const QString dirAbs = pending.takeFirst();
        const QDir dir(dirAbs);
        const QFileInfoList entries = dir.entryInfoList(
            QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
            QDir::Name);
        for (const QFileInfo& info : entries) {
            if (info.isSymLink()) continue;

            if (info.isDir()) {
                if (isIgnoredDir(info.fileName())) continue;
                if (out.size() < maxResults) pending.append(info.filePath());
            } else if (!info.isFile() || isSensitiveFile(info.fileName())) {
                continue;
            }

            QString rel = relativeUnder(root, info.filePath());
            if (rel.toLower().contains(needle)) {
                SearchHit hit;
                hit.relPath = rel;
                hit.isDir = info.isDir();
                out.append(hit);
                if (out.size() >= maxResults) break;
            }
        }
    }
    return out;
}

FilePreview readFilePreview(const QString& root, const QString& relPath,
                            qint64 maxBytes) {
    FilePreview preview;
    if (maxBytes <= 0) maxBytes = 512 * 1024;

    QString absolute;
    if (!resolveUnderRoot(root, relPath, &absolute, &preview.error))
        return preview;

    // resolveUnderRoot only checks the lexical path. A symlink or a Windows
    // junction that lives inside the project can still target a file outside
    // it, so the canonical target is what decides containment. Everything
    // below reads the canonical path, never the link itself.
    const QString cleanRoot = QDir::cleanPath(QDir::fromNativeSeparators(root));
    const QString canonicalRoot = canonicalForContainment(cleanRoot);
    if (canonicalRoot.isEmpty()) {
        preview.error = QStringLiteral("unreadable");
        return preview;
    }

    const QFileInfo info(absolute);
    if (!info.exists() || !info.isFile()) {
        preview.error = QStringLiteral("missing");
        return preview;
    }

    const QString canonical = canonicalForContainment(absolute);
    if (canonical.isEmpty()) {
        // A broken link, or a target the OS refuses to resolve.
        preview.error = QStringLiteral("unreadable");
        return preview;
    }

#if defined(Q_OS_WIN)
    const Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    QString prefix = QDir::cleanPath(canonicalRoot);
    if (!prefix.endsWith(QLatin1Char('/'))) prefix += QLatin1Char('/');
    const QString cleanCanonical =
        QDir::cleanPath(QDir::fromNativeSeparators(canonical));
    if (!cleanCanonical.startsWith(prefix, sensitivity)) {
        preview.error = QStringLiteral("outside");
        return preview;
    }

    const QFileInfo canonicalInfo(cleanCanonical);
    preview.bytes = canonicalInfo.size();

    QFile file(cleanCanonical);
    if (!file.open(QIODevice::ReadOnly)) {
        preview.error = QStringLiteral("unreadable");
        return preview;
    }

    // Read one byte past the cap so "exactly maxBytes" is not reported as
    // truncated.
    const QByteArray raw = file.read(maxBytes + 1);
    file.close();
    preview.truncated = raw.size() > maxBytes;
    const QByteArray head = preview.truncated ? raw.left(maxBytes) : raw;

    // A NUL byte early in the file is the cheap, reliable binary signal
    // (same heuristic git uses). Reporting "binary" beats rendering mojibake.
    constexpr int kBinaryProbeBytes = 8000;
    const QByteArray probe = head.left(kBinaryProbeBytes);
    if (probe.contains('\0')) {
        preview.binary = true;
        return preview;
    }

    // Decode as UTF-8. Qt6's fromUtf8 replaces invalid sequences rather than
    // failing, so a legacy-encoded file (GBK) shows replacement characters
    // instead of mojibake - acceptable for a read-only preview, and it keeps
    // the preview free of a QTextCodec dependency (Core5Compat is not linked).
    //
    // A truncated read can split a multi-byte character at the boundary; the
    // decoder drops that partial tail instead of emitting garbage.
    preview.text = QString::fromUtf8(head);
    return preview;
}

Mention mentionAt(const QString& text, int cursor) {
    Mention mention;
    const int end = qBound(0, cursor, text.size());
    int at = -1;
    for (int i = end - 1; i >= 0; --i) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char('\n')) break;
        if (ch == QLatin1Char('@')) {
            if (i == 0 || text.at(i - 1).isSpace())
                at = i;
            break;
        }
    }
    if (at < 0) return mention;

    // Accepting a completion replaces [start, cursor). Opening the popup
    // with the cursor inside a token would shred the remainder, so only
    // offer completion when the cursor sits at the token boundary.
    if (end < text.size() && !text.at(end).isSpace())
        return mention;

    const int tokenStart = at + 1;
    if (tokenStart >= end) {
        mention.active = true;
        mention.start = at;
        return mention;
    }

    if (text.at(tokenStart) == QLatin1Char('"')) {
        const int contentStart = tokenStart + 1;
        const int close = text.indexOf(QLatin1Char('"'), contentStart);
        if (close >= 0)
            return mention;   // closed quote: the reference is complete
        const QString token = text.mid(contentStart, end - contentStart);
        if (token.contains(QLatin1Char('\n')))
            return mention;
        mention.active = true;
        mention.start = at;
        mention.query = token;
        mention.quoted = true;
        return mention;
    }

    const QString token = text.mid(tokenStart, end - tokenStart);
    for (const QChar ch : token) {
        if (ch.isSpace())
            return mention;
    }
    mention.active = true;
    mention.start = at;
    mention.query = token;
    return mention;
}

QStringList mentionPaths(const QString& text) {
    static const QString trailingPunctuation =
        QStringLiteral(",;:!?.)]}。，；：！？）】");
    QStringList out;
    QSet<QString> seen;
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) != QLatin1Char('@'))
            continue;
        if (i > 0 && !text.at(i - 1).isSpace())
            continue;

        int j = i + 1;
        QString token;
        bool quoted = false;
        if (j < text.size() && text.at(j) == QLatin1Char('"')) {
            const int close = text.indexOf(QLatin1Char('"'), j + 1);
            if (close < 0) {
                i = j;
                continue;
            }
            token = text.mid(j + 1, close - j - 1);
            quoted = true;
            i = close;
        } else {
            const int start = j;
            while (j < text.size() && !text.at(j).isSpace())
                ++j;
            token = text.mid(start, j - start);
            i = j - 1;
        }

        token = token.trimmed();
        // Quoted tokens are explicit: punctuation inside the quotes is part
        // of the path. Unquoted ones swallow the sentence delimiter that
        // follows them ("@main.cpp." -> "main.cpp").
        if (!quoted) {
            while (!token.isEmpty() && trailingPunctuation.contains(token.back()))
                token.chop(1);
        }
        token.replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (token.isEmpty() || seen.contains(token))
            continue;
        seen.insert(token);
        out.append(token);
    }
    return out;
}

QStringList scan(const QString& root, int maxFiles) {
    QStringList out;
    if (root.isEmpty() || maxFiles <= 0)
        return out;
    const QDir rootDir(root);
    if (!rootDir.exists())
        return out;
    walk(rootDir, rootDir, maxFiles, &out);
    return out;
}

QStringList complete(const QStringList& files, const QString& query, int limit) {
    if (limit <= 0)
        return {};
    const QString q = query.trimmed().toLower();

    struct Scored {
        int score;
        qsizetype length;
        QString path;
    };
    QVector<Scored> scored;
    scored.reserve(files.size());
    for (const QString& path : files) {
        if (q.isEmpty()) {
            scored.append({0, path.size(), path});
            continue;
        }
        const QString lower = path.toLower();
        const int slash = lower.lastIndexOf(QLatin1Char('/'));
        const QString base = slash >= 0 ? lower.mid(slash + 1) : lower;
        int score = -1;
        if (base.startsWith(q)) score = 0;
        else if (base.contains(q)) score = 1;
        else if (lower.startsWith(q)) score = 2;
        else if (lower.contains(q)) score = 3;
        if (score >= 0)
            scored.append({score, path.size(), path});
    }

    std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
        if (a.score != b.score) return a.score < b.score;
        if (a.length != b.length) return a.length < b.length;
        return a.path.compare(b.path, Qt::CaseInsensitive) < 0;
    });

    QStringList out;
    const int count = qMin(limit, scored.size());
    out.reserve(count);
    for (int i = 0; i < count; ++i)
        out.append(scored.at(i).path);
    return out;
}

QVector<Reference> resolveReferences(const QString& root, const QString& text,
                                     const ReferenceLimits& limits) {
    QVector<Reference> out;
    if (root.isEmpty() || limits.maxFiles <= 0 || limits.maxBytesPerFile <= 0
            || limits.maxTotalBytes <= 0)
        return out;

    const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
    if (canonicalRoot.isEmpty())
        return out;
    const QDir rootDir(canonicalRoot);

    QSet<QString> seen;
    qint64 totalBytes = 0;
    const QStringList paths = mentionPaths(text);
    for (const QString& raw : paths) {
        if (out.size() >= limits.maxFiles || totalBytes >= limits.maxTotalBytes)
            break;

        const PathResolution resolved = resolveTokenPath(rootDir, raw);
        if (!resolved.error.isEmpty() || seen.contains(resolved.canonical))
            continue;
        seen.insert(resolved.canonical);

        QFile file(resolved.canonical);
        if (!file.open(QIODevice::ReadOnly))
            continue;

        const qint64 budget = qMin(limits.maxBytesPerFile,
                                   limits.maxTotalBytes - totalBytes);
        if (budget <= 0)
            break;

        // QFile::readLine() reserves a byte for the terminator, so a
        // byte-tight budget would come out one short and log a warning.
        const QByteArray bytes = file.read(budget);
        if (bytes.contains('\0'))
            continue;   // binary content must never reach the prompt

        const bool truncated = resolved.size > bytes.size();
        QString content = QString::fromUtf8(bytes);
        if (truncated) {
            content += QStringLiteral(
                "\n\n[... truncated: showing first %1 bytes of %2]")
                .arg(QString::number(bytes.size()),
                     QString::number(resolved.size));
        }

        totalBytes += bytes.size();
        out.append({resolved.relative, content, truncated});
    }
    return out;
}

QVector<ReferenceCheck> checkReferences(const QString& root, const QString& text) {
    constexpr int kMaxChecks = 32;
    QVector<ReferenceCheck> out;

    const QString canonicalRoot =
        root.isEmpty() ? QString() : QFileInfo(root).canonicalFilePath();
    const QDir rootDir(canonicalRoot);
    const QStringList paths = mentionPaths(text);
    out.reserve(qMin(paths.size(), kMaxChecks));

    for (const QString& raw : paths) {
        if (out.size() >= kMaxChecks)
            break;
        ReferenceCheck check;
        check.token = raw;
        if (canonicalRoot.isEmpty()) {
            check.error = QStringLiteral("no-project");
        } else {
            const PathResolution resolved = resolveTokenPath(rootDir, raw);
            check.error = resolved.error;
            check.path = resolved.relative;
        }
        out.append(check);
    }
    return out;
}

} // namespace NexusAI::ProjectFileIndex
