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

constexpr char kDuckDuckGoUrl[] = "https://html.duckduckgo.com/html/";
constexpr char kZhipuUrl[] = "https://open.bigmodel.cn/api/paas/v4/web_search";
constexpr char kDeepSeekUrl[] = "https://api.deepseek.com/v1/tools/search";

// Backends clamp to the same window so the model-facing result count never
// depends on which provider is configured.
int clampMaxResults(int maxResults) { return qBound(1, maxResults, 10); }

// One HTTP round-trip, shared by every backend. Kept in one place so the
// timeout / cancellation contract (docs/22 F3: the caller's cancel() only
// flips an atomic; the abort happens on this thread, which owns the reply)
// cannot drift between providers.
struct HttpResponse {
    QByteArray body;
    int status{0};
    QNetworkReply::NetworkError netErr{QNetworkReply::NoError};
    QString netErrText;
    bool timedOut{false};
};

HttpResponse sendRequest(const QNetworkRequest& request, bool isPost,
                         const QByteArray& payload, int timeoutMs,
                         const std::atomic<bool>* cancelled) {
    HttpResponse result;
    QNetworkAccessManager nam;

    QNetworkReply* reply = isPost ? nam.post(request, payload) : nam.get(request);

    QTimer timeout;
    QEventLoop loop;
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&] {
        result.timedOut = true;
        reply->abort();
        loop.quit();
    });
    timeout.setSingleShot(true);
    // 超时窗口被夹在 5–60s：调用方传入更小的值不会让请求在握手阶段就被砍。
    timeout.start(qBound(5000, timeoutMs, 60000));
    QTimer cancelChecker;
    if (cancelled) {
        cancelChecker.setInterval(200);
        QObject::connect(&cancelChecker, &QTimer::timeout, &loop, [&] {
            if (cancelled->load()) {
                reply->abort();
                loop.quit();
            }
        });
        cancelChecker.start();
    }
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    cancelChecker.stop();

    if (result.timedOut) {
        reply->deleteLater();
        return result;
    }

    result.body = reply->readAll();
    result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.netErr = reply->error();
    result.netErrText = reply->errorString();
    reply->deleteLater();
    return result;
}

QNetworkRequest jsonPostRequest(const char* url, const QString& apiKey) {
    QNetworkRequest req(QUrl(QString::fromUtf8(url)));
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json"));
    if (!apiKey.isEmpty()) {
        req.setRawHeader("Authorization",
                         "Bearer " + apiKey.toUtf8());
    }
    return req;
}

// 传输层错误优先于 HTTP 状态码报告：此时 status 往往是 0，单独看状态码
// 会得到「HTTP 0」这种无意义诊断。
bool reportTransportFailure(const HttpResponse& response, QString* errorReason) {
    if (response.timedOut) {
        if (errorReason) *errorReason = QStringLiteral("请求超时");
        return true;
    }
    if (response.netErr != QNetworkReply::NoError) {
        if (errorReason)
            *errorReason = QStringLiteral("请求失败：%1").arg(response.netErrText);
        return true;
    }
    return false;
}

// Pull `text` out of an OpenAI-style / 智谱-style error envelope. Both use
// {"error":{"message": …}}; returns an empty string when the body is not one.
QString errorMessageFromBody(const QByteArray& body) {
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) return {};
    const QJsonObject error = doc.object().value(QStringLiteral("error")).toObject();
    return error.value(QStringLiteral("message")).toString().trimmed();
}

// 智谱在 200 之外还带业务码（1701/1702/1703），部分错误也走 HTTP 200 以外的
// 通道；统一按 code 给可操作提示，而不是把原始 JSON 丢给用户。
QString zhipuHint(const QString& code, const QString& message) {
    if (code == QLatin1String("1701"))
        return QStringLiteral("智谱搜索并发已达上限，请稍后重试或减少并发请求");
    if (code == QLatin1String("1702"))
        return QStringLiteral("智谱平台侧暂无可用搜索引擎（非本地配置问题），请稍后重试");
    if (code == QLatin1String("1703"))
        return QStringLiteral("智谱搜索引擎未返回有效数据，请调整查询条件后重试");
    return message;
}

QString deepSeekHint(int status, const QString& message) {
    switch (status) {
    case 402:
        return QStringLiteral("DeepSeek 账号余额不足，请充值后重试");
    case 429:
        return QStringLiteral("DeepSeek 搜索请求超过限速（账号级 100 QPS），请稍后重试");
    case 504:
        return QStringLiteral("DeepSeek 下游请求超时，请稍后重试");
    case 401:
        return QStringLiteral("DeepSeek API Key 无效或已过期，请在设置中心重新填写");
    default:
        break;
    }
    if (status >= 500)
        return QStringLiteral("DeepSeek 服务端错误（HTTP %1），请稍后重试").arg(status);
    return message;
}

} // namespace

