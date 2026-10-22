#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <QHash>

namespace NexusAI {

// QML facade for the sidebar's project file browser.
//
// The browser reads one directory level at a time instead of indexing the
// whole tree: a project can hold tens of thousands of files, and building a
// tree from a flat list is O(n) in JS on the QML side. One level also makes
// empty directories visible, which a flat list cannot express.
//
// Filtering lives in ProjectFileIndex::listDir, not here - the browser and
// the '@' indexer must not disagree about what a project contains, and that
// module already owns the ignore/sensitive rules.
//
// Read-only by design: nothing here writes, creates, renames or deletes.
class ProjectFileViewModel : public QObject {
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(ProjectFileVM)

    // Project root. Empty means "no project"; every call short-circuits then,
    // so a local chat never touches the filesystem.
    Q_PROPERTY(QString root READ root WRITE setRoot NOTIFY rootChanged)

public:
    explicit ProjectFileViewModel(QObject* parent = nullptr);
    ~ProjectFileViewModel() override;

    static ProjectFileViewModel* instance();
    static ProjectFileViewModel* create(QQmlEngine*, QJSEngine*);

    QString root() const { return m_root; }
    void setRoot(const QString& root);

    // One directory level: [{name, relPath, isDir}], directories first.
    // Cached per (root, relPath) until the root changes or clearCache() runs.
    // error is "" on success, else "no-project" / "outside" / "missing" /
    // "unreadable" - the UI shows it rather than an empty list, so "no files"
    // and "could not look" stay distinguishable.
    Q_INVOKABLE QVariantList children(const QString& relPath);
    // {text, bytes, truncated, binary, error} - see ProjectFileIndex::readFilePreview.
    Q_INVOKABLE QVariantMap preview(const QString& relPath, int maxBytes = 512 * 1024);
    // Opens with the OS handler. Returns false when the path is invalid or
    // the platform refused; the caller reports that instead of failing quietly.
    Q_INVOKABLE bool openInDefaultApp(const QString& relPath);
    // Absolute path for the given relPath, empty when it would escape root.
    // Exposed mainly so a test (and a future "copy path" button) can assert
    // the containment rule without opening anything.
    Q_INVOKABLE QString absolutePath(const QString& relPath) const;

    // Drop every cached listing. Called on root change; QML calls it for an
    // explicit refresh (the browser has no filesystem watcher by design).
    Q_INVOKABLE void clearCache();

signals:
    void rootChanged();

private:
    QString m_root;
    // Key: relPath (root is fixed per cache lifetime). QVariantList because
    // it is handed to QML as-is.
    QHash<QString, QVariantList> m_cache;

    static ProjectFileViewModel* s_instance;
};

} // namespace NexusAI
