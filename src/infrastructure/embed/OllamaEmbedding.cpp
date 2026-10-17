#include "OllamaEmbedding.h"
#include "EmbedShutdown.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEventLoop>
#include <QDebug>
#include <QThread>
#include <QTimer>

namespace NexusAI {
namespace {

// 任务 #22：QEventLoop 等待期间的退出标志轮询间隔（与
// OpenAICompatibleEmbedding 保持一致——既要尽快响应 aboutToQuit，又
// 不能密到空转浪费 CPU）。
constexpr int kShutdownPollMs = 200;

} // namespace

OllamaEmbedding::OllamaEmbedding(QObject* parent)
    : QObject(parent) {}

QVector<float> OllamaEmbedding::embed(const QString& text) {
    // 任务 #22 退出协作取消：标志已置位（aboutToQuit 已发生）时直接返
    // 回空——线程池里积压的调用在进程退出时不必再白跑一个 30s 超时。
    if (embeddingShutdownRequested()) return {};

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
    // 任务 #22：transferTimeout 要 30s 后才生效，退出时等不起（静态析
    // 构阶段线程池 waitForDone 会被一直拖着）。同线程 QTimer 每 200ms
    // 轮询退出标志：置位则 abort 在途请求（abort 会触发 finished，
    // loop.quit() 是双保险）。QTimer 是栈对象，天然创建在调用线程上，
    // 与事件循环、reply 同线程，无跨线程 QObject 问题。
    QTimer shutdownPoll;
    QObject::connect(&shutdownPoll, &QTimer::timeout, &loop,
                     [&loop, reply]() {
                         if (!embeddingShutdownRequested()) return;
                         reply->abort();
                         loop.quit();
                     });
    shutdownPoll.start(kShutdownPollMs);
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
    // 任务 #22 退出协作取消：入口快速短路（同 embed()）。
    if (embeddingShutdownRequested()) return {};

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
        // 任务 #22：退出标志置位后中止后续批，返回空向量——与"任一批
        // 失败整篇降级"的现有语义一致，调用方照旧整体回退。
        if (embeddingShutdownRequested()) return {};

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
        // them is meaningless. 任务 #22 B1 修正落地：HTTP 4xx 客户端错
        // 误同样不重试（404 模型不存在 / 400 请求体），见循环内判断。
        QNetworkReply* reply = nullptr;
        for (int attempt = 0; attempt < 2; ++attempt) {
            // 任务 #22：退出中断——不再发起新尝试（含第一次）。已持有
            // 的 reply 释放后按整篇失败返回（attempt 0 时 reply 尚为
            // nullptr，不能解引用）。
            if (embeddingShutdownRequested()) {
                if (reply) reply->deleteLater();
                return {};
            }
            if (attempt > 0) {
                qWarning() << "Batch embedding request failed once, retrying:"
                           << reply->errorString();
                reply->deleteLater();
                QThread::msleep(500);
            }
            reply = nam.post(request, QJsonDocument(body).toJson());
            QEventLoop loop;
            // 任务 #22：200ms 轮询退出标志（同 embed()，同线程 QTimer）。
            QTimer shutdownPoll;
            QObject::connect(&shutdownPoll, &QTimer::timeout, &loop,
                             [&loop, reply]() {
                                 if (!embeddingShutdownRequested()) return;
                                 reply->abort();
                                 loop.quit();
                             });
            shutdownPoll.start(kShutdownPollMs);
            connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            loop.exec();
            if (reply->error() == QNetworkReply::NoError) break;
            // 任务 #22 B1：HTTP 4xx 客户端错误（404 模型不存在 / 400
            // 请求体 / 413 载荷过大）重试注定再失败——不重试，
            // fail-fast；5xx 与传输层错误（error() 非 NoError 且无 4xx
            // 状态码，如 Ollama 重启中的连接被拒/重置）保留一次重试。
            const int httpStatus = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (httpStatus >= 400 && httpStatus < 500) break;
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
