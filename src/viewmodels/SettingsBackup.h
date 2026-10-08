#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QVariantMap>

namespace NexusAI {

// Portable, credential-free settings exchange. SQLite data is intentionally
// outside this format; restored settings take effect after an application restart.
class SettingsBackup : public QObject {
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(SettingsBackup)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged FINAL)
    Q_PROPERTY(bool restartRequired READ restartRequired NOTIFY restartRequiredChanged FINAL)
public:
    explicit SettingsBackup(QObject* parent = nullptr);
    static SettingsBackup* instance();
    static SettingsBackup* create(QQmlEngine*, QJSEngine*);

    QString lastError() const { return m_lastError; }
    bool restartRequired() const { return m_restartRequired; }

    Q_INVOKABLE bool exportSettings(const QString& path);
    Q_INVOKABLE QVariantMap previewImport(const QString& path);
    Q_INVOKABLE bool importSettings(const QString& path);

signals:
    void lastErrorChanged();
    void restartRequiredChanged();

private:
    void setError(const QString& error);
    QVariantMap parseImport(const QString& path, QVariantMap* settings,
                            QVariantList* profiles, QVariantList* servers);

    QString m_lastError;
    bool m_restartRequired{false};
    static SettingsBackup* s_instance;
};

} // namespace NexusAI
