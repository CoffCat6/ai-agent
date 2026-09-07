#pragma once

#include <QLoggingCategory>
#include <QString>
#include <functional>

Q_DECLARE_LOGGING_CATEGORY(lcApp)
Q_DECLARE_LOGGING_CATEGORY(lcUser)
Q_DECLARE_LOGGING_CATEGORY(lcAgent)
Q_DECLARE_LOGGING_CATEGORY(lcTool)
Q_DECLARE_LOGGING_CATEGORY(lcRag)
Q_DECLARE_LOGGING_CATEGORY(lcNetwork)
Q_DECLARE_LOGGING_CATEGORY(lcDb)

namespace NexusAI {

// Lightweight facade for categorized, correlatable activity events (Batch 12
// 前日志增强). Two channels per event:
//
//   1. Ring buffer + rotated file: via qCInfo under a QLoggingCategory
//      ("nexus.user"/"nexus.network"/...) — LogEntry.category is populated
//      automatically, so LogPanel can filter by event class.
//   2. Persistent usage trail: for user/network events the sink injected via
//      setUsageSink() receives the structured fields (SqliteMemory
//      ::addUsageEvent → usage_log, retention-pruned).
//
// Privacy contract: metadata only (action, target id/name, sizes, ids).
// Never pass prompt text, document content or API keys.
class Activity {
public:
    enum class Kind { App, User, Agent, Tool, Rag, Network, Db };

    // Persistent sink (injected in main.cpp). category is the logging
    // category name ("nexus.user"), matching LogEntry.category.
    using UsageSink = std::function<void(const QString& category, const QString& action,
                                         const QString& target, quint64 requestId,
                                         const QString& detail)>;
    static void setUsageSink(UsageSink sink);

    // One INFO line in the ring buffer/file under the mapped category.
    // contextId (request id, document name, session id) is appended so
    // LogPanel's keyword filter and cross-references can find the event.
    static void report(Kind kind, const QString& message, const QString& contextId = QString());

    // Report AND persist: for user operations and network outcomes that must
    // remain traceable across restarts.
    static void usage(Kind kind, const QString& action, const QString& target = QString(),
                      quint64 requestId = 0, const QString& detail = QString());

    // Logging category name for a kind (also the usage_log.category value).
    static QString categoryName(Kind kind);
};

} // namespace NexusAI
