#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUrl>

#include <atomic>

namespace NexusAI {

// Search result entry returned by a web search backend.
struct WebSearchResult {
    QString title;
    QString url;
    QString snippet;
};

// Abstract search backend. Concrete implementations (Tavily, …) are wired
// via SettingsService::webSearchProvider. The backend is stateless and
// shared across calls - execute() is const.
class WebSearchBackend {
public:
    virtual ~WebSearchBackend() = default;

    // Human-readable provider key ("tavily"). Used for logging and the
    // SettingsService enum.
    virtual QString providerKey() const = 0;

    // Search `query` and return up to `maxResults` entries. Network/parse
    // errors are reported via an empty list - the caller must check and
    // surface a ToolResult::fail. `timeoutMs` caps the HTTP round-trip.
    //
    // docs/20 §5.3 (code-review fix): `errorReason` (optional out-param)
    // separates「请求根本没成功」from「请求成功了但一条结果都没有」。空结果
    // 列表本身无法区分这两者，而把前者报成「没找到结果」会让人误以为是自己
    // 的查询词问题。传 nullptr 表示调用方不关心细节。
    virtual QList<WebSearchResult> search(const QString& query,
                                          int maxResults,
                                          const QString& apiKey,
                                          int timeoutMs = 30000,
                                          QString* errorReason = nullptr,
                                          const std::atomic<bool>* cancelled = nullptr) const = 0;

    // Whether this backend requires an API key. Defaults to true; free
    // backends (e.g. DuckDuckGo) override to false.
    virtual bool requiresApiKey() const { return true; }

    // docs/20 §5.3 (code-review fix): 后端自报「是否已就绪」。返回空串表示
    // 就绪；否则返回可直接展示给用户的原因。Tavily/DuckDuckGo 只要有网络就
    // 能用，故默认就绪；SearXNG 额外要求 baseUrl 非空（空串 = 用户还没配
    // 实例地址）。若不区分，未配置的 searxng 只会得到「请检查网络连接」这种
    // 指错方向的错误。
    virtual QString notReadyReason() const { return QString(); }
};

// Tavily search API (https://api.tavily.com/search). Accepts a JSON POST
// with {"api_key":"…","query":"…","max_results":N,"search_depth":"basic"}
// and returns {"results":[{"title","url","content"}]}.
class TavilyBackend : public WebSearchBackend {
public:
    QString providerKey() const override { return QStringLiteral("tavily"); }

    QList<WebSearchResult> search(const QString& query,
                                  int maxResults,
                                  const QString& apiKey,
                                  int timeoutMs = 30000,
                                  QString* errorReason = nullptr,
                                  const std::atomic<bool>* cancelled = nullptr) const override;
};

// DuckDuckGo Instant Answer API (https://api.duckduckgo.com/). Free, no
// API key required. Returns instant answers and related topics via GET.
class DuckDuckGoBackend : public WebSearchBackend {
public:
    QString providerKey() const override { return QStringLiteral("duckduckgo"); }
    bool requiresApiKey() const override { return false; }

    QList<WebSearchResult> search(const QString& query,
                                  int maxResults,
                                  const QString& apiKey,
                                  int timeoutMs = 30000,
                                  QString* errorReason = nullptr,
                                  const std::atomic<bool>* cancelled = nullptr) const override;
};

// docs/20 §5.3 (D14): self-hosted SearXNG JSON API —
// GET <baseUrl>/search?q=…&format=json returns
// {"results":[{"title","url","content"}]}. The entry shape matches Tavily,
// so the truncation rules are identical (qBound(1, maxResults, 10), skip
// entries without a URL, missing title falls back to the URL). No API key —
// the baseUrl (webSearch/searxBaseUrl) is the only required setting.
class SearxBackend : public WebSearchBackend {
public:
    explicit SearxBackend(const QString& baseUrl = QString())
        : m_baseUrl(baseUrl.trimmed()) {}

    QString providerKey() const override { return QStringLiteral("searxng"); }
    bool requiresApiKey() const override { return false; }
    const QString& baseUrl() const { return m_baseUrl; }

    // docs/20 §5.3: 空 baseUrl 是「用户还没填实例地址」，不是网络故障。
    // 工具层据此给出可操作的提示，而不是让人去查网络。
    QString notReadyReason() const override {
        if (m_baseUrl.isEmpty()) {
            return QStringLiteral(
                "未配置 SearXNG 服务地址：请在设置中心填写 webSearch/searxBaseUrl"
                "（自托管实例地址，例如 http://localhost:8080）");
        }
        return QString();
    }

    // Pure helpers exposed for unit tests (test_web_search covers URL
    // building and mock-JSON parsing without touching the network).
    static QUrl buildSearchUrl(const QString& baseUrl, const QString& query);
    static QList<WebSearchResult> parseResponse(const QByteArray& json,
                                                 int maxResults);

    QList<WebSearchResult> search(const QString& query,
                                  int maxResults,
                                  const QString& apiKey,
                                  int timeoutMs = 30000,
                                  QString* errorReason = nullptr,
                                  const std::atomic<bool>* cancelled = nullptr) const override;

private:
    QString m_baseUrl;
};

} // namespace NexusAI
