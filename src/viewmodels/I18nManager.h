#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QTranslator>

namespace NexusAI {

class I18nManager : public QObject {
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(I18n)
    Q_PROPERTY(QString currentLocale READ currentLocale NOTIFY localeChanged FINAL)
    Q_PROPERTY(QStringList availableLocales READ availableLocales CONSTANT FINAL)
public:
    explicit I18nManager(QObject* parent);

    static I18nManager* instance();
    static I18nManager* create(QQmlEngine*, QJSEngine*);

    QString currentLocale() const { return m_currentLocale; }
    QStringList availableLocales() const;
    double coverage(const QString& locale) const;

    Q_INVOKABLE void applyLocale(const QString& locale);
    Q_INVOKABLE double localeCoverage(const QString& locale) const { return coverage(locale); }

signals:
    void localeChanged(const QString& locale);

private:
    void registerPreference();
    void subscribeChanges();
    QString detectInitialLocale() const;

    QString m_currentLocale{QStringLiteral("en")};
    QTranslator m_translator;
    bool m_translatorInstalled{false};

    static I18nManager* s_instance;
};

} // namespace NexusAI
