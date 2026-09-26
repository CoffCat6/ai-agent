#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

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
    virtual QList<WebSearchResult> search(const QString& query,
                                          int maxResults,
                                          const QString& apiKey,
                                          int timeoutMs = 30000) const = 0;
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
                                  int timeoutMs = 30000) const override;
};

} // namespace NexusAI
