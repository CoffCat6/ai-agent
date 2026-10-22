#include "ProjectFileViewModel.h"

#include "core/utils/ProjectFileIndex.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJSEngine>
#include <QUrl>

namespace NexusAI {

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
    return QDesktopServices::openUrl(QUrl::fromLocalFile(absolute));
}

} // namespace NexusAI
