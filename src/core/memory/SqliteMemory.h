#pragma once

#include "IMemory.h"

#include <QSqlDatabase>
#include <QString>

namespace NexusAI {

class SqliteMemory : public IMemory {
public:
    explicit SqliteMemory(const QString& dbPath = QString());
    ~SqliteMemory() override;

    void saveMessage(const QString& sessionId, const QJsonObject& msg) override;
    QJsonArray loadMessages(const QString& sessionId, int limit = 50) override;

    void saveEpisodic(const QString& sessionId, const QJsonObject& episode) override;
    QJsonArray searchEpisodic(const QString& query, int limit = 10) override;

    void clear(const QString& sessionId) override;

    // Session bookkeeping
    void beginSession(const QString& sessionId, const QString& title) override;
    void touchSession(const QString& sessionId) override;
    QJsonArray listSessions(int limit = 50) override;
    void deleteSession(const QString& sessionId) override;
    void renameSession(const QString& sessionId, const QString& title) override;

    // Audit trail
    void logAudit(const QString& sessionId, const QString& event,
                  const QString& toolName, const QString& arguments,
                  bool success, const QString& detail) override;
    QJsonArray auditLog(int limit = 100) override;

    // Knowledge-base persistence: imported documents survive restarts.
    // Concrete extension (not part of IMemory); called on the GUI thread.
    void upsertKnowledgeDoc(const QString& name, const QString& content);
    bool removeKnowledgeDoc(const QString& name);
    void clearKnowledgeDocs();
    QJsonObject loadKnowledgeDocs() const; // { name: content }
    QString loadKnowledgeDoc(const QString& name) const; // empty when unknown

    // Trace view persistence (IMemory optional capability)
    void saveTraceNode(const QString& sessionId, const QJsonObject& node, int order) override;
    QJsonArray loadTraceNodes(const QString& sessionId) override;

private:
    void initDatabase();
    void createTables();

    QSqlDatabase m_db;
    QString m_dbPath;
    QString m_connName; // unique per instance: several may coexist (tests)
};

} // namespace NexusAI
