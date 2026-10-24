#include "ProjectFileViewModel.h"

#include "core/utils/ProjectFileIndex.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJSEngine>
#include <QProcess>
#include <QProcessEnvironment>
#include <QUrl>
#include <QDebug>
#include <windows.h>

namespace NexusAI {

// ── Emergency "open with" fallback ─────────────────────
//
// The shell's file association can point at anything, including broken
// handlers. Real-world case: an Electron markdown editor whose Node 24 runtime
// treated the .md file as a JavaScript entry point and crashed with
// "SyntaxError: Invalid or unexpected token" - while openInDefaultApp()
// reported success, so our UI said nothing went wrong.
//
// This fallback asks the registry which editors are actually registered and
// launches one directly with the file as an argument, bypassing whatever the
// association resolves to.

// Registry value reader. Two things the obvious implementation gets wrong:
//   1. RegGetValueW refuses HKEY_CLASSES_ROOT, so the key is opened with
//      RegOpenKeyExW and read with RegQueryValueExW.
//   2. RegQueryValueExW's value-name argument cannot carry a sub-key path
//      ("ProgId\shell\open\command" as a value name returns
//      ERROR_FILE_NOT_FOUND even though the key itself opens fine). Callers
//      pass the full KEY path here and the VALUE name (empty = default value)
//      separately.
static bool readRegString(HKEY root, const QString& keyPath,
                          const QString& valueName, QString* out) {
    const QString native = QDir::toNativeSeparators(keyPath);
    HKEY key = nullptr;
    const LONG rc = ::RegOpenKeyExW(
        root, reinterpret_cast<const wchar_t*>(native.utf16()), 0, KEY_READ, &key);
    if (rc != ERROR_SUCCESS) return false;

    DWORD type = 0, size = 0;
    LONG qrc = ::RegQueryValueExW(
        key,
        valueName.isEmpty() ? nullptr
            : reinterpret_cast<const wchar_t*>(valueName.utf16()),
        nullptr, &type, nullptr, &size);
    if (qrc != ERROR_SUCCESS || size == 0) {
        ::RegCloseKey(key);
        return false;
    }
    QByteArray buffer(int(size), char(0));
    qrc = ::RegQueryValueExW(
        key,
        valueName.isEmpty() ? nullptr
            : reinterpret_cast<const wchar_t*>(valueName.utf16()),
        nullptr, &type, reinterpret_cast<BYTE*>(buffer.data()), &size);
    ::RegCloseKey(key);
    if (qrc != ERROR_SUCCESS) return false;

    if (type == REG_EXPAND_SZ) {
        // REG_EXPAND_SZ carries %VAR% segments; the unexpanded form is not a
        // usable path.
        wchar_t expanded[MAX_PATH * 2];
        const DWORD n = ::ExpandEnvironmentStringsW(
            reinterpret_cast<const wchar_t*>(buffer.constData()),
            expanded, MAX_PATH * 2);
        *out = (n > 0 && n <= MAX_PATH * 2)
            ? QString::fromWCharArray(expanded)
            : QString::fromWCharArray(
                reinterpret_cast<const wchar_t*>(buffer.constData()));
    } else {
        *out = QString::fromWCharArray(
            reinterpret_cast<const wchar_t*>(buffer.constData()));
    }
    return true;
}

// Reads the command line of "<progId>\shell\open\command". "command" is a KEY
// whose DEFAULT value holds the command line, not a value of the progId key
// (measured: reading it as a value name returned ERROR_FILE_NOT_FOUND).
static bool readShellOpenCommand(HKEY root, const QString& progId,
                                 QString* out) {
    return readRegString(root,
                         progId + QStringLiteral("\\shell\\open\\command"),
                         QString(), out);
}

// Child processes must not inherit ELECTRON_RUN_AS_NODE: it turns every
// Electron application into a bare Node runtime, which then misreads the
// document it was handed as a script. The variable is harmless to native
// programs, so a copy with just that entry removed is safe for anything we
// might launch here.
static QProcessEnvironment cleanChildEnvironment() {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("ELECTRON_RUN_AS_NODE"));
    return env;
}