// ── DuckDuckGo ──────────────────────────────────────────────────────────

bool DuckDuckGoBackend::looksLikeAntiBotChallenge(const QString& html) {
    // 挑战页特征：anomaly 脚本 + challenge-form + 「Select all squares」拼图。
    // 任一命中即判定为反爬拦截（三者同时出现是当前实际页面，但单独看
    // anomaly 出现也已经足够说明不是正常结果页）。
    return html.contains(QLatin1String("anomaly.js"))
        || html.contains(QLatin1String("challenge-form"))
        || html.contains(QLatin1String("anomaly-modal"));
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
} // namespace

QList<WebSearchResult> DuckDuckGoBackend::search(const QString& query,
                                                 int maxResults,
                                                 const QString& /*apiKey*/,
                                                 int timeoutMs,
                                                 QString* errorReason,
                                                 const std::atomic<bool>* cancelled) const {
    // docs/25 E12：空 query 是 no-op，不写 errorReason——与其它后端契约一致
    //（test_web_search 的 sentinel 断言；工具层本就拦截空 query）。
    if (query.trimmed().isEmpty())
        return {};

    maxResults = clampMaxResults(maxResults);

    QUrl url(QString::fromUtf8(kDuckDuckGoUrl));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), query.trimmed());
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("NexusAI/1.0"));

    const HttpResponse response = sendRequest(req, false, {}, timeoutMs, cancelled);
    if (reportTransportFailure(response, errorReason))
        return {};

    // 反爬判定放在状态码判定之前：挑战页既可能以 202 返回（实测），也可能
    // 以 200 返回，单看状态码抓不住。命中时给出可行动的指引。
    const QString html = QString::fromUtf8(response.body);
    if (looksLikeAntiBotChallenge(html)) {
        if (errorReason) {
            *errorReason = QStringLiteral(
                "DuckDuckGo 返回了人机验证页（反爬拦截），当前网络无法直接检索。"
                "请在设置中心配置代理，或改用智谱 / DeepSeek 搜索服务。");
        }
        return {};
    }

    if (response.status < 200 || response.status >= 300) {
        if (errorReason) *errorReason = QStringLiteral("HTTP %1").arg(response.status);
        return {};
    }

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

    // 200 且不是挑战页，却一条都解析不出来：页面结构和解析器不匹配了
    // （DDG 改版）。单独报出来，别让用户以为是自己查询词的问题。
    if (out.isEmpty() && errorReason) {
        *errorReason = QStringLiteral(
            "DuckDuckGo 响应中没有可解析的搜索结果（页面结构可能已变更），"
            "请改用其它搜索服务或配置代理后重试");
    }
    return out;
}

// ── 智谱 Web Search ─────────────────────────────────────────────────────

QJsonObject ZhipuSearchBackend::buildRequest(const QString& query, int maxResults) {
    // search_intent 是 schema 里的 required 字段（漏传直接 400）。语义是
    // 「要不要先做意图识别再决定搜不搜」，false = 跳过识别直接搜——工具层
    // 被调用时查询意图已经明确，跳过更省一次往返。
    // content_size=high 换更长的摘要，减少「摘要太短模型读不出信息」的情况。
    return QJsonObject{
        {QStringLiteral("search_query"), query.trimmed()},
        {QStringLiteral("search_engine"), QStringLiteral("search_std")},
        {QStringLiteral("search_intent"), false},
        {QStringLiteral("count"), clampMaxResults(maxResults)},
        {QStringLiteral("content_size"), QStringLiteral("high")}
    };
}

QList<WebSearchResult> ZhipuSearchBackend::parseResponse(const QByteArray& json,
                                                         int maxResults,
                                                         QString* errorReason) {
    maxResults = clampMaxResults(maxResults);

    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) {
        if (errorReason) *errorReason = QStringLiteral("响应不是合法的 JSON 对象");
        return {};
    }

    // 错误信封优先：智谱可能在 HTTP 200 之外用 body 携带业务错误。
    const QJsonObject envelope = doc.object().value(QStringLiteral("error")).toObject();
    if (!envelope.isEmpty()) {
        if (errorReason) {
            *errorReason = zhipuHint(
                envelope.value(QStringLiteral("code")).toString(),
                envelope.value(QStringLiteral("message")).toString().trimmed());
        }
        return {};
    }

    // 链接字段是 `link`（不是 url），摘要是 `content`。
    const QJsonArray results = doc.object().value(QStringLiteral("search_result")).toArray();
    QList<WebSearchResult> out;
    for (const auto& v : results) {
        if (out.size() >= maxResults) break;
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        WebSearchResult r;
        r.title = o.value(QStringLiteral("title")).toString().trimmed();
        r.url = o.value(QStringLiteral("link")).toString().trimmed();
        r.snippet = o.value(QStringLiteral("content")).toString().trimmed();
        if (r.url.isEmpty()) continue;
        if (r.title.isEmpty()) r.title = r.url;
        out.append(r);
    }
    return out;
}

