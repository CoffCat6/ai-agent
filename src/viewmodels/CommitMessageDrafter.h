#pragma once

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace NexusAI {

class IAIService;

// Everything a draft request needs from the outside world: the active model
// profile (including its key, which never round-trips through QML) and the
// user's local-only switch.
struct DraftContext {
    QVariantMap profile;
    bool localOnlyMode{false};
};

// One-shot "draft a commit message from the staged diff" request. Shaped on
// purpose like ConnectionTester: build a throwaway service for the active
// protocol, send one prompt, take the first response, then cancel and release
// it. It never touches a chat session, the message table, or git itself — the
// panel decides what to do with the text, and committing stays behind its own
// confirmation dialog.
class CommitMessageDrafter : public QObject {
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(CommitDrafter)

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    // Staged-diff budget for the prompt, matching the 64K ceiling the tool
    // layer gives checkpoint diffs, so a huge stage cannot blow up the request.
    static constexpr int kDiffBudgetChars = 64 * 1024;
    static constexpr int kTimeoutMs = 45000;
    static constexpr int kMaxBodyLines = 20;
    // A remote profile under localOnlyMode must not carry project content off
    // the machine; only local protocols (ollama and friends) are allowed there.
    static bool blockedByLocalOnly(bool localOnlyMode, bool profileIsLocal);
    // Cut to `budget` characters on a line boundary, marking the cut.
    static QString truncateDiff(const QString& diff, int budget);
    static QString buildPrompt(const QString& stagedDiff,
                               const QStringList& recentSubjects);
    // Strip code fences, wrapping quotes and stray bullets, collapse blank
    // lines and cap the length, so what lands in the composer is a message.
    static QString cleanDraft(const QString& raw);

    explicit CommitMessageDrafter(QObject* parent = nullptr);

    static CommitMessageDrafter* instance();
    static CommitMessageDrafter* create(QQmlEngine*, QJSEngine*);

    bool busy() const { return !m_done; }

    // Where the profile and the local-only switch come from. Production reads
    // SettingsService; tests inject a profile that fails before any request is
    // made. It is a *class* seam on purpose: a QML singleton may be held by the
    // engine rather than by instance(), so an instance member would silently
    // apply to the wrong object - and a test that missed would then send a real
    // request to the user's configured endpoint.
    static void setContextProvider(std::function<DraftContext()> provider);

    Q_INVOKABLE void draft(const QString& stagedDiff,
                           const QVariantList& recentSubjects);
    Q_INVOKABLE void cancel();

signals:
    void busyChanged();
    void drafted(bool ok, const QString& text, const QString& error);

private:
    void finish(bool ok, const QString& text, const QString& error);

    QPointer<IAIService> m_service;
    QTimer m_timeout;
    bool m_done{true};

    static CommitMessageDrafter* s_instance;
};

} // namespace NexusAI
