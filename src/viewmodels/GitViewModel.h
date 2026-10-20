#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace NexusAI {

// Async QML facade for GitService. Every git call runs on the global thread
// pool (QProcess must never block the GUI thread); results come back through
// per-request signals carrying an opaque request id, so a dialog can ignore
// answers for a path it no longer shows.
class GitViewModel : public QObject {
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(GitVM)

    Q_PROPERTY(QVariantMap repoInfo READ repoInfo NOTIFY repoInfoChanged)
    Q_PROPERTY(bool repoInfoLoading READ repoInfoLoading
                   NOTIFY repoInfoLoadingChanged)

public:
    explicit GitViewModel(QObject* parent = nullptr);

    static GitViewModel* instance();
    static GitViewModel* create(QQmlEngine*, QJSEngine*);

    QVariantMap repoInfo() const { return m_repoInfo; }
    bool repoInfoLoading() const { return m_repoInfoLoading; }

    Q_INVOKABLE void refreshRepoInfo();
    Q_INVOKABLE QString requestFileHistory(const QString& path, int limit = 20);
    Q_INVOKABLE QString requestCommitDiff(const QString& sha,
                                          const QString& path = QString());
    Q_INVOKABLE QString requestWorkingDiff(const QString& path = QString(),
                                           bool staged = false);

signals:
    void repoInfoChanged();
    void repoInfoLoadingChanged();

    void fileHistoryReady(const QString& requestId, const QString& path,
                          const QVariantList& commits, const QString& error);
    void commitDiffReady(const QString& requestId, const QString& sha,
                         const QString& path, const QString& diff,
                         bool truncated, const QString& error);
    void workingDiffReady(const QString& requestId, const QString& path,
                          bool staged, const QString& diff,
                          bool truncated, const QString& error);

private:
    QString nextRequestId();
    void setRepoInfoLoading(bool loading);

    QVariantMap m_repoInfo;
    bool m_repoInfoLoading{false};
    int m_repoGeneration{0};
    quint64 m_nextRequestId{0};

    static GitViewModel* s_instance;
};

} // namespace NexusAI
