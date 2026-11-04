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
// 析构时等待网络收尾的总时间预算。按“总预算”而不是“每个 reply 一份”
// 计算，保证多个在飞请求叠加也不会把 GUI 线程卡超过 2s。
constexpr int kShutdownWaitBudgetMs = 2000;

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
    // (1) 先置取消标志：本次析构前已排队的 readyRead/finished 回调一旦被
    // 投递，就会看到 cancelled 而不再解析/发射。随后断开连接，让它们连
    // 半析构对象都不再进入。
    if (m_current)
        m_current->cancelled = true;

    // 收集 QNAM 下所有仍在跑的 reply —— 不只是 m_current->reply。被新请求
    // 顶替的旧 reply 同样可能还在飞：旧实现只 abort m_current，把旧 reply
    // 留给 ~QNAM 在 HTTP 后端收尾时同步释放，正是 use-after-free 的来源。
    const auto collectRunning = [this]() {
        QList<QNetworkReply*> running;
        if (!m_nam) return running;
        const QList<QNetworkReply*> replies = m_nam->findChildren<QNetworkReply*>();
        for (QNetworkReply* r : replies) {
            if (r && r->isRunning()) running.append(r);
        }
        return running;
    };

    // (2) 断开网络/解析连接后再 abort()。abort 只是发起中止，finished 可能
    // 稍后投递；断开 this 连接确保它不再调用 handleReplyFinished（那里会
    // emit 信号、访问已析构成员），也不会再解析流数据。
    if (m_nam) {
        disconnect(m_nam, nullptr, this, nullptr);
        for (QNetworkReply* r : collectRunning()) {
            disconnect(r, nullptr, this, nullptr);
            r->abort();
        }
    }

    // (3) 限时等待活动请求归零（finished 或对象销毁送达）。总预算 2s：
    // 每轮都用“剩余预算”做兜底，多个请求不会叠加等待时间，GUI 不卡更久。
    QElapsedTimer wait;
    wait.start();
    while (int(wait.elapsed()) < kShutdownWaitBudgetMs) {
        const QList<QNetworkReply*> running = collectRunning();
        if (running.isEmpty()) break;
        const int remaining = kShutdownWaitBudgetMs - int(wait.elapsed());
        QEventLoop drain;
        for (QNetworkReply* r : running) {
            connect(r, &QNetworkReply::finished, &drain, &QEventLoop::quit);
            connect(r, &QObject::destroyed, &drain, &QEventLoop::quit);
        }
        // drain 析构时上述连接自动断开；timer 以 drain 为 context 一并取消。
        QTimer::singleShot(remaining, &drain, &QEventLoop::quit);
        drain.exec();
    }

    if (!collectRunning().isEmpty()) {
        // (4) 超时兜底：仍有 reply 没有收尾。绝不能让它作为 m_nam 的子对象
        // 在 ~QObject 里被同步 delete —— HTTP 后端的收尾事件随后会踩已释放
        // 的对象。解除父子关系并 deleteLater 到其所属线程，由该线程的事件
        // 循环安全回收；若该线程没有事件循环（池线程）则宁可泄漏也不
        // use-after-free。QNAM 同理延后清理。
        for (QNetworkReply* r : collectRunning()) {
            r->setParent(nullptr);
            r->deleteLater();
        }
        if (m_nam) {
            m_nam->setParent(nullptr);
            m_nam->deleteLater();
        }
    } else {
        // 收尾已完成：回收 abort/finished 路径排队的 DeferredDelete。延迟
        // 删除同样依赖事件循环，池线程上不会自行处理。
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
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
