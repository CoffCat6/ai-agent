#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QQmlEngine>

namespace NexusAI {

class LocaleProbe : public QObject {
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(LocaleProbe)
public:
    explicit LocaleProbe(QObject* parent);

    static LocaleProbe* instance();
    static LocaleProbe* create(QQmlEngine*, QJSEngine*);

    Q_INVOKABLE QStringList availableLocales() const;

private:
    static LocaleProbe* s_instance;
};

} // namespace NexusAI
