#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

#include <atomic>
#include <memory>

namespace NexusAI {

// Search result entry returned by a web search backend.
struct WebSearchResult {
    QString title;
    QString url;
    QString snippet;
};

// Abstract search backend. Concrete implementations are wired via
// WebSearchProviderRegistry + SettingsService::webSearchProvider. The backend
// is stateless and shared across calls - search() is const.
class WebSearchBackend {
public:
    virtual ~WebSearchBackend() = default;

    // Human-readable provider key ("duckduckgo"). Used for logging and the
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

    // 后端自报「是否已就绪」。返回空串表示就绪；否则返回可直接展示给用户的
    // 原因。现有后端都只依赖网络 + 凭据，故默认就绪。
    virtual QString notReadyReason() const { return QString(); }
};

// DuckDuckGo HTML 端点（https://html.duckduckgo.com/html/）。免费、无需
// API key，是首装默认后端。docs/25 E12：实现是 HTML 抓取——GET 查询后解析
// result__a（标题/链接，含 uddg 跳转参数解包）与 result__snippet（摘要），
// 并非早期注释误写的 Instant Answer JSON API。
class DuckDuckGoBackend : public WebSearchBackend {
public:
    QString providerKey() const override { return QStringLiteral("duckduckgo"); }
    bool requiresApiKey() const override { return false; }

    // 反爬挑战页识别：DDG 对疑似机器人返回 HTTP 202 + anomaly 验证页，
    // 页面里没有任何 result__a。不识别的话零结果会被报成「没找到结果」，
    // 用户只会反复改查询词，永远不知道该换服务或开代理。
    static bool looksLikeAntiBotChallenge(const QString& html);

    QList<WebSearchResult> search(const QString& query,
                                  int maxResults,
                                  const QString& apiKey,
                                  int timeoutMs = 30000,
                                  QString* errorReason = nullptr,
                                  const std::atomic<bool>* cancelled = nullptr) const override;
};

// 智谱 Web Search（POST https://open.bigmodel.cn/api/paas/v4/web_search）。
// Bearer 鉴权，JSON 请求：search_query / search_engine / search_intent 三项
// 必填（漏传 search_intent 直接 400）。结果在 search_result[]，链接字段叫
// `link` 而不是 `url`，摘要字段叫 `content`。
class ZhipuSearchBackend : public WebSearchBackend {
public:
    // 平台侧硬限制：search_query 最长 70 字符（超出返回 400）。提前拦截，
    // 给出可操作的提示，而不是让用户面对一个原始错误体。
    static constexpr int kMaxQueryChars = 70;

    QString providerKey() const override { return QStringLiteral("zhipu"); }

    // 纯 helper，供单测覆盖请求构造与解析，不触网。
    static QJsonObject buildRequest(const QString& query, int maxResults);
    static QList<WebSearchResult> parseResponse(const QByteArray& json,
                                                int maxResults,
                                                QString* errorReason = nullptr);

    QList<WebSearchResult> search(const QString& query,
                                  int maxResults,
                                  const QString& apiKey,
                                  int timeoutMs = 30000,
                                  QString* errorReason = nullptr,
                                  const std::atomic<bool>* cancelled = nullptr) const override;
};

// DeepSeek Search（POST https://api.deepseek.com/v1/tools/search）。
// Bearer 鉴权（与 DeepSeek 对话 API 同一把 key），OpenAI 兼容错误体。
// need_content=false 是刻意的：content 是整页正文，打开会瞬间撑爆 6000
// 字符预算；summary 是 600~800 字符的「给模型读」摘要，正好对上工具层
// 的 snippet.left(800)。
class DeepSeekSearchBackend : public WebSearchBackend {
public:
    QString providerKey() const override { return QStringLiteral("deepseek"); }

    static QJsonObject buildRequest(const QString& query, int maxResults);
    static QList<WebSearchResult> parseResponse(const QByteArray& json,
                                                int maxResults,
                                                QString* errorReason = nullptr);

    QList<WebSearchResult> search(const QString& query,
                                  int maxResults,
                                  const QString& apiKey,
                                  int timeoutMs = 30000,
                                  QString* errorReason = nullptr,
                                  const std::atomic<bool>* cancelled = nullptr) const override;
};

// 一种搜索后端对外的全部元数据。设置 UI、schema 选项、后端工厂与「密钥存
// 在哪个设置键」都只查这张表，避免三处硬编码漂移（对齐
// LlmProtocolRegistry 的既有做法）。
struct WebSearchProviderDescriptor {
    QString key;                 // "duckduckgo"
    QString label;               // 技术名词，跨语言一致；UI 侧按需本地化
    bool requiresApiKey{true};
    QString apiKeySettingKey;    // "webSearch/zhipuApiKey"；无需密钥时为空
};

class WebSearchProviderRegistry {
public:
    static const QVector<WebSearchProviderDescriptor>& descriptors();

    // 未注册返回 nullptr。
    static const WebSearchProviderDescriptor* find(const QString& key);

    // 注册顺序即 UI 展示顺序。
    static QStringList keys();

    // 首装默认后端。DuckDuckGo 免密钥，装上即可用；国内网络下它可能撞上
    // 反爬挑战页，此时工具层会给出明确原因（见 looksLikeAntiBotChallenge），
    // 而不是假装「没搜到」。
    static QString defaultProviderKey();

    // 未知 key 返回 nullptr（调用方据此报告「未配置搜索服务」）。
    static std::unique_ptr<WebSearchBackend> create(const QString& key);

    static QString apiKeySettingKey(const QString& key);
    static bool requiresApiKey(const QString& key);
};

} // namespace NexusAI
