#include "OllamaEmbedding.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEventLoop>
#include <QDebug>

namespace NexusAI {

OllamaEmbedding::OllamaEmbedding(QObject* parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this)) {}

QVector<float> OllamaEmbedding::embed(const QString& text) {
    // Stack-local NAM living on the calling thread: embed() may run on a
    // worker thread (RAG tool execution, document import) where the
    // main-thread m_nam would be cross-thread QObject misuse.
    QNetworkAccessManager nam;

    QJsonObject body;
    body["model"] = m_model;
    body["prompt"] = text;

    QJsonDocument doc(body);
    QNetworkRequest request;
    request.setUrl(QUrl(m_baseUrl + "/api/embeddings"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(30000);

    QNetworkReply* reply = nam.post(request, doc.toJson());

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    QVector<float> embedding;

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "Embedding request failed:" << reply->errorString();
        reply->deleteLater();
        return embedding;
    }

    QByteArray responseData = reply->readAll();
    reply->deleteLater();

    QJsonParseError parseError;
    QJsonDocument responseDoc = QJsonDocument::fromJson(responseData, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "Failed to parse embedding response:" << parseError.errorString();
        return embedding;
    }

    QJsonObject responseObj = responseDoc.object();
    QJsonArray embeddingArray = responseObj.value("embedding").toArray();

    embedding.reserve(embeddingArray.size());
    for (const auto& val : embeddingArray) {
        embedding.append(static_cast<float>(val.toDouble()));
    }

    return embedding;
}

QVector<QVector<float>> OllamaEmbedding::embedBatch(const QStringList& texts) {
    if (texts.isEmpty()) return {};

    // Stack-local NAM on the calling thread (see embed()).
    QNetworkAccessManager nam;

    QJsonObject body;
    body["model"] = m_model;
    QJsonArray input;
    for (const QString& t : texts)
        input.append(t);
    body["input"] = input;

    QNetworkRequest request;
    request.setUrl(QUrl(m_baseUrl + "/api/embed"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(120000);

    QNetworkReply* reply = nam.post(request, QJsonDocument(body).toJson());

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    QVector<QVector<float>> embeddings;
    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "Batch embedding request failed:" << reply->errorString();
        reply->deleteLater();
        return embeddings;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    reply->deleteLater();

    const QJsonArray array = doc.object().value("embeddings").toArray();
    if (array.size() != texts.size()) {
        qWarning() << "Batch embedding response size mismatch:"
                   << array.size() << "of" << texts.size();
        return embeddings;
    }

    embeddings.reserve(array.size());
    for (const auto& item : array) {
        const QJsonArray vec = item.toArray();
        if (vec.isEmpty()) return {};   // any hole -> caller falls back wholesale
        QVector<float> embedding;
        embedding.reserve(vec.size());
        for (const auto& val : vec)
            embedding.append(static_cast<float>(val.toDouble()));
        embeddings.append(embedding);
    }
    return embeddings;
}

void OllamaEmbedding::embedAsync(const QString& text) {
    QJsonObject body;
    body["model"] = m_model;
    body["prompt"] = text;

    QJsonDocument doc(body);
    QNetworkRequest request;
    request.setUrl(QUrl(m_baseUrl + "/api/embeddings"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = m_nam->post(request, doc.toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(reply->errorString());
            reply->deleteLater();
            return;
        }

        QByteArray responseData = reply->readAll();
        reply->deleteLater();

        QJsonParseError parseError;
        QJsonDocument responseDoc = QJsonDocument::fromJson(responseData, &parseError);

        if (parseError.error != QJsonParseError::NoError) {
            emit errorOccurred("Failed to parse embedding response");
            return;
        }

        QJsonObject responseObj = responseDoc.object();
        QJsonArray embeddingArray = responseObj.value("embedding").toArray();

        QVector<float> embedding;
        embedding.reserve(embeddingArray.size());
        for (const auto& val : embeddingArray) {
            embedding.append(static_cast<float>(val.toDouble()));
        }

        emit embeddingReady(embedding);
    });
}

} // namespace NexusAI
