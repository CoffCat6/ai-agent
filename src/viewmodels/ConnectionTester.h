#pragma once

#include <QQmlEngine>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include "infrastructure/llm/IAIService.h"

namespace NexusAI {

// Fires a minimal chat completion against a provider configuration from the
// settings dialog. Creates a throwaway IAIService so the live agent pipeline
// is never touched. Exactly one testFinished() is emitted per test() call,
// after which the temporary service is destroyed.
class ConnectionTester : public QObject {
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(ConnectionTester)
public:
    // No default constructor on purpose (see AgentViewModel: the engine
    // would prefer Constructor over the create() factory).
    explicit ConnectionTester(QObject* parent);

    // QML singleton plumbing (same pattern as AgentViewModel)
    static ConnectionTester* instance();
    static ConnectionTester* create(QQmlEngine*, QJSEngine*);

    Q_INVOKABLE void test(const QString& provider, const QString& baseUrl,
                          const QString& apiKey, const QString& model);

signals:
    void testFinished(bool ok, const QString& message);

private:
    void finish(bool ok, const QString& message);

    QPointer<IAIService> m_service;
    QTimer m_timeout{this};
    bool m_done{true};

    static ConnectionTester* s_instance;
};

} // namespace NexusAI
