#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QQmlEngine>

namespace NexusAI {

class FontProbe : public QObject {
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(FontProbe)
public:
    explicit FontProbe(QObject* parent);

    static FontProbe* instance();
    static FontProbe* create(QQmlEngine*, QJSEngine*);

    Q_INVOKABLE bool isAvailable(const QString& family) const;
    Q_INVOKABLE QString systemDefaultSans() const;
    Q_INVOKABLE QString systemDefaultMono() const;
    Q_INVOKABLE QStringList availableFamilies() const;

private:
    static FontProbe* s_instance;
};

} // namespace NexusAI
