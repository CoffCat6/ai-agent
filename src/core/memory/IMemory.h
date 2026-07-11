#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace NexusAI {

class IMemory {
public:
    virtual ~IMemory() = default;

    virtual void saveMessage(const QString& sessionId, const QJsonObject& msg) = 0;
    virtual QJsonArray loadMessages(const QString& sessionId, int limit = 50) = 0;

    virtual void saveEpisodic(const QString& sessionId, const QJsonObject& episode) = 0;
    virtual QJsonArray searchEpisodic(const QString& query, int limit = 10) = 0;

    virtual void clear(const QString& sessionId) = 0;
};

} // namespace NexusAI