// Known markdown/code editors, probed where each installer registers itself.
// The .md UserChoice (what Explorer actually honours) comes first; everything
// else is a fixed list.
static QStringList candidateEditors(const QString& absolute) {
    QStringList commands;

    auto pushCommand = [&](QString value) {
        value = value.trimmed();
        if (value.isEmpty()) return;
        // A bare "%1" means "open with this file as the only argument".
        if (value.compare(QStringLiteral("\"%1\""), Qt::CaseInsensitive) == 0
                || value.compare(QStringLiteral("%1"), Qt::CaseInsensitive) == 0) {
            commands.append(QStringLiteral("\"%1\"").arg(absolute));
            return;
        }
        value.replace(QStringLiteral("%1"), absolute, Qt::CaseInsensitive);
        commands.append(value);
    };

    // 1. The user's own choice for .md.
    QString progId;
    if (readRegString(HKEY_CURRENT_USER,
                      QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion"
                                     "\\Explorer\\FileExts\\.md\\UserChoice"),
                      QStringLiteral("ProgId"), &progId) && !progId.isEmpty()) {
        QString cmd;
        if (readShellOpenCommand(HKEY_CLASSES_ROOT, progId, &cmd))
            pushCommand(cmd);
    }

    // 2. Fixed fallbacks: common markdown/code editors, by their ProgIds.
    for (const QString& id : {QStringLiteral("MarkText.Document"),
                              QStringLiteral("VSCodium.markdown"),
                              QStringLiteral("Applications\\VSCodium.exe"),
                              QStringLiteral("VSCode.md")}) {
        QString cmd;
        if (readShellOpenCommand(HKEY_CLASSES_ROOT, id, &cmd))
            pushCommand(cmd);
    }
    return commands;
}

ProjectFileViewModel* ProjectFileViewModel::s_instance = nullptr;

ProjectFileViewModel::ProjectFileViewModel(QObject* parent) : QObject(parent) {}

ProjectFileViewModel::~ProjectFileViewModel() = default;

ProjectFileViewModel* ProjectFileViewModel::instance() {
    if (!s_instance) s_instance = new ProjectFileViewModel(nullptr);
    return s_instance;
}

ProjectFileViewModel* ProjectFileViewModel::create(QQmlEngine*, QJSEngine*) {
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}

void ProjectFileViewModel::setRoot(const QString& root) {
    const QString trimmed = root.trimmed();
    if (trimmed == m_root) return;
    m_root = trimmed;
    // Listings belong to the previous root; nothing about them survives.
    m_cache.clear();
    emit rootChanged();
}

void ProjectFileViewModel::clearCache() {
    m_cache.clear();
}

QVariantList ProjectFileViewModel::children(const QString& relPath) {
    // Local chat: no project, so no listing. Answering with an empty list
    // (rather than an error) keeps the panel quiet here - there is nothing
    // wrong, there is simply nothing to browse.
    if (m_root.isEmpty())
        return {};

    const QString key = relPath.trimmed();
    const auto cached = m_cache.constFind(key);
    if (cached != m_cache.constEnd())
        return cached.value();

    QString error;
    const QVector<ProjectFileIndex::DirEntry> entries =
        ProjectFileIndex::listDir(m_root, key, &error);

    QVariantList out;
    out.reserve(entries.size());
    for (const auto& entry : entries) {
        out.append(QVariantMap{{QStringLiteral("name"), entry.name},
                               {QStringLiteral("relPath"), entry.relPath},
                               {QStringLiteral("isDir"), entry.isDir}});
    }
    // Failures and "no project" are not cached: a temporary permission error
    // or a race with a directory being created should not stick until the
    // user thinks to refresh. Only a real, successful listing is stored.
    if (error.isEmpty() && !m_root.isEmpty())
        m_cache.insert(key, out);
    return out;
}

QVariantList ProjectFileViewModel::search(const QString& query, int maxResults) {
    if (m_root.isEmpty()) return {};

    const QVector<ProjectFileIndex::SearchHit> hits =
        ProjectFileIndex::searchPaths(m_root, query, maxResults);

    QVariantList out;
    out.reserve(hits.size());
    for (const auto& hit : hits) {
        out.append(QVariantMap{{QStringLiteral("relPath"), hit.relPath},
                               {QStringLiteral("isDir"), hit.isDir}});
    }
    return out;
}

QVariantMap ProjectFileViewModel::preview(const QString& relPath, int maxBytes) {
    const ProjectFileIndex::FilePreview preview =
        ProjectFileIndex::readFilePreview(m_root, relPath, maxBytes);
    return QVariantMap{{QStringLiteral("text"), preview.text},
                       {QStringLiteral("bytes"), preview.bytes},
                       {QStringLiteral("truncated"), preview.truncated},
                       {QStringLiteral("binary"), preview.binary},
                       {QStringLiteral("error"), preview.error}};
}

