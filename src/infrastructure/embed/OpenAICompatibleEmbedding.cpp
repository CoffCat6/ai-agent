#include "OpenAICompatibleEmbedding.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEventLoop>
#include <QDebug>
#include <QThread>
#include <QAtomicInt>

namespace NexusAI {
namespace {

// 外部向量服务（云端排队、TLS 握手、跨地域 RTT）比本地 Ollama 慢一个
// 量级：单条 60s / 批量 300s（本地后端保持 30s/120s 不变）。批量预算
// 还要容纳每页一次 500ms 的重试退避。
constexpr int kSingleTimeoutMs = 60000;
constexpr int kBatchTimeoutMs = 300000;

// key 为空的提醒每个进程只发一次：批量导入会连发几十个请求，第一行
// 日志已经把话说完了。
QAtomicInt g_warnedEmptyApiKey;

} // namespace

OpenAICompatibleEmbedding::OpenAICompatibleEmbedding(QObject* parent)
    : QObject(parent) {}

QString OpenAICompatibleEmbedding::embeddingsUrl(const QString& baseUrl) {
    // 用户从各家文档粘贴的 baseUrl 两种形态都有（"https://api.openai.com"
    // 与 "https://api.openai.com/v1"），部分还带尾斜杠。先归一再拼路径，
    // 避免拼出 .../v1/v1/embeddings 或在根路径上少一段。
    QString base = baseUrl.trimmed();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    if (base.endsWith(QLatin1String("/v1")))
        return base + QStringLiteral("/embeddings");
    return base + QStringLiteral("/v1/embeddings");
}

QVector<float> OpenAICompatibleEmbedding::embed(const QString& text) {
    // Stack-local NAM living on the calling thread: embed() may run on a
    // worker thread (RAG tool execution, document import) where a
    // main-thread NAM would be cross-thread QObject misuse.
    QNetworkAccessManager nam;

    // docs/22 F3: snapshot the endpoint/model/key under the mutex — the GUI
    // thread may swap them while this call is queued or running.
    QString baseUrl;
    QString model;
    QString apiKey;
    {
        QMutexLocker lock(&m_configMutex);
        baseUrl = m_baseUrl;
        model = m_model;
        apiKey = m_apiKey;
    }

    QJsonObject body;
    body["model"] = model;
    body["input"] = text;

    QNetworkRequest request;
    request.setUrl(QUrl(embeddingsUrl(baseUrl)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(kSingleTimeoutMs);
    if (apiKey.isEmpty()) {
        // 部分自建网关无鉴权也能用：仍然发请求，让要求鉴权的服务端用
        // 401 拒绝（日志可见），而不是客户端直接失败——后者让用户无法
        // 区分"网关本就免鉴权"与"密钥没配好"。
        if (g_warnedEmptyApiKey.testAndSetAcquire(0, 1))
            qWarning() << "Embedding API key is empty - sending the request"
                       << "anyway; auth-requiring services will answer 401";
    } else {
        request.setRawHeader(QByteArrayLiteral("Authorization"),
                             QByteArrayLiteral("Bearer ") + apiKey.toUtf8());
    }

    QNetworkReply* reply = nam.post(request, QJsonDocument(body).toJson());

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

    const QJsonArray dataArray = responseDoc.object().value("data").toArray();
    if (dataArray.isEmpty()) {
        // HTTP 200 但没有 data 属协议错误（个别网关这样透出错误）：
        // 报告并失败——重试同样的请求没有意义（docs/24 H5 fail-fast）。
        qWarning() << "Embedding response has no data array";
        return embedding;
    }
    // 单条请求只发了一条 input，data[0] 即结果（index 字段只在批量
    // 响应里才承载归位信息）。
    const QJsonArray vec =
        dataArray.at(0).toObject().value("embedding").toArray();
    if (vec.isEmpty()) {
        qWarning() << "Embedding response vector is empty";
        return embedding;
    }

    embedding.reserve(vec.size());
    for (const auto& val : vec)
        embedding.append(static_cast<float>(val.toDouble()));

    return embedding;
}

QVector<QStringList> OpenAICompatibleEmbedding::splitBatches(
        const QStringList& texts, int batchSize) {
    QVector<QStringList> batches;
    if (texts.isEmpty() || batchSize <= 0) return batches;
    for (int i = 0; i < texts.size(); i += batchSize)
        batches.append(texts.mid(i, batchSize));
    return batches;
}

QVector<QVector<float>> OpenAICompatibleEmbedding::embedBatch(
        const QStringList& texts) {
    if (texts.isEmpty()) return {};

    // Stack-local NAM on the calling thread (see embed()).
    QNetworkAccessManager nam;

    // docs/22 F3: one snapshot for the whole run — every batch in this
    // import uses the model/endpoint/key as of the call, even if the user
    // swaps them mid-import (the next import picks up the new value).
    QString baseUrl;
    QString model;
    QString apiKey;
    {
        QMutexLocker lock(&m_configMutex);
        baseUrl = m_baseUrl;
        model = m_model;
        apiKey = m_apiKey;
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
        request.setUrl(QUrl(embeddingsUrl(baseUrl)));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setTransferTimeout(kBatchTimeoutMs);
        if (!apiKey.isEmpty()) {
            request.setRawHeader(QByteArrayLiteral("Authorization"),
                                 QByteArrayLiteral("Bearer ") + apiKey.toUtf8());
        }

        // docs/24 H5: one retry per batch on transport errors — a single
        // transient failure (gateway blip, TLS reset) used to degrade the
        // whole document to pseudo embeddings permanently. Protocol errors
        // (missing data / size mismatch / bad index below) stay fail-fast:
        // retrying them is meaningless.
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

        // OpenAI 兼容响应用 data[i].index 标明向量对应请求里的第几条
        // input。协议只保证 index 正确、不保证数组保序（实测部分网关
        // 乱序返回），按 index 归位而不是依赖顺序；数量不符/索引非法
        // /重复索引都按协议错误 fail-fast。
        const QJsonArray dataArray = doc.object().value("data").toArray();
        if (dataArray.size() != batch.size()) {
            qWarning() << "Batch embedding response size mismatch:"
                       << dataArray.size() << "of" << batch.size();
            return {};
        }

        QVector<QVector<float>> batchVectors(batch.size());
        for (const auto& item : dataArray) {
            const QJsonObject obj = item.toObject();
            const int index = obj.value("index").toInt(-1);
            if (index < 0 || index >= batch.size()
                    || !batchVectors.at(index).isEmpty()) {
                qWarning() << "Batch embedding response has invalid index:"
                           << index;
                return {};
            }
            const QJsonArray vec = obj.value("embedding").toArray();
            if (vec.isEmpty()) return {};   // any hole -> caller falls back wholesale
            QVector<float> embedding;
            embedding.reserve(vec.size());
            for (const auto& val : vec)
                embedding.append(static_cast<float>(val.toDouble()));
            batchVectors[index] = std::move(embedding);
        }

        embeddings += batchVectors;
        done += batch.size();
        emit embedProgress(done, texts.size());
    }
    return embeddings;
}

} // namespace NexusAI