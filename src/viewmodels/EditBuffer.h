#pragma once

#include <QObject>
#include <QVariant>
#include <QVariantMap>
#include <QString>
#include <QQmlEngine>

#include "PreferenceRegistry.h"

namespace NexusAI {

struct ValidationResult {
    bool valid{true};
    QString firstInvalidKey;
    QString errorMessage;
};

// Staging area between the settings pages and PreferenceStore. QML controls
// must bind against the `staged` map property - Q_INVOKABLE reads carry no
// change notification, so bindings on them evaluate exactly once.
class EditBuffer : public QObject {
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(EditBuffer)
    Q_PROPERTY(bool dirty READ isDirty NOTIFY dirtyChanged FINAL)
    Q_PROPERTY(QVariantMap staged READ staged NOTIFY stagedChanged FINAL)
    // Set by a failed commit(), consumed by the per-field error labels;
    // cleared on the next stage/rollback/reset.
    Q_PROPERTY(QString firstInvalidKey READ firstInvalidKey NOTIFY validationErrorChanged FINAL)
    Q_PROPERTY(QString validationError READ validationError NOTIFY validationErrorChanged FINAL)
public:
    explicit EditBuffer(QObject* parent);

    static EditBuffer* instance();
    static EditBuffer* create(QQmlEngine*, QJSEngine*);

    QVariantMap staged() const { return m_staged; }
    QString firstInvalidKey() const { return m_firstInvalidKey; }
    QString validationError() const { return m_validationError; }

    Q_INVOKABLE void stage(const QString& key, const QVariant& value);
    Q_INVOKABLE bool isDirty() const;
    Q_INVOKABLE QVariantMap validate() const;
    Q_INVOKABLE bool commit();
    Q_INVOKABLE void rollback();
    Q_INVOKABLE void resetToDefault(const QStringList& keys);
    Q_INVOKABLE void resetAllToDefault();
    Q_INVOKABLE QStringList stagedKeys() const;

signals:
    void dirtyChanged();
    void stagedChanged();
    void validationErrorChanged();
    void staged(const QString& key, const QVariant& value);

private:
    bool validateOne(const QString& key, const QVariant& value, QString& error) const;
    void clearValidationError();

    QVariantMap m_staged;
    QString m_firstInvalidKey;
    QString m_validationError;

    static EditBuffer* s_instance;
};

} // namespace NexusAI
