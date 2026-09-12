#pragma once

#include <QObject>
#include <QVariant>
#include <QQmlEngine>

namespace NexusAI {

class ChangeBus : public QObject {
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(ChangeBus)
public:
    explicit ChangeBus(QObject* parent);

    static ChangeBus* instance();
    static ChangeBus* create(QQmlEngine*, QJSEngine*);

    Q_INVOKABLE void notify(const QString& key, const QVariant& value);

signals:
    void changed(const QString& key, const QVariant& value);

private:
    static ChangeBus* s_instance;
};

} // namespace NexusAI
