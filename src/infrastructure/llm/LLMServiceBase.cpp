#include "LLMServiceBase.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QDebug>

namespace NexusAI {

LLMServiceBase::LLMServiceBase(QObject* parent)
    : IAIService(parent), m_nam(new QNetworkAccessManager(this)) {}

LLMServiceBase::~LLMServiceBase() {
    if (m_currentReply) {
        m_currentReply->abort();
    }
}

void LLMServiceBase::cancel() {
    if (m_currentReply) {
        m_currentReply->abort(); // finished handler performs cleanup
    }
}

void LLMServiceBase::resetStreamState() {
    m_lineBuffer.clear();
    m_rawBody.clear();
    m_content.clear();
    m_tokensUsed = 0;
    m_doneReceived = false;
    m_responseEmitted = false;
    m_errorEmitted = false;
}

void LLMServiceBase::startReply(QNetworkRequest& request, const QByteArray& body) {
    QNetworkReply* reply = m_nam->post(request, body);
    m_currentReply = reply;
    const quint64 generation = ++m_requestGeneration;

    connect(reply, &QNetworkReply::readyRead, this, [this, reply, generation]() {
        if (generation != m_requestGeneration || m_currentReply != reply) return;
        onReadyRead();
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() {
        if (generation != m_requestGeneration) {
            // Superseded request: its bytes must never touch the current
            // request's stream state, and it must not emit either.
            reply->deleteLater();
            return;
        }
        if (m_currentReply == reply) m_currentReply = nullptr;
        handleReplyFinished(reply);
    });
}

void LLMServiceBase::onReadyRead() {
    const QByteArray data = m_currentReply->readAll();
    m_rawBody.append(data);
    m_lineBuffer.append(data);
    processIncoming();
}

void LLMServiceBase::processIncoming() {
    int newlineIdx;
    while ((newlineIdx = m_lineBuffer.indexOf('\n')) >= 0) {
        QByteArray line = m_lineBuffer.left(newlineIdx);
        m_lineBuffer.remove(0, newlineIdx + 1);
        processLine(line.trimmed());
    }
}

void LLMServiceBase::handleReplyFinished(QNetworkReply* reply) {
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError err = reply->error();
    const QString errString = reply->errorString();

    const QByteArray rest = reply->readAll();
    reply->deleteLater();

    if (!rest.isEmpty()) {
        m_rawBody.append(rest);
        m_lineBuffer.append(rest);
        processIncoming();
        // Servers may end the body without a trailing newline; the leftover
        // is a complete final line (last SSE event / NDJSON record) that
        // processIncoming's newline split cannot reach.
        if (!m_lineBuffer.isEmpty()) {
            const QByteArray tail = m_lineBuffer;
            m_lineBuffer.clear();
            processLine(tail.trimmed());
        }
    }

    if (m_responseEmitted || m_errorEmitted) return;

    // Assemble the streamed response when stream data was received
    if (streamComplete()) {
        m_responseEmitted = true;
        emit responseReady(streamResult());
        return;
    }

    // No stream data: error body, plain non-stream JSON, or network failure
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(m_rawBody, &parseError);
    if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
        const QJsonObject json = doc.object();

        const QString apiError = extractApiError(json);
        if (!apiError.isEmpty()) {
            emit errorOccurred(QStringLiteral("%1 error (HTTP %2): %3")
                .arg(providerLabel(), QString::number(status), apiError));
            return;
        }

        if (err == QNetworkReply::NoError && isCompleteBody(json)) {
            // Server ignored the stream flag - treat as regular response
            LLMResponse response = parseBody(json);
            if (response.success) {
                m_responseEmitted = true;
                emit responseReady(response);
            } else {
                emit errorOccurred(response.error.isEmpty()
                    ? QStringLiteral("Empty response from %1").arg(providerLabel())
                    : response.error);
            }
            return;
        }
    }

    if (err != QNetworkReply::NoError) {
        emit errorOccurred(QStringLiteral("Network error: %1").arg(errString));
        return;
    }

    emit errorOccurred(QStringLiteral("Empty or invalid response from %1")
                           .arg(providerLabel()));
}

} // namespace NexusAI
