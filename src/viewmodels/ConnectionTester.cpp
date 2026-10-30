#include "ConnectionTester.h"

#include "infrastructure/llm/LlmProtocolRegistry.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>

namespace NexusAI {

namespace {

// 端点的错误体不统一：OpenAI 系 errno 在 error.message，Gemini 在 error.message，
// 有些网关只给 message，还有的直接返回字符串。取到哪条算哪条，取不到用回退文案。
QString serverErrorMessage(const QByteArray& body, const QString& fallback) {
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isObject()) {
        const QJsonObject root = doc.object();
        const QJsonValue error = root.value(QStringLiteral("error"));
        if (error.isObject()) {
            const QString message = error.toObject()
                .value(QStringLiteral("message")).toString().trimmed();
            if (!message.isEmpty()) return message;
        } else if (error.isString() && !error.toString().trimmed().isEmpty()) {
            return error.toString().trimmed();
        }
        const QString message = root.value(QStringLiteral("message")).toString().trimmed();
        if (!message.isEmpty()) return message;
    }
    const QString text = QString::fromUtf8(body).trimmed();
    return text.isEmpty() ? fallback : text.left(200);
}

} // namespace

ConnectionTester* ConnectionTester::s_instance = nullptr;

ConnectionTester* ConnectionTester::instance() {
    if (!s_instance) s_instance = new ConnectionTester(nullptr);
    return s_instance;
}

ConnectionTester* ConnectionTester::create(QQmlEngine*, QJSEngine*) {
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}

ConnectionTester::ConnectionTester(QObject* parent) : QObject(parent) {
    // 一次连接，反复 start/stop；每次 fetch 都 connect 会叠出多个 timeout 回调。
    connect(&m_modelsTimeout, &QTimer::timeout, this, [this]() {
        if (!m_modelsBusy) return;
        if (m_modelsReply) {
            m_modelsReply->abort();
            m_modelsReply->deleteLater();
            m_modelsReply.clear();
        }
        m_modelsBusy = false;
        finishModels(false, {}, tr("拉取超时（20 秒）"));
    });
}

void ConnectionTester::test(const QString& apiProtocol, const QString& baseUrl,
                            const QString& apiKey, const QString& model) {
    if (!m_done) return; // a test is already in flight

    IAIService* service = LlmProtocolRegistry::createService(apiProtocol, this);
    if (!service) {
        finish(false, tr("未知的模型接口协议：%1").arg(apiProtocol));
        return;
    }
    m_done = false;
    m_service = service;

    LlmProfileConfig cfg;
    cfg.baseUrl = baseUrl;
    cfg.apiKey = apiKey;
    cfg.model = model;
    service->configure(cfg);

    connect(service, &IAIService::responseReady, this,
            [this, service](const LLMResponse& response) {
        if (m_done) return;
        m_done = true;
        const QString detail = response.content.trimmed().left(60);
        finish(true, detail.isEmpty()
            ? tr("连接成功") : tr("连接成功：%1").arg(detail));
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
        finish(false, tr("连接超时（20 秒）"));
    });
    m_timeout.start(20000);

    QJsonArray messages;
    QJsonObject msg;
    msg["role"] = "user";
    msg["content"] = QStringLiteral("Reply with the single word: OK");
    messages.append(msg);
    service->chatComplete(messages);
}

QUrl ConnectionTester::modelsUrl(const QString& apiProtocol,
                                 const QString& baseUrl) {
    QString base = baseUrl.trimmed();
    // 尾斜杠会拼出 "//v1beta"，与 GeminiService::buildStreamUrl 同样的去重。
    while (base.endsWith(QLatin1Char('/'))) base.chop(1);
    if (base.isEmpty()) return QUrl();

    if (apiProtocol == LlmProtocol::kGeminiGenerate)
        return QUrl(base + QStringLiteral("/v1beta/models"));
    if (apiProtocol == LlmProtocol::kAnthropicMessages)
        return QUrl(base + QStringLiteral("/v1/models"));
    if (apiProtocol == LlmProtocol::kOllamaChat)
        return QUrl(base + QStringLiteral("/api/tags"));
    // OpenAI 系的 baseUrl 自带 /v1（默认值与占位文案都是这样写的）。
    return QUrl(base + QStringLiteral("/models"));
}

