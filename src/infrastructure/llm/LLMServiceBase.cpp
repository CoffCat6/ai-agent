#include "LLMServiceBase.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QDebug>

namespace NexusAI {

LLMServiceBase::LLMServiceBase(QObject* parent)
    : IAIService(parent), m_nam(new QNetworkAccessManager(this)) {}

LLMServiceBase::~LLMServiceBase() {
    if (m_current && m_current->reply) {
        m_current->reply->abort();
    }
}

void LLMServiceBase::cancel() {
    if (m_current) {
        m_current->cancelled = true;
        if (m_current->reply)
            m_current->reply->abort(); // finished handler performs cleanup
    }
}

void LLMServiceBase::startReply(QNetworkRequest& request, const QByteArray& body) {
    auto ctx = LLMRequestContextPtr::create();
    ctx->requestId = ++m_lastRequestId;
    ctx->provider = createRequestState();
    QNetworkReply* reply = m_nam->post(request, body);
    ctx->reply = reply;
    m_current = ctx;
    emit requestStarted(ctx->requestId);

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
