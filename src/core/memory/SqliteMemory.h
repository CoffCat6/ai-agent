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

    // Token estimation and compression
    static int estimateTokens(const QString& text);
    static int estimateTokens(const QJsonArray& messages);
    QJsonArray compressIfTooLong(const QJsonArray& messages, int maxTokens);

private:
    void initDatabase();
    void createTables();

    QSqlDatabase m_db;
    QString m_dbPath;
};

} // namespace NexusAI