QStringList ConnectionTester::parseModelList(const QString& apiProtocol,
                                             const QByteArray& body,
                                             QString* error) {
    const auto fail = [error](const QString& message) {
        if (error) *error = message;
        return QStringList{};
    };
    if (error) error->clear();

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return fail(tr("端点返回的不是 JSON 对象。"));

    const QJsonObject root = doc.object();
    QStringList names;
    const auto add = [&names](const QString& raw) {
        QString name = raw.trimmed();
        if (name.startsWith(QStringLiteral("models/")))
            name = name.mid(QStringLiteral("models/").length());
        if (name.isEmpty() || names.contains(name)) return;
        names.append(name);
    };

    if (apiProtocol == LlmProtocol::kOllamaChat) {
        // Ollama /api/tags: {"models":[{"name":"qwen2.5:7b",...}]}
        for (const QJsonValue& value : root.value(QStringLiteral("models")).toArray()) {
            const QJsonObject item = value.toObject();
            add(item.value(QStringLiteral("name")).toString(
                item.value(QStringLiteral("model")).toString()));
        }
    } else if (apiProtocol == LlmProtocol::kGeminiGenerate) {
        // Gemini /v1beta/models: {"models":[{"name":"models/gemini-2.5-flash",
        //   "supportedGenerationMethods":["generateContent",...]}]}
        // 同一份响应里还有 embedding 模型，按 supportedGenerationMethods 过滤。
        for (const QJsonValue& value : root.value(QStringLiteral("models")).toArray()) {
            const QJsonObject item = value.toObject();
            if (item.contains(QStringLiteral("supportedGenerationMethods"))) {
                const QJsonArray methods =
                    item.value(QStringLiteral("supportedGenerationMethods")).toArray();
                bool chatCapable = false;
                for (const QJsonValue& method : methods) {
                    if (method.toString() == QStringLiteral("generateContent")) {
                        chatCapable = true;
                        break;
                    }
                }
                if (!chatCapable) continue;
            }
            add(item.value(QStringLiteral("name")).toString());
        }
    } else {
        // OpenAI 兼容 /models（以及 Anthropic 的同形响应）：
        // {"data":[{"id":"gpt-4o"},...]}；少数网关用 models[] 或纯字符串数组。
        QJsonArray array = root.value(QStringLiteral("data")).toArray();
        if (array.isEmpty()) array = root.value(QStringLiteral("models")).toArray();
        for (const QJsonValue& value : array) {
            if (value.isString()) {
                add(value.toString());
                continue;
            }
            const QJsonObject item = value.toObject();
            add(item.value(QStringLiteral("id")).toString(
                item.value(QStringLiteral("name")).toString()));
        }
    }

    if (names.isEmpty())
        return fail(tr("端点没有返回任何模型。"));
    return names;
}

void ConnectionTester::fetchModels(const QString& apiProtocol,
                                   const QString& baseUrl,
                                   const QString& apiKey) {
    if (m_modelsBusy) return; // 一次只发一个列举请求

    if (LlmProtocolRegistry::find(apiProtocol) == nullptr) {
        finishModels(false, {},
                     tr("未知的模型接口协议：%1").arg(apiProtocol));
        return;
    }
    QUrl url = modelsUrl(apiProtocol, baseUrl);
    if (!url.isValid() || url.host().isEmpty()) {
        finishModels(false, {}, tr("服务地址为空或无效，请先填写端点。"));
        return;
    }

    const QString key = apiKey.trimmed();
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    if (!key.isEmpty()) {
        if (apiProtocol == LlmProtocol::kGeminiGenerate) {
            QUrlQuery query(url);
            query.addQueryItem(QStringLiteral("key"), key);
            url.setQuery(query);
            request.setUrl(url);
        } else if (apiProtocol == LlmProtocol::kAnthropicMessages) {
            request.setRawHeader("x-api-key", key.toUtf8());
            request.setRawHeader("anthropic-version", "2023-06-01");
        } else if (apiProtocol != LlmProtocol::kOllamaChat) {
            request.setRawHeader("Authorization",
                                 QByteArray("Bearer ") + key.toUtf8());
        }
    }

    if (!m_net) m_net = new QNetworkAccessManager(this);
    m_modelsBusy = true;
    QNetworkReply* reply = m_net->get(request);
    m_modelsReply = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply, apiProtocol]() {
        if (!m_modelsBusy || m_modelsReply != reply) {
            reply->deleteLater();
            return;
        }
        m_modelsReply.clear();
        m_modelsBusy = false;
        m_modelsTimeout.stop();

        const int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const QString replyError = reply->errorString();
        reply->deleteLater();
        // status == 0 → 没拿到响应（DNS / 拒绝连接 / TLS）。
        if (status == 0) {
            finishModels(false, {}, tr("请求失败：%1").arg(replyError));
            return;
        }
        if (status >= 400) {
            finishModels(false, {},
                         tr("HTTP %1：%2").arg(status).arg(
                             serverErrorMessage(body, replyError)));
            return;
        }
        QString parseError;
        const QStringList names = parseModelList(apiProtocol, body, &parseError);
        if (names.isEmpty()) {
            finishModels(false, {}, parseError.isEmpty()
                ? tr("端点没有返回任何模型。") : parseError);
            return;
        }
        finishModels(true, names, tr("拉取到 %1 个模型。").arg(names.size()));
    });

    m_modelsTimeout.start(20000);
}

void ConnectionTester::finish(bool ok, const QString& message) {
    m_timeout.stop();
    emit testFinished(ok, message);
}

void ConnectionTester::finishModels(bool ok, const QStringList& models,
                                    const QString& message) {
    m_modelsTimeout.stop();
    emit modelsFetched(ok, models, message);
}

} // namespace NexusAI
