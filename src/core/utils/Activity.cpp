#include "Activity.h"

#include <QMutex>
#include <QStringList>

Q_LOGGING_CATEGORY(lcApp, "nexus.app")
Q_LOGGING_CATEGORY(lcUser, "nexus.user")
Q_LOGGING_CATEGORY(lcAgent, "nexus.agent")
Q_LOGGING_CATEGORY(lcTool, "nexus.tool")
Q_LOGGING_CATEGORY(lcRag, "nexus.rag")
Q_LOGGING_CATEGORY(lcNetwork, "nexus.network")
Q_LOGGING_CATEGORY(lcDb, "nexus.db")

namespace NexusAI {

namespace {
QMutex g_sinkMutex;
Activity::UsageSink g_usageSink;

const QLoggingCategory& categoryFor(Activity::Kind kind) {
    switch (kind) {
    case Activity::Kind::App:     return lcApp();
    case Activity::Kind::User:    return lcUser();
    case Activity::Kind::Agent:   return lcAgent();
    case Activity::Kind::Tool:    return lcTool();
    case Activity::Kind::Rag:     return lcRag();
    case Activity::Kind::Network: return lcNetwork();
    case Activity::Kind::Db:      return lcDb();
    }
    return lcApp();
}
} // namespace

void Activity::setUsageSink(UsageSink sink) {
    QMutexLocker lock(&g_sinkMutex);
    g_usageSink = std::move(sink);
}

void Activity::report(Kind kind, const QString& message, const QString& contextId) {
    // contextId goes into the message so the LogPanel keyword filter and
    // request correlation can find the line without a schema change.
    const QString line = contextId.isEmpty()
        ? message
        : QStringLiteral("%1 [%2]").arg(message, contextId);
    qCInfo(categoryFor(kind)).noquote() << line;
}

void Activity::usage(Kind kind, const QString& action, const QString& target,
                     quint64 requestId, const QString& detail) {
    report(kind, QStringLiteral("%1 %2").arg(action, target),
           requestId != 0 ? QString::number(requestId) : QString());

    UsageSink sink;
    {
        QMutexLocker lock(&g_sinkMutex);
        sink = g_usageSink;
    }
    if (sink)
        sink(categoryName(kind), action, target, requestId, detail);
}

QString Activity::categoryName(Kind kind) {
    return QString::fromUtf8(categoryFor(kind).categoryName());
}

} // namespace NexusAI
