#include "OpenAICompatibleEmbedding.h"
#include "EmbedShutdown.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEventLoop>
#include <QDebug>
#include <QLoggingCategory>
#include <QThread>
#include <QTimer>
#include <QAtomicInt>

// Embedding 域日志类别（对齐 SettingsService/McpManager 的模块级
// Q_LOGGING_CATEGORY + qCWarning 规范）：空 key 这类需要按域过滤的
// 告警不再走无类别的 qWarning。
Q_LOGGING_CATEGORY(lcEmbedding, "nexus.embedding")

namespace NexusAI {
namespace {

// 外部向量服务（云端排队、TLS 握手、跨地域 RTT）比本地 Ollama 慢一个
// 量级：单条 60s / 批量 300s（本地后端保持 30s/120s 不变）。批量预算
// 还要容纳每页一次 500ms 的重试退避。任务 #22 后这些超时不再是退出
// 路径的下限：下面的轮询定时器在 ~200ms 内掐断在途请求。
constexpr int kSingleTimeoutMs = 60000;
constexpr int kBatchTimeoutMs = 300000;

// 任务 #22：QEventLoop 等待期间的退出标志轮询间隔——既要尽快响应
// aboutToQuit（用户在等进程退出），又不能密到空转浪费 CPU。
constexpr int kShutdownPollMs = 200;

// key 为空的提醒每个进程只发一次：批量导入会连发几十个请求，第一行
// 日志已经把话说完了。embed() 与 embedBatch() 共用同一标志，同一
// 进程不会打两条重复警告（任务 #22 B2）。
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
    // 任务 #22 退出协作取消：标志已置位（aboutToQuit 已发生）时直接返
    // 回空——线程池里积压的调用在进程退出时不必再白跑一个 60s 超时。
    if (embeddingShutdownRequested()) return {};

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
            qCWarning(lcEmbedding) << "Embedding API key is empty - sending the request"
                                   << "anyway; auth-requiring services will answer 401";
    } else {
        request.setRawHeader(QByteArrayLiteral("Authorization"),
                             QByteArrayLiteral("Bearer ") + apiKey.toUtf8());
    }

    QNetworkReply* reply = nam.post(request, QJsonDocument(body).toJson());

    QEventLoop loop;
    // 任务 #22：transferTimeout 要 60s 后才生效，退出时等不起（静态析
    // 构阶段线程池 waitForDone 会被拖满 300s）。同线程 QTimer 每 200ms
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
    // 任务 #22 退出协作取消：入口快速短路（同 embed()）。
    if (embeddingShutdownRequested()) return {};

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
        request.setUrl(QUrl(embeddingsUrl(baseUrl)));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setTransferTimeout(kBatchTimeoutMs);
        if (apiKey.isEmpty()) {
            // 任务 #22 B2：对齐 embed() 的每进程一次警告——共用同一个
            // g_warnedEmptyApiKey，批量导入几十批不会刷屏，也不会与
            // embed() 各打一条重复警告。
            if (g_warnedEmptyApiKey.testAndSetAcquire(0, 1))
                qCWarning(lcEmbedding) << "Embedding API key is empty - batch requests"
                                       << "are sent anyway; auth-requiring services"
                                       << "will answer 401";
        } else {
            request.setRawHeader(QByteArrayLiteral("Authorization"),
                                 QByteArrayLiteral("Bearer ") + apiKey.toUtf8());
        }

        // docs/24 H5: one retry per batch on transport errors — a single
        // transient failure (gateway blip, TLS reset) used to degrade the
        // whole document to pseudo embeddings permanently. Protocol errors
        // (missing data / size mismatch / bad index below) stay fail-fast:
        // retrying them is meaningless. 任务 #22 B1 修正落地：HTTP 4xx
        // 客户端错误同样不重试（重试注定再失败），见循环内判断。
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
            // 任务 #22 B1：HTTP 4xx 是客户端错误（401 鉴权失败 / 400 请
            // 求体 / 404 路径或模型不存在 / 413 载荷过大 / 429 限流），
            // 同样的请求再发一次注定再失败（429 的 500ms 立即重试也只
            // 会再吃一次限流）——不重试，fail-fast。5xx 与传输层错误
            //（error() 非 NoError 且无 4xx 状态码，如连接重置/TLS 抖动）
            // 保留一次重试。
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