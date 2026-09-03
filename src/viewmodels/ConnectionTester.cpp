#include "ConnectionTester.h"

#include "infrastructure/llm/OpenAIService.h"
#include "infrastructure/llm/OllamaService.h"
#include "infrastructure/llm/AnthropicService.h"

#include <QTimer>

namespace NexusAI {

ConnectionTester* ConnectionTester::s_instance = nullptr;

ConnectionTester* ConnectionTester::instance() {
    if (!s_instance) s_instance = new ConnectionTester(nullptr);
    return s_instance;
}

ConnectionTester* ConnectionTester::create(QQmlEngine*, QJSEngine*) {
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}

ConnectionTester::ConnectionTester(QObject* parent) : QObject(parent) {}

void ConnectionTester::test(const QString& provider, const QString& baseUrl,
                            const QString& apiKey, const QString& model) {
    if (!m_done) return; // a test is already in flight
    m_done = false;

    IAIService* service;
    if (provider == QLatin1String("ollama")) {
        service = new OllamaService(this);
    } else if (provider == QLatin1String("anthropic")) {
        service = new AnthropicService(this);
    } else {
        service = new OpenAIService(this);
    }
    m_service = service;

    service->setBaseUrl(baseUrl);
    service->setApiKey(apiKey);
    service->setModel(model);

    connect(service, &IAIService::responseReady, this,
            [this, service](const LLMResponse& response) {
        if (m_done) return;
        m_done = true;
        const QString detail = response.content.trimmed().left(60);
        finish(true, detail.isEmpty()
            ? QStringLiteral("连接成功") : QStringLiteral("连接成功：%1").arg(detail));
        service->deleteLater();
    });
    connect(service, &IAIService::errorOccurred, this,
            [this, service](const QString& error) {
        if (m_done) return;
        m_done = true;
        finish(false, error);
        service->deleteLater();
    });

    // Bound the wait regardless of the service's own transfer timeout
    m_timeout.stop();
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this]() {
        if (m_done) return;
        m_done = true;
        if (m_service) {
            m_service->cancel();
            m_service->deleteLater();
            m_service.clear();
        }
        finish(false, QStringLiteral("连接超时（20 秒）"));
    });
    m_timeout.start(20000);

    QJsonArray messages;
    QJsonObject msg;
    msg["role"] = "user";
    msg["content"] = QStringLiteral("Reply with the single word: OK");
    messages.append(msg);
    service->chatComplete(messages);
}

void ConnectionTester::finish(bool ok, const QString& message) {
    m_timeout.stop();
    emit testFinished(ok, message);
}

} // namespace NexusAI
