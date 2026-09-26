#include "WebSearchBackend.h"

#include "core/utils/Activity.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>

namespace NexusAI {

namespace {
constexpr char kTavilyUrl[] = "https://api.tavily.com/search";
}

QList<WebSearchResult> TavilyBackend::search(const QString& query,
                                             int maxResults,
                                             const QString& apiKey,
                                             int timeoutMs) const {
    if (query.trimmed().isEmpty() || apiKey.isEmpty())
        return {};

    maxResults = qBound(1, maxResults, 10);

    QNetworkAccessManager nam;
    QNetworkRequest req(QUrl(QString::fromUtf8(kTavilyUrl)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonObject payload{
        {QStringLiteral("api_key"), apiKey},
        {QStringLiteral("query"), query.trimmed()},
        {QStringLiteral("max_results"), maxResults},
        {QStringLiteral("search_depth"), QStringLiteral("basic")}
    };

    QNetworkReply* reply = nam.post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));

    QTimer timeout;
    bool timedOut = false;
    QEventLoop loop;
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&] {
        timedOut = true;
        reply->abort();
        loop.quit();
    });
    timeout.setSingleShot(true);
    timeout.start(qBound(5000, timeoutMs, 60000));
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (timedOut) {
        reply->deleteLater();
        return {};
    }

    const QByteArray body = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    if (status < 200 || status >= 300)
        return {};

    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return {};

    const QJsonArray results = doc.object().value(QStringLiteral("results")).toArray();
    QList<WebSearchResult> out;
    for (const auto& v : results) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        WebSearchResult r;
        r.title = o.value(QStringLiteral("title")).toString().trimmed();
        r.url = o.value(QStringLiteral("url")).toString().trimmed();
        r.snippet = o.value(QStringLiteral("content")).toString().trimmed();
        if (r.url.isEmpty()) continue;
        if (r.title.isEmpty()) r.title = r.url;
        out.append(r);
    }
    return out;
}

} // namespace NexusAI
