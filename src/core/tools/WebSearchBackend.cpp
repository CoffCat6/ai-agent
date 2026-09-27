#include "WebSearchBackend.h"

#include "core/utils/Activity.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
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

namespace {
QString stripHtml(const QString& s) {
    QString out = s;
    out.remove(QStringLiteral("<b>"));
    out.remove(QStringLiteral("</b>"));
    out.remove(QStringLiteral("<i>"));
    out.remove(QStringLiteral("</i>"));
    out.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    out.replace(QStringLiteral("&#x27;"), QStringLiteral("'"));
    out.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    out.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    out.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    return out;
}
}

QList<WebSearchResult> DuckDuckGoBackend::search(const QString& query,
                                                  int maxResults,
                                                  const QString& /*apiKey*/,
                                                  int timeoutMs) const {
    if (query.trimmed().isEmpty())
        return {};

    maxResults = qBound(1, maxResults, 10);

    QUrl url(QStringLiteral("https://html.duckduckgo.com/html/"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), query.trimmed());
    url.setQuery(q);

    QNetworkAccessManager nam;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("NexusAI/1.0"));

    QNetworkReply* reply = nam.get(req);

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

    const QString html = QString::fromUtf8(body);

    static const QRegularExpression linkRe(
        QStringLiteral(R"REX(class="result__a"\s+href="([^"]*)"[^>]*>(.*?)</a>)REX"),
        QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression snippetRe(
        QStringLiteral(R"REX(class="result__snippet"[^>]*>(.*?)</a>)REX"),
        QRegularExpression::DotMatchesEverythingOption);

    QList<WebSearchResult> out;
    auto linkIt = linkRe.globalMatch(html);
    auto snippetIt = snippetRe.globalMatch(html);

    while (linkIt.hasNext() && out.size() < maxResults) {
        const QRegularExpressionMatch m = linkIt.next();
        const QString rawHref = m.captured(1);
        const QString title = stripHtml(m.captured(2)).trimmed();

        QString realUrl = rawHref;
        if (rawHref.contains(QStringLiteral("uddg="))) {
            const QString scheme = rawHref.startsWith(QStringLiteral("//"))
                ? QStringLiteral("https:") + rawHref : rawHref;
            const QUrl redirectUrl(scheme);
            const QUrlQuery redirectQuery(redirectUrl.query());
            realUrl = redirectQuery.queryItemValue(QStringLiteral("uddg"), QUrl::FullyDecoded);
        }

        if (realUrl.isEmpty() || title.isEmpty()) continue;

        WebSearchResult r;
        r.title = title;
        r.url = realUrl;
        r.snippet = snippetIt.hasNext()
            ? stripHtml(snippetIt.next().captured(1)).trimmed() : QString();
        out.append(r);
    }

    return out;
}

} // namespace NexusAI
