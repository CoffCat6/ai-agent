#include "LLMServiceBase.h"
#include "core/utils/Activity.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QTimer>
#include <QDebug>

namespace NexusAI {

namespace {
bool isTransientNetworkError(QNetworkReply::NetworkError err) {
    switch (err) {
    case QNetworkReply::TimeoutError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::UnknownNetworkError:
        return true;
    default:
        return false;
    }
}
} // namespace

LLMServiceBase::LLMServiceBase(QObject* parent)
    : IAIService(parent), m_nam(new QNetworkAccessManager(this)) {}

LLMServiceBase::~LLMServiceBase() {
    if (m_current && m_current->reply) {
        QNetworkReply* reply = m_current->reply;
        reply->abort();
        // QNAM 的中止经内部 HTTP 线程异步完成：abort() 只是发起，finished
        // 可能要等事件循环投递。服务经常在池线程上用完即删（子代理/嵌套
        // 服务），那里没有常驻事件循环——若不等收尾就析构，reply 会作为
        // m_nam 子对象被销毁，而 HTTP 线程的收尾事件随后踩进已释放内存
        // （真实栈上表现为 SIGSEGV / 访问违例）。这里自旋等 finished 或
        // 对象销毁真正送达，1s 兜底防止异常实现挂死线程。
        QEventLoop drain;
        connect(reply, &QNetworkReply::finished, &drain, &QEventLoop::quit);
        connect(reply, &QObject::destroyed, &drain, &QEventLoop::quit);
        QTimer::singleShot(1000, &drain, &QEventLoop::quit);
        if (reply->isRunning()) drain.exec();
    }
    // 回收 abort 触发的 reply->deleteLater()：延迟删除同样依赖事件循环，
    // 池线程上不会自行处理。只清 DeferredDelete 队列，不动其它排队事件。
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void LLMServiceBase::cancel() {
    if (m_current) {
        m_current->cancelled = true;
        if (m_current->reply)
            m_current->reply->abort(); // finished handler performs cleanup
    }
}

void LLMServiceBase::configure(const LlmProfileConfig& cfg) {
    m_baseUrl = cfg.baseUrl;
    m_apiKey = cfg.apiKey;
    m_model = cfg.model;
    applyProfileParams(cfg);
}

void LLMServiceBase::startReply(QNetworkRequest& request, const QByteArray& body) {
    auto ctx = LLMRequestContextPtr::create();
    ctx->requestId = ++m_lastRequestId;
    ctx->provider = createRequestState();
    ctx->request = request;
    ctx->requestBody = body;
    m_current = ctx;
    emit requestStarted(ctx->requestId);
    postReply(ctx);
}

void LLMServiceBase::postReply(const LLMRequestContextPtr& ctx) {
    QNetworkReply* reply = m_nam->post(ctx->request, ctx->requestBody);
    ctx->reply = reply;
    ctx->startMs = m_elapsed.isValid() ? m_elapsed.elapsed() : 0;
    if (!m_elapsed.isValid()) m_elapsed.start();
    m_current = ctx;

    // OBS/Activity: network start metadata only (never the body) — the
    // response-side outcome is logged in handleReplyFinished.
    Activity::report(Activity::Kind::Network,
                     QStringLiteral("llm request start %1 attempt %2 body %3 B")
                         .arg(ctx->request.url().toString())
                         .arg(ctx->attempt)
                         .arg(ctx->requestBody.size()),
                     QString::number(ctx->requestId));

    connect(reply, &QNetworkReply::readyRead, this, [this, ctx]() {
        if (ctx != m_current || ctx->cancelled) return;
        onReadyRead(ctx);
    });

    connect(reply, &QNetworkReply::finished, this, [this, ctx]() {
        if (ctx != m_current) {
            // Superseded request: its bytes must never touch the current
            // request's context, and it must not emit either.
            if (ctx->reply) ctx->reply->deleteLater();
            return;
        }
        m_current.clear();
        handleReplyFinished(ctx);
    });
}

void LLMServiceBase::onReadyRead(const LLMRequestContextPtr& ctx) {
    const QByteArray data = ctx->reply->readAll();
    ctx->rawBody.append(data);
    ctx->lineBuffer.append(data);
    processIncoming(ctx);
}

void LLMServiceBase::processIncoming(const LLMRequestContextPtr& ctx) {
    int newlineIdx;
    while ((newlineIdx = ctx->lineBuffer.indexOf('\n')) >= 0) {
        QByteArray line = ctx->lineBuffer.left(newlineIdx);
        ctx->lineBuffer.remove(0, newlineIdx + 1);
        processLine(*ctx, line.trimmed());
    }
}

void LLMServiceBase::handleReplyFinished(const LLMRequestContextPtr& ctx) {
    QNetworkReply* reply = ctx->reply;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError err = reply->error();
    const QString errString = reply->errorString();

    const QByteArray rest = reply->readAll();
    reply->deleteLater();
    ctx->reply = nullptr;

    if (!rest.isEmpty()) {
        ctx->rawBody.append(rest);
        ctx->lineBuffer.append(rest);
        processIncoming(ctx);
        // Servers may end the body without a trailing newline; the leftover
        // is a complete final line (last SSE event / NDJSON record) that
        // processIncoming's newline split cannot reach.
        if (!ctx->lineBuffer.isEmpty()) {
            const QByteArray tail = ctx->lineBuffer;
            ctx->lineBuffer.clear();
            processLine(*ctx, tail.trimmed());
        }
    }

    if (ctx->responseEmitted || ctx->errorEmitted) return;

    // REL-1001: one transparent retry for transient failures (timeout /
    // connection reset / HTTP 5xx / 429). Cancelled requests, already-retried
    // attempts and anything that streamed output are not retried.
    const bool transientStatus =
        (status >= 500 || status == 429) && err != QNetworkReply::OperationCanceledError;
    // docs/25 A3: align the condition with the comment above — requests
    // that already streamed any text (content or reasoning) are NOT
    // retried; resetStreamState() would wipe what the UI already showed
    // (flash-back) and re-bill the request.
    if (ctx->attempt == 0 && !ctx->cancelled
            && ctx->content.isEmpty() && ctx->reasoning.isEmpty()
            && (isTransientNetworkError(err) || transientStatus)) {
        ctx->attempt = 1;
        ctx->resetStreamState();
        qInfo() << providerLabel() << "transient failure (HTTP" << status << err
                << ") - retrying request" << ctx->requestId;
        postReply(ctx);
        return;
    }

    // Network outcome metadata (privacy: never the response body). Persisted
    // via the usage sink so network health is traceable across restarts.
    {
        const qint64 durationMs = m_elapsed.isValid()
            ? m_elapsed.elapsed() - ctx->startMs : -1;
        const QString errorSummary = err == QNetworkReply::NoError
            ? QString() : errString.left(120);
        Activity::usage(Activity::Kind::Network,
                        QStringLiteral("llm_result"),
                        ctx->request.url().toString(), ctx->requestId,
                        QStringLiteral("status=%1 duration_ms=%2 bytes=%3 attempt=%4%5")
                            .arg(status)
                            .arg(durationMs)
                            .arg(ctx->rawBody.size())
                            .arg(ctx->attempt)
                            .arg(errorSummary.isEmpty()
                                     ? QString()
                                     : QStringLiteral(" err=%1").arg(errorSummary)));
    }

    // Assemble the streamed response when stream data was received
    if (streamComplete(*ctx)) {
        ctx->responseEmitted = true;
        LLMResponse response = streamResult(*ctx);
        response.requestId = ctx->requestId;
        emit responseReady(response);
        return;
    }

    // No stream data: error body, plain non-stream JSON, or network failure
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(ctx->rawBody, &parseError);
    if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
        const QJsonObject json = doc.object();

        const QString apiError = extractApiError(*ctx, json);
        if (!apiError.isEmpty()) {
            ctx->errorEmitted = true;
            emit errorOccurred(QStringLiteral("%1 error (HTTP %2): %3")
                .arg(providerLabel(), QString::number(status), apiError));
            return;
        }

        if (err == QNetworkReply::NoError && isCompleteBody(json)) {
            // Server ignored the stream flag - treat as regular response
            LLMResponse response = parseBody(json);
            if (response.success) {
                ctx->responseEmitted = true;
                response.requestId = ctx->requestId;
                emit responseReady(response);
            } else {
                ctx->errorEmitted = true;
                emit errorOccurred(response.error.isEmpty()
                    ? QStringLiteral("Empty response from %1").arg(providerLabel())
                    : response.error);
            }
            return;
        }
    }

    if (err != QNetworkReply::NoError) {
        ctx->errorEmitted = true;
        emit errorOccurred(QStringLiteral("Network error: %1").arg(errString));
        return;
    }

    ctx->errorEmitted = true;
    emit errorOccurred(QStringLiteral("Empty or invalid response from %1")
                           .arg(providerLabel()));
}

} // namespace NexusAI
