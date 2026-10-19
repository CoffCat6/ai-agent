#include "core/mcp/HttpTransport.h"

#include <QJsonDocument>

namespace NexusAI {

namespace {
// docs/24 H1: per-reply accumulation bound (JSON bodies and SSE framing).
constexpr int kMaxResponseBytes = 32 * 1024 * 1024;
} // namespace

HttpTransport::HttpTransport(const Config& config, QObject* parent)
    : McpTransport(parent)
    , m_config(config)
    , m_nam(new QNetworkAccessManager(this)) {}

HttpTransport::~HttpTransport() {
    stop();
}

void HttpTransport::start() {
    // Streamable HTTP is stateless from the transport's point of view: every
    // message is a self-contained POST. Per-request failures surface via
    // errorOccurred and the client's request timeouts.
    setState(State::Connected);
}

void HttpTransport::stop() {
    m_sessionId.clear();
    setState(State::Disconnected);
}

void HttpTransport::sendMessage(const QJsonObject& message) {
    QNetworkRequest request{QUrl(m_config.url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader(QByteArrayLiteral("Accept"),
        QByteArrayLiteral("application/json, text/event-stream"));
    if (!m_sessionId.isEmpty())
        request.setRawHeader("Mcp-Session-Id", m_sessionId.toUtf8());
    for (auto it = m_config.headers.constBegin();
         it != m_config.headers.constEnd(); ++it)
        request.setRawHeader(it.key().toUtf8(), it.value().toString().toUtf8());

    const QByteArray body =
        QJsonDocument(message).toJson(QJsonDocument::Compact);
    auto* reply = m_nam->post(request, body);

    connect(reply, &QNetworkReply::readyRead, this,
            [this, reply]() { onSseChunk(reply, reply->readAll()); });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReplyFinished(reply,
                        reply->isOpen() ? reply->readAll() : QByteArray());
        reply->deleteLater();
        m_sseBuffers.remove(reply);
    });
}

void HttpTransport::onReplyFinished(QNetworkReply* reply,
                                    const QByteArray& trailing) {
    // Session bookkeeping (the server may issue/renew it on any response).
    const QByteArray session =
        reply->rawHeader(QByteArrayLiteral("Mcp-Session-Id"));
    if (!session.isEmpty())
        m_sessionId = QString::fromUtf8(session);

    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    // H1: status 0 with a transport error (DNS / proxy refused / TLS) used to
    // surface as "MCP HTTP error 0". Keep the server error text readable.
    if (status <= 0 && reply->error() != QNetworkReply::NoError) {
        emit errorOccurred(QStringLiteral("MCP HTTP transport error: %1")
                               .arg(reply->errorString()));
        return;
    }
    if (status >= 400) {
        emit errorOccurred(QStringLiteral("MCP HTTP error %1").arg(status));
        return;
    }
    if (status == 202) return;   // notification accepted, no body

    const QByteArray contentType =
        reply->header(QNetworkRequest::ContentTypeHeader).toByteArray();

    // readyRead consumed whatever arrived earlier (into the per-reply
    // buffer); merge it with the trailing read from finished — reading the
    // reply again alone would see an empty body.
    const QByteArray body = m_sseBuffers.value(reply) + trailing;

    if (contentType.contains("text/event-stream")) {
        // Events were dispatched on the fly in onSseChunk; anything left in
        // the merged body is a final, unterminated event (best effort).
        const QByteArray trimmed = body.trimmed();
        if (trimmed.startsWith("data:")) {
            QByteArray payload;
            for (const QByteArray& line : trimmed.split('\n'))
                if (line.trimmed().startsWith("data:"))
                    payload += line.trimmed().mid(5).trimmed();
            if (!payload.isEmpty())
                dispatchPayload(payload);
        }
        return;
    }
    dispatchPayload(body);
}

void HttpTransport::onSseChunk(QNetworkReply* reply, const QByteArray& chunk) {
    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status != 200) return;   // errors handled on finished

    const QByteArray contentType =
        reply->header(QNetworkRequest::ContentTypeHeader).toByteArray();

    // docs/24 H1: bound the per-reply accumulation — a malicious or broken
    // server must not be able to inflate memory without limit inside the
    // request window (the MCP client timeout is the only other bound).
    // Aborting fires finished, where the standard cleanup runs.
    QByteArray& buffer = m_sseBuffers[reply];
    if (buffer.size() + chunk.size() > kMaxResponseBytes) {
        qWarning() << "MCP HTTP response exceeded" << kMaxResponseBytes
                   << "bytes — aborting the reply";
        emit errorOccurred(QStringLiteral(
            "MCP HTTP response exceeded the 32 MiB transport limit"));
        // Fail the transport: the client immediately fails every pending
        // request (an aborted reply would otherwise leave them hanging
        // until their 35s timeouts). A server that answers with 32 MiB
        // responses is broken — no auto-recovery.
        setState(State::Failed);
        reply->abort();
        return;
    }
    buffer += chunk;

    if (!contentType.contains("text/event-stream")) {
        // application/json: accumulate; parsed once on finished (where the
        // buffer and the trailing read are merged).
        return;
    }

    // SSE framing: "data: <payload>" lines, events separated by a blank line.
    QByteArray dataPayload;
    int newline = 0;
    while ((newline = buffer.indexOf('\n')) >= 0) {
        const QByteArray line = buffer.left(newline).trimmed();
        buffer.remove(0, newline + 1);
        if (line.isEmpty()) {
            if (!dataPayload.isEmpty()) {
                dispatchSseEvents(dataPayload);
                dataPayload.clear();
            }
            continue;
        }
        if (line.startsWith("data:"))
            dataPayload += line.mid(5).trimmed();
    }
}

void HttpTransport::dispatchSseEvents(const QByteArray& payload) {
    // One event may carry multiple data lines already joined; here it is one
    // JSON document per event.

    dispatchPayload(payload);
}

} // namespace NexusAI
