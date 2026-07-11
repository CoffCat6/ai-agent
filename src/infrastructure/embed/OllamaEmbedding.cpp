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
    QJsonObject body;
    body["model"] = m_model;
    body["prompt"] = text;

    QJsonDocument doc(body);
    QNetworkRequest request;
    request.setUrl(QUrl(m_baseUrl + "/api/embeddings"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = m_nam->post(request, doc.toJson());

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
