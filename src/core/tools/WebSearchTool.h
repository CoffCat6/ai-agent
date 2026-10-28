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
// WebSearchBackend (DuckDuckGo by default, per WebSearchProviderRegistry).
// The backend is selected via SettingsService::webSearchProvider; each
// provider keeps its own DPAPI-encrypted API key. Results are capped at 6000
// characters (DFX) and the tool respects localOnlyMode gating (REQ-T6-04).
class WebSearchTool : public ITool {
public:
    // DFX: max total characters returned to the model across all results.
    static constexpr int kMaxResultChars = 6000;

    // 默认后端取注册表的默认项（DuckDuckGo），与设置项默认值同源，
    // 避免「构造出的默认」与「设置里的默认」两处漂移。
    explicit WebSearchTool(std::unique_ptr<WebSearchBackend> backend = nullptr)
        : m_backend(backend ? std::shared_ptr<WebSearchBackend>(std::move(backend))
                            : WebSearchProviderRegistry::create(
                                  WebSearchProviderRegistry::defaultProviderKey())) {
        if (!m_backend) m_backend = std::make_shared<DuckDuckGoBackend>();
    }

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
        def.readOnly = true;       // 只读检索：PlanOnly 下仍允许
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

        // 每条返回路径都带上后端标识：用户需要知道这次查询发给了谁——
        // 失败时尤其重要（「请求失败」到底是哪家失败）。
        const QString source = cfg.backend->providerKey();

        const QString query = args.value(QStringLiteral("query")).toString().trimmed();
        if (query.isEmpty())
            return ToolResult::fail(QStringLiteral("query is required"), source);

        if (cfg.backend->requiresApiKey() && cfg.apiKey.isEmpty())
            return ToolResult::fail(QStringLiteral(
                "未配置搜索 API Key（请在设置中心 → 工具 → 联网搜索填写）"), source);

        // 后端自报的未就绪原因优先于网络诊断——否则用户只会看到「请检查
        // 网络连接」而被指错方向。
        const QString notReady = cfg.backend->notReadyReason();
        if (!notReady.isEmpty())
            return ToolResult::fail(notReady, source);

        int maxResults = args.value(QStringLiteral("max_results")).toInt(5);
        maxResults = qBound(1, maxResults, 10);

        // docs/20 §5.3: 把「请求根本没成功」与「请求成功但零结果」分开报。
        // 空结果列表本身无法区分这两者，而把前者说成「没找到结果」会让人
        // 误以为是查询词的问题。
        QString failureReason;
        const QList<WebSearchResult> results =
            cfg.backend->search(query, maxResults, cfg.apiKey, cfg.timeoutMs, &failureReason, &m_cancelRequested);

        if (m_cancelRequested.exchange(false))
            return ToolResult::fail(QStringLiteral("Search cancelled"), source);

        if (!failureReason.isEmpty())
            return ToolResult::fail(failureReason, source);

        if (results.isEmpty())
            // 走到这里说明后端既没抛错也没返回结果（后端应在自己那边写明
            // 原因，如「没搜到」「被反爬拦截」）；这条是兜底文案。
            return ToolResult::fail(
                QStringLiteral("搜索没有返回结果，请调整查询词后重试"), source);

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
                        source, 0,
                        QStringLiteral("query=%1 results=%2").arg(query).arg(results.size()));

        return ToolResult::ok(lines.join(QStringLiteral("\n\n")), source);
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