QList<WebSearchResult> ZhipuSearchBackend::search(const QString& query,
                                                  int maxResults,
                                                  const QString& apiKey,
                                                  int timeoutMs,
                                                  QString* errorReason,
                                                  const std::atomic<bool>* cancelled) const {
    if (query.trimmed().isEmpty())
        return {};
    if (apiKey.isEmpty()) {
        if (errorReason) {
            *errorReason = QStringLiteral(
                "未配置智谱 API Key（请在设置中心 → 工具 → 联网搜索填写）");
        }
        return {};
    }
    // 超过平台 70 字符硬限制时提前拦截：请求一定会被 400 拒绝，与其把原始
    // 错误体丢给用户，不如直接说清楚该缩短到什么长度。
    if (query.trimmed().size() > kMaxQueryChars) {
        if (errorReason) {
            *errorReason = QStringLiteral(
                "智谱搜索的查询词最多 %1 个字符（当前 %2 个），请缩短后重试")
                .arg(kMaxQueryChars).arg(query.trimmed().size());
        }
        return {};
    }

    const QNetworkRequest req = jsonPostRequest(kZhipuUrl, apiKey);
    const QByteArray payload = QJsonDocument(buildRequest(query, maxResults))
                                   .toJson(QJsonDocument::Compact);

    const HttpResponse response = sendRequest(req, true, payload, timeoutMs, cancelled);
    if (reportTransportFailure(response, errorReason))
        return {};

    // 先让 parseResponse 有机会读取业务错误信封（部分错误走 HTTP 200）。
    QString bodyReason;
    const QList<WebSearchResult> out =
        parseResponse(response.body, maxResults, &bodyReason);
    if (!bodyReason.isEmpty()) {
        if (errorReason) *errorReason = bodyReason;
        return {};
    }

    if (response.status < 200 || response.status >= 300) {
        if (errorReason) {
            const QString message = errorMessageFromBody(response.body);
            *errorReason = QStringLiteral("HTTP %1%2").arg(response.status).arg(
                message.isEmpty() ? QString() : QStringLiteral("：") + message);
        }
        return {};
    }

    if (out.isEmpty() && errorReason)
        *errorReason = QStringLiteral("智谱搜索未返回结果（查询词可能过窄，或平台暂无匹配内容）");
    return out;
}

// ── DeepSeek Search ─────────────────────────────────────────────────────

QJsonObject DeepSeekSearchBackend::buildRequest(const QString& query, int maxResults) {
    // need_content 保持 false：content 是整页正文，会瞬间撑爆工具层 6000 字符
    // 的预算；summary（600~800 字符，官方定位「适用于给模型阅读」）才是要的。
    return QJsonObject{
        {QStringLiteral("query"), query.trimmed()},
        {QStringLiteral("count"), clampMaxResults(maxResults)},
        {QStringLiteral("need_content"), false}
    };
}

QList<WebSearchResult> DeepSeekSearchBackend::parseResponse(const QByteArray& json,
                                                            int maxResults,
                                                            QString* errorReason) {
    maxResults = clampMaxResults(maxResults);

    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) {
        if (errorReason) *errorReason = QStringLiteral("响应不是合法的 JSON 对象");
        return {};
    }

    const QJsonObject envelope = doc.object().value(QStringLiteral("error")).toObject();
    if (!envelope.isEmpty()) {
        if (errorReason) {
            *errorReason = envelope.value(QStringLiteral("message")).toString().trimmed();
            if (errorReason->isEmpty())
                *errorReason = QStringLiteral("DeepSeek 搜索返回错误");
        }
        return {};
    }

    const QJsonArray results = doc.object().value(QStringLiteral("results")).toArray();
    QList<WebSearchResult> out;
    for (const auto& v : results) {
        if (out.size() >= maxResults) break;
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        WebSearchResult r;
        r.title = o.value(QStringLiteral("title")).toString().trimmed();
        r.url = o.value(QStringLiteral("url")).toString().trimmed();
        // summary 优先（给模型读的长摘要），回退到前端用的 snippet。
        r.snippet = o.value(QStringLiteral("summary")).toString().trimmed();
        if (r.snippet.isEmpty())
            r.snippet = o.value(QStringLiteral("snippet")).toString().trimmed();
        if (r.url.isEmpty()) continue;
        if (r.title.isEmpty()) r.title = r.url;
        out.append(r);
    }
    return out;
}

