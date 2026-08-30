#pragma once

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
public:
    using QObject::QObject;

    Q_INVOKABLE void test(const QString& provider, const QString& baseUrl,
                          const QString& apiKey, const QString& model);

signals:
    void testFinished(bool ok, const QString& message);

private:
    void finish(bool ok, const QString& message);

    QPointer<IAIService> m_service;
    QTimer m_timeout{this};
    bool m_done{true};
};

} // namespace NexusAI
