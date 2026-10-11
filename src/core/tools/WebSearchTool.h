#pragma once

#include "ITool.h"
#include "WebSearchBackend.h"
#include "core/utils/Activity.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>

#include <atomic>
#include <memory>
#include <mutex>

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
        : m_backend(backend ? std::shared_ptr<WebSearchBackend>(std::move(backend))
                            : std::make_shared<TavilyBackend>()) {}

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
        def.autoEditSafe = true;   // 会话"自动编辑"模式放行
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

    // docs/22 F3: setters may run on the GUI thread while execute() runs on
    // a pool thread — all configuration goes through the mutex and callers
    // work on a snapshot, so a swap can never pull the backend out from
    // under an in-flight call (the old backend dies when the snapshot does).
    void setApiKey(const QString& key) {
        std::lock_guard lock(m_configMutex);
        m_apiKey = key;
    }
    // L4: swap the search backend at runtime (driven by webSearchProvider).
    // A nullptr is ignored so a failed factory call never leaves the tool
    // without a backend.
    void setBackend(std::unique_ptr<WebSearchBackend> backend) {
        if (!backend) return;
        std::lock_guard lock(m_configMutex);
        m_backend = std::move(backend);
    }
    void setTimeoutMs(int ms) {
        std::lock_guard lock(m_configMutex);
        m_timeoutMs = qBound(5000, ms, 60000);
    }

    void cancel() override { m_cancelRequested.store(true); }

    ToolResult execute(const QJsonObject& args) override {
        m_cancelRequested.store(false);

        // docs/22 F3: one locked snapshot up front — the whole call runs
        // against it, so the backend cannot be swapped mid-search.
        const Config cfg = snapshotConfig();

        const QString query = args.value(QStringLiteral("query")).toString().trimmed();
        if (query.isEmpty())
            return ToolResult::fail(QStringLiteral("query is required"));

        if (cfg.backend->requiresApiKey() && cfg.apiKey.isEmpty())
            return ToolResult::fail(QStringLiteral("未配置搜索 API key（请在设置中心配置 web_search 密钥）"));

        // docs/20 §5.3: 后端自报的未就绪原因（如 searxng 缺 baseUrl）优先于
        // 网络诊断——否则用户只会看到「请检查网络连接」而被指错方向。
        const QString notReady = cfg.backend->notReadyReason();
        if (!notReady.isEmpty())
            return ToolResult::fail(notReady);

        int maxResults = args.value(QStringLiteral("max_results")).toInt(5);
        maxResults = qBound(1, maxResults, 10);

        // docs/20 §5.3: 把「请求根本没成功」与「请求成功但零结果」分开报。
        // 空结果列表本身无法区分这两者，而把前者说成「没找到结果」会让人
        // 误以为是查询词的问题。
        QString failureReason;
        const QList<WebSearchResult> results =
            cfg.backend->search(query, maxResults, cfg.apiKey, cfg.timeoutMs, &failureReason);

        if (m_cancelRequested.exchange(false))
            return ToolResult::fail(QStringLiteral("Search cancelled"));

        if (!failureReason.isEmpty())
            return ToolResult::fail(failureReason);

        if (results.isEmpty())
            return ToolResult::fail(cfg.backend->requiresApiKey()
                ? QStringLiteral("未找到搜索结果（请检查 API key 或网络连接）")
                : QStringLiteral("未找到搜索结果（请检查网络连接）"));

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
                        cfg.backend->providerKey(), 0,
                        QStringLiteral("query=%1 results=%2").arg(query).arg(results.size()));

        return ToolResult::ok(lines.join(QStringLiteral("\n\n")));
    }

private:
    struct Config {
        std::shared_ptr<WebSearchBackend> backend;
        QString apiKey;
        int timeoutMs;
    };
    // Sole synchronization point between GUI-thread setters and the
    // pool-thread execute() (docs/22 F3).
    Config snapshotConfig() const {
        std::lock_guard lock(m_configMutex);
        return {m_backend, m_apiKey, m_timeoutMs};
    }

    mutable std::mutex m_configMutex;
    std::shared_ptr<WebSearchBackend> m_backend;
    QString m_apiKey;
    int m_timeoutMs{30000};
    std::atomic<bool> m_cancelRequested{false};
};

} // namespace NexusAI