QString ProjectFileViewModel::absolutePath(const QString& relPath) const {
    if (m_root.isEmpty()) return {};
    const QString cleanRoot = QDir::cleanPath(QDir::fromNativeSeparators(m_root));
    const QString wanted = relPath.trimmed();
    if (wanted.isEmpty()) return cleanRoot;
    // A leading dash could be read as an option by whatever gets the path
    // later; the same rejection GitViewModel::absolutePath applies.
    if (wanted.startsWith(QLatin1Char('-')) || wanted.startsWith(QLatin1Char(':')))
        return {};

    QString prefix = cleanRoot;
    if (!prefix.endsWith(QLatin1Char('/'))) prefix += QLatin1Char('/');

    const QString clean = QDir::fromNativeSeparators(wanted);
    const QString absolute = QDir::cleanPath(
        QDir::isAbsolutePath(clean) ? clean : prefix + clean);

#if defined(Q_OS_WIN)
    const Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    // cleanPath already folded "..", so an escape stops matching the prefix.
    if (absolute != cleanRoot && !absolute.startsWith(prefix, sensitivity))
        return {};
    return absolute;
}

bool ProjectFileViewModel::openInDefaultApp(const QString& relPath) {
    const QString absolute = absolutePath(relPath);
    if (absolute.isEmpty() || !QFileInfo::exists(absolute)) return false;

    // ShellExecute spawns the handler with OUR environment. If this process
    // was started from a VSCode-family terminal it carries
    // ELECTRON_RUN_AS_NODE=1, and any Electron-based handler (MarkText was
    // the reported case) then boots as a bare Node runtime and treats the
    // file as a JavaScript entry point: "SyntaxError: Invalid or unexpected
    // token". Removing the variable before the spawn is what makes the
    // handler start as a GUI app again.
    QProcessEnvironment::systemEnvironment().remove(
        QStringLiteral("ELECTRON_RUN_AS_NODE"));
    return QDesktopServices::openUrl(QUrl::fromLocalFile(absolute));
}

bool ProjectFileViewModel::openAbsolute(const QString& absolutePath) {
    if (absolutePath.isEmpty() || !QFileInfo::exists(absolutePath)) return false;
    // Same ELECTRON_RUN_AS_NODE scrub as the openInDefaultApp path above.
    QProcessEnvironment::systemEnvironment().remove(
        QStringLiteral("ELECTRON_RUN_AS_NODE"));
    return QDesktopServices::openUrl(QUrl::fromLocalFile(absolutePath));
}

QString ProjectFileViewModel::canonicalAbsolute(const QString& absolutePath) const {
    if (absolutePath.isEmpty()) return {};
    const QFileInfo info(absolutePath);
    if (!info.exists()) return {};
    return info.canonicalFilePath();
}

QString ProjectFileViewModel::openWithKnownEditor(const QString& relPath) {
    const QString absolute = absolutePath(relPath);
    if (absolute.isEmpty() || !QFileInfo::exists(absolute)) return {};

    for (const QString& command : candidateEditors(absolute)) {
        // Split the executable from its own arguments: every candidate starts
        // with either a quoted path or a bare one.
        QString program;
        QString rest;
        if (command.startsWith(QLatin1Char('"'))) {
            const int close = command.indexOf(QLatin1Char('"'), 1);
            if (close < 0) continue;
            program = command.left(close + 1);
            rest = command.mid(close + 1).trimmed();
        } else {
            const int space = command.indexOf(QLatin1Char(' '));
            if (space > 0) {
                program = command.left(space);
                rest = command.mid(space + 1).trimmed();
            } else {
                program = command;
            }
        }
        program.remove(QLatin1Char('"'));
        if (program.isEmpty() || !QFileInfo::exists(program)) continue;

        QStringList args;
        if (!rest.isEmpty()) args.append(rest);
        args.append(absolute);
        QProcess proc;
        proc.setProgram(program);
        proc.setArguments(args);
        proc.setProcessEnvironment(cleanChildEnvironment());
        if (proc.startDetached())
            return program + QLatin1Char(' ') + absolute;
    }
    return {};
}

} // namespace NexusAI
