#pragma once

#include "ITool.h"
#include "WebSearchBackend.h"
#include "core/utils/Activity.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>

#include <atomic>
#include <memory>

namespace NexusAI {

// REQ-T6-01..06: web search tool. ReAct-callable wrapper around a
// WebSearchBackend (Tavily by default). The backend is selected via
// SettingsService::webSearchProvider; the API key is stored via DPAPI
// encryption. Results are capped at 6000 characters (DFX) and the tool
// respects localOnlyMode gating (REQ-T6-04).
class WebSearchTool : public ITool {
public:
    // DFX: max total characters returned to the model across all results.
    static constexpr int kMaxResultChars = 6000;

    explicit WebSearchTool(std::unique_ptr<WebSearchBackend> backend = nullptr)
        : m_backend(backend ? std::move(backend)
                             : std::make_unique<TavilyBackend>()) {}

    ToolDefinition definition() const override {
        ToolDefinition def;
        def.name = QStringLiteral("web_search");
        def.description =
            "Search the web for current information (news, facts, prices, "
            "weather, documentation). Returns a list of results with title, "
            "URL and snippet. Privacy notice: the query is sent to the "
            "configured search provider; only use it when the current user "
            "message needs live web content that the model cannot answer.";
        def.level = ToolLevel::RequiresConfirm;
        def.parameters = QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"query", QJsonObject{
                    {"type", "string"},
                    {"description", "Search query (what you would type into a search engine)."}}},
                {"max_results", QJsonObject{
                    {"type", "integer"},
                    {"description", "Maximum number of results to return (1-10, default 5)."}}}
            }},
            {"required", QJsonArray{QJsonValue(QStringLiteral("query"))}}
        };
        return def;
    }

    void setApiKey(const QString& key) { m_apiKey = key; }
    void setTimeoutMs(int ms) { m_timeoutMs = qBound(5000, ms, 60000); }

    void cancel() override { m_cancelRequested.store(true); }

    ToolResult execute(const QJsonObject& args) override {
        m_cancelRequested.store(false);

        const QString query = args.value(QStringLiteral("query")).toString().trimmed();
        if (query.isEmpty())
            return ToolResult::fail(QStringLiteral("query is required"));

        if (m_apiKey.isEmpty())
            return ToolResult::fail(QStringLiteral("未配置搜索 API key（请在设置中心配置 web_search 密钥）"));

        int maxResults = args.value(QStringLiteral("max_results")).toInt(5);
        maxResults = qBound(1, maxResults, 10);

        const QList<WebSearchResult> results =
            m_backend->search(query, maxResults, m_apiKey, m_timeoutMs);

        if (m_cancelRequested.exchange(false))
            return ToolResult::fail(QStringLiteral("Search cancelled"));

        if (results.isEmpty())
            return ToolResult::fail(QStringLiteral("未找到搜索结果（请检查 API key 或网络连接）"));

        // Format results as a numbered list and cap total characters.
        QStringList lines;
        int totalChars = 0;
        for (int i = 0; i < results.size(); ++i) {
            const auto& r = results[i];
            const QString snippet = r.snippet.isEmpty()
                ? QStringLiteral("(no snippet)")
                : r.snippet.left(800);
            const QString entry = QStringLiteral("%1. **%2**\n   URL: %3\n   %4")
                                      .arg(i + 1)
                                      .arg(r.title.isEmpty() ? r.url : r.title,
                                           r.url,
                                           snippet);
            if (totalChars + entry.size() > kMaxResultChars) {
                if (lines.isEmpty()) {
                    // First entry already exceeds the cap - truncate it.
                    lines.append(entry.left(kMaxResultChars));
                }
                lines.append(QStringLiteral(
                    "\n…[results truncated to %1 characters]").arg(kMaxResultChars));
                break;
            }
            lines.append(entry);
            totalChars += entry.size();
        }

        Activity::usage(Activity::Kind::Network, QStringLiteral("web_search"),
                        m_backend->providerKey(), 0,
                        QStringLiteral("query=%1 results=%2").arg(query).arg(results.size()));

        return ToolResult::ok(lines.join(QStringLiteral("\n\n")));
    }

private:
    std::unique_ptr<WebSearchBackend> m_backend;
    QString m_apiKey;
    int m_timeoutMs{30000};
    std::atomic<bool> m_cancelRequested{false};
};

} // namespace NexusAI
