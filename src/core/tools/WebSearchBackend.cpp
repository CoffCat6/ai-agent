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
                                             int timeoutMs,
                                             QString* errorReason,
                                             const std::atomic<bool>* cancelled) const {
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
    QTimer cancelChecker;
    if (cancelled) {
        cancelChecker.setInterval(200);
        QObject::connect(&cancelChecker, &QTimer::timeout, &loop, [&] {
            if (cancelled->load()) { reply->abort(); loop.quit(); }
        });
        cancelChecker.start();
    }
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    cancelChecker.stop();

    if (timedOut) {
        reply->deleteLater();
        if (errorReason) *errorReason = QStringLiteral("请求超时");
        return {};
    }

    const QByteArray body = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError netErr = reply->error();
    const QString netErrText = reply->errorString();
    reply->deleteLater();

    // 传输层错误（DNS / 连接被拒 / TLS）优先于 HTTP 状态码报告：此时
    // status 往往是 0，单独看状态码会得到「HTTP 0」这种无意义诊断。
    if (netErr != QNetworkReply::NoError) {
        if (errorReason) *errorReason = QStringLiteral("请求失败：%1").arg(netErrText);
        return {};
    }

    if (status < 200 || status >= 300) {
        if (errorReason) *errorReason = QStringLiteral("HTTP %1").arg(status);
        return {};
    }

    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        if (errorReason) *errorReason = QStringLiteral("响应不是合法的 JSON 对象");
        return {};
    }

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
                                                   int timeoutMs,
                                                   QString* errorReason,
                                                   const std::atomic<bool>* cancelled) const {
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
    QTimer cancelChecker;
    if (cancelled) {
        cancelChecker.setInterval(200);
        QObject::connect(&cancelChecker, &QTimer::timeout, &loop, [&] {
            if (cancelled->load()) { reply->abort(); loop.quit(); }
        });
        cancelChecker.start();
    }
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    cancelChecker.stop();

    if (timedOut) {
        reply->deleteLater();
        if (errorReason) *errorReason = QStringLiteral("请求超时");
        return {};
    }

    const QByteArray body = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError netErr = reply->error();
    const QString netErrText = reply->errorString();
    reply->deleteLater();

    if (netErr != QNetworkReply::NoError) {
        if (errorReason) *errorReason = QStringLiteral("请求失败：%1").arg(netErrText);
        return {};
    }

    if (status < 200 || status >= 300) {
        if (errorReason) *errorReason = QStringLiteral("HTTP %1").arg(status);
        return {};
    }
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

// docs/20 §5.3: SearXNG (self-hosted, GET + JSON format). The parsing rules
// mirror TavilyBackend::search — same {title,url,content} mapping, same
// qBound(1, maxResults, 10) clamp, empty-URL entries dropped, missing title
// falls back to the URL — so callers see identical result semantics.
QUrl SearxBackend::buildSearchUrl(const QString& baseUrl, const QString& query) {
    QString base = baseUrl.trimmed();
    while (base.endsWith(QLatin1Char('/'))) base.chop(1);
    QUrl url(base + QStringLiteral("/search"));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("q"), query.trimmed());
    params.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    url.setQuery(params);
    return url;
}

QList<WebSearchResult> SearxBackend::parseResponse(const QByteArray& json,
                                                    int maxResults) {
    maxResults = qBound(1, maxResults, 10);

    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return {};

    const QJsonArray results = doc.object().value(QStringLiteral("results")).toArray();
    QList<WebSearchResult> out;
    for (const auto& v : results) {
        if (out.size() >= maxResults) break;
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

QList<WebSearchResult> SearxBackend::search(const QString& query,
                                               int maxResults,
                                               const QString& /*apiKey*/,
                                               int timeoutMs,
                                               QString* errorReason,
                                               const std::atomic<bool>* cancelled) const {
    // 未配置实例地址不是「没搜到」，工具层会先读 notReadyReason() 报出来；
    // 这里仍留一道兜底（直接 new 出后端、没走设置链路的调用方）。
    if (m_baseUrl.isEmpty()) {
        if (errorReason) *errorReason = notReadyReason();
        return {};
    }
    if (query.trimmed().isEmpty())
        return {};

    const QUrl url = buildSearchUrl(m_baseUrl, query);
    // scheme 必须是 http/https：QUrl 的宽容模式会把 "localhost:8080" 解析成
    // scheme="localhost"，isValid()/isRelative() 都拦不住，最后变成一次注定
    // 失败的怪异请求。这里显式收敛到两种可接受 scheme。
    if (!url.isValid() || url.isRelative()
        || (url.scheme() != QLatin1String("http")
            && url.scheme() != QLatin1String("https"))) {
        if (errorReason) {
            *errorReason = QStringLiteral(
                "无效的 SearXNG 地址（需以 http:// 或 https:// 开头）：%1").arg(m_baseUrl);
        }
        return {};
    }

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
    QTimer cancelChecker;
    if (cancelled) {
        cancelChecker.setInterval(200);
        QObject::connect(&cancelChecker, &QTimer::timeout, &loop, [&] {
            if (cancelled->load()) { reply->abort(); loop.quit(); }
        });
        cancelChecker.start();
    }
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    cancelChecker.stop();

    if (timedOut) {
        reply->deleteLater();
        if (errorReason) *errorReason = QStringLiteral("请求超时");
        return {};
    }

    const QByteArray body = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError netErr = reply->error();
    const QString netErrText = reply->errorString();
    reply->deleteLater();

    // SearXNG 默认在 settings.yml 里关闭 JSON 输出（search.formats: [html]），
    // 请求 format=json 会拿到 403/400 的 HTML 错误页——这属于「实例没开
    // JSON API」，单独报出来用户才知道要去改 SearXNG 配置，而不是怀疑网络。
    if (netErr != QNetworkReply::NoError) {
        if (errorReason) *errorReason = QStringLiteral("请求失败：%1").arg(netErrText);
        return {};
    }
    if (status < 200 || status >= 300) {
        if (errorReason) {
            *errorReason = QStringLiteral("HTTP %1（请确认该 SearXNG 实例已启用 JSON API："
                                          "search.formats 需包含 json）").arg(status);
        }
        return {};
    }

    const QList<WebSearchResult> out = parseResponse(body, maxResults);
    if (out.isEmpty() && errorReason) {
        // 200 且 JSON 合法但一条结果都没有：把「实例返回了空结果集」与
        // 「响应不是预期结构」分开。
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (!doc.isObject())
            *errorReason = QStringLiteral("响应不是预期的 JSON 对象");
    }
    return out;
}

} // namespace NexusAI
