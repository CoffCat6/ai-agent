#include "ChangeBus.h"

namespace NexusAI {

ChangeBus* ChangeBus::s_instance = nullptr;

ChangeBus* ChangeBus::instance() {
    if (!s_instance) s_instance = new ChangeBus(nullptr);
    return s_instance;
}

ChangeBus* ChangeBus::create(QQmlEngine*, QJSEngine*) {
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}

ChangeBus::ChangeBus(QObject* parent)
    : QObject(parent) {
}

void ChangeBus::notify(const QString& key, const QVariant& value) {
    emit changed(key, value);
}

} // namespace NexusAI
