#include "OllamaEmbedding.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEventLoop>
#include <QDebug>
#include <QThread>

namespace NexusAI {

OllamaEmbedding::OllamaEmbedding(QObject* parent)
    : QObject(parent) {}

QVector<float> OllamaEmbedding::embed(const QString& text) {
    // Stack-local NAM living on the calling thread: embed() may run on a
    // worker thread (RAG tool execution, document import) where the
    // main-thread m_nam would be cross-thread QObject misuse.
    QNetworkAccessManager nam;

    // docs/22 F3: snapshot the endpoint/model under the mutex — the GUI
    // thread may swap them while this call is queued or running.
    QString baseUrl;
    QString model;
    {
        QMutexLocker lock(&m_configMutex);
        baseUrl = m_baseUrl;
        model = m_model;
    }

    QJsonObject body;
    body["model"] = model;
    body["prompt"] = text;

    QJsonDocument doc(body);
    QNetworkRequest request;
    request.setUrl(QUrl(baseUrl + "/api/embeddings"));
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

QVector<QStringList> OllamaEmbedding::splitBatches(const QStringList& texts, int batchSize) {
    QVector<QStringList> batches;
    if (texts.isEmpty() || batchSize <= 0) return batches;
    for (int i = 0; i < texts.size(); i += batchSize)
        batches.append(texts.mid(i, batchSize));
    return batches;
}

QVector<QVector<float>> OllamaEmbedding::embedBatch(const QStringList& texts) {
    if (texts.isEmpty()) return {};

    // Stack-local NAM on the calling thread (see embed()).
    QNetworkAccessManager nam;

    // docs/22 F3: one snapshot for the whole run — every batch in this
    // import uses the model/endpoint as of the call, even if the user
    // swaps them mid-import (the next import picks up the new value).
    QString baseUrl;
    QString model;
    {
        QMutexLocker lock(&m_configMutex);
        baseUrl = m_baseUrl;
        model = m_model;
    }

    QVector<QVector<float>> embeddings;
    embeddings.reserve(texts.size());
    int done = 0;

    // FEAT-803: page the request so one huge document cannot pin the server
    // (or the GUI-facing progress) for the whole import.
    for (const QStringList& batch : splitBatches(texts, BatchSize)) {
        QJsonObject body;
        body["model"] = model;
        QJsonArray input;
        for (const QString& t : batch)
            input.append(t);
        body["input"] = input;

        QNetworkRequest request;
        request.setUrl(QUrl(baseUrl + "/api/embed"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setTransferTimeout(120000);

        // docs/24 H5: one retry per batch on transport errors — a single
        // transient failure (Ollama restarting, blip) used to degrade the
        // whole document to pseudo embeddings permanently. Protocol errors
        // (size mismatch / empty vectors below) stay fail-fast: retrying
        // them is meaningless.
        QNetworkReply* reply = nullptr;
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (attempt > 0) {
                qWarning() << "Batch embedding request failed once, retrying:"
                           << reply->errorString();
                reply->deleteLater();
                QThread::msleep(500);
            }
            reply = nam.post(request, QJsonDocument(body).toJson());
            QEventLoop loop;
            connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            loop.exec();
            if (reply->error() == QNetworkReply::NoError) break;
        }

        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "Batch embedding request failed:" << reply->errorString();
            reply->deleteLater();
            return {};   // abort -> caller falls back wholesale
        }

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        reply->deleteLater();

        const QJsonArray array = doc.object().value("embeddings").toArray();
        if (array.size() != batch.size()) {
            qWarning() << "Batch embedding response size mismatch:"
                       << array.size() << "of" << batch.size();
            return {};
        }

        for (const auto& item : array) {
            const QJsonArray vec = item.toArray();
            if (vec.isEmpty()) return {};   // any hole -> caller falls back wholesale
            QVector<float> embedding;
            embedding.reserve(vec.size());
            for (const auto& val : vec)
                embedding.append(static_cast<float>(val.toDouble()));
            embeddings.append(embedding);
        }

        done += batch.size();
        emit embedProgress(done, texts.size());
    }
    return embeddings;
}


} // namespace NexusAI
