#pragma once

#include <QObject>
#include <QVariant>
#include <QQmlEngine>

#include "PreferenceRegistry.h"

namespace NexusAI {

class ChangeBus;

class PreferenceStore : public QObject {
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(PreferenceStore)
public:
    explicit PreferenceStore(QObject* parent);

    static PreferenceStore* instance();
    static PreferenceStore* create(QQmlEngine*, QJSEngine*);

    Q_INVOKABLE QVariant load(const QString& key) const;
    Q_INVOKABLE bool hasStoredValue(const QString& key) const;
    Q_INVOKABLE bool save(const QString& key, const QVariant& value);
    Q_INVOKABLE bool saveBatch(const QVariantMap& entries);
    Q_INVOKABLE bool reset(const QString& key);
    Q_INVOKABLE bool resetBatch(const QStringList& keys);

    void setChangeBus(ChangeBus* bus);

signals:
    void persistenceError(const QString& message);

private:
    QVariant loadWithLegacy(const QString& key) const;
    QVariant coerce(const QString& key, const QVariant& value) const;
    QVariant convertLegacyValue(const QString& key, const QVariant& value) const;

    ChangeBus* m_changeBus{nullptr};

    static PreferenceStore* s_instance;
};

} // namespace NexusAI