QList<WebSearchResult> DeepSeekSearchBackend::search(const QString& query,
                                                     int maxResults,
                                                     const QString& apiKey,
                                                     int timeoutMs,
                                                     QString* errorReason,
                                                     const std::atomic<bool>* cancelled) const {
    if (query.trimmed().isEmpty())
        return {};
    if (apiKey.isEmpty()) {
        if (errorReason) {
            *errorReason = QStringLiteral(
                "未配置 DeepSeek API Key（请在设置中心 → 工具 → 联网搜索填写）");
        }
        return {};
    }

    const QNetworkRequest req = jsonPostRequest(kDeepSeekUrl, apiKey);
    const QByteArray payload = QJsonDocument(buildRequest(query, maxResults))
                                   .toJson(QJsonDocument::Compact);

    const HttpResponse response = sendRequest(req, true, payload, timeoutMs, cancelled);
    if (reportTransportFailure(response, errorReason))
        return {};

    if (response.status < 200 || response.status >= 300) {
        if (errorReason) {
            const QString message = errorMessageFromBody(response.body);
            *errorReason = deepSeekHint(response.status, message.isEmpty()
                ? QStringLiteral("HTTP %1").arg(response.status) : message);
        }
        return {};
    }

    QString bodyReason;
    const QList<WebSearchResult> out =
        parseResponse(response.body, maxResults, &bodyReason);
    if (!bodyReason.isEmpty()) {
        if (errorReason) *errorReason = bodyReason;
        return {};
    }

    // 官方约定：搜索正常结束但没有匹配结果是 200 + 空数组，且这种情况不计费。
    // 报成「没搜到」是准确的，不用提示网络问题。
    if (out.isEmpty() && errorReason)
        *errorReason = QStringLiteral("DeepSeek 搜索没有匹配结果（可尝试放宽查询词）");
    return out;
}

// ── 注册表 ──────────────────────────────────────────────────────────────

const QVector<WebSearchProviderDescriptor>& WebSearchProviderRegistry::descriptors() {
    static const QVector<WebSearchProviderDescriptor> table = {
        // 首装默认：免密钥、装上即可用。国内网络可能撞反爬挑战页，但那时
        // 工具层会给出明确原因，而不是静默零结果。
        {QStringLiteral("duckduckgo"), QStringLiteral("DuckDuckGo"), false, QString()},

        {QStringLiteral("zhipu"), QStringLiteral("智谱 Web Search"), true,
         QStringLiteral("webSearch/zhipuApiKey")},

        {QStringLiteral("deepseek"), QStringLiteral("DeepSeek Search"), true,
         QStringLiteral("webSearch/deepseekApiKey")},
    };
    return table;
}

const WebSearchProviderDescriptor* WebSearchProviderRegistry::find(const QString& key) {
    for (const auto& d : descriptors())
        if (d.key == key) return &d;
    return nullptr;
}

QStringList WebSearchProviderRegistry::keys() {
    QStringList out;
    for (const auto& d : descriptors()) out.append(d.key);
    return out;
}

QString WebSearchProviderRegistry::defaultProviderKey() {
    return QStringLiteral("duckduckgo");
}

std::unique_ptr<WebSearchBackend> WebSearchProviderRegistry::create(const QString& key) {
    // 工厂按 key 分派（而不是在描述符里存函数指针）：描述符保持可拷贝，
    // 才能作为 QList 的静态表成员。
    if (key == QLatin1String("duckduckgo"))
        return std::make_unique<DuckDuckGoBackend>();
    if (key == QLatin1String("zhipu"))
        return std::make_unique<ZhipuSearchBackend>();
    if (key == QLatin1String("deepseek"))
        return std::make_unique<DeepSeekSearchBackend>();
    return nullptr;
}

QString WebSearchProviderRegistry::apiKeySettingKey(const QString& key) {
    if (const auto* d = find(key))
        return d->apiKeySettingKey;
    return {};
}

bool WebSearchProviderRegistry::requiresApiKey(const QString& key) {
    if (const auto* d = find(key))
        return d->requiresApiKey;
    return false;
}

} // namespace NexusAI
