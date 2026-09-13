#include "PreferenceRegistry.h"

#include <QCoreApplication>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcPrefReg, "nexus.preferences.registry")

namespace NexusAI {

namespace {

// Display names are registered as QT_TRANSLATE_NOOP("NexusAI", ...) sources;
// resolving them at query time keeps a runtime language switch effective
// (a name translated at registration would be frozen for the session).
QString translated(const QString& source) {
    return QCoreApplication::translate("NexusAI", source.toUtf8().constData());
}

} // namespace

PreferenceRegistry* PreferenceRegistry::s_instance = nullptr;

PreferenceRegistry* PreferenceRegistry::instance() {
    if (!s_instance) s_instance = new PreferenceRegistry(nullptr);
    return s_instance;
}

PreferenceRegistry* PreferenceRegistry::create(QQmlEngine*, QJSEngine*) {
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}

PreferenceRegistry::PreferenceRegistry(QObject* parent)
    : QObject(parent) {
}

void PreferenceRegistry::registerCategory(const QString& id, const QString& name, int order) {
    if (id.isEmpty()) {
        qCWarning(lcPrefReg) << "registerCategory: empty id";
        return;
    }
    if (m_categories.contains(id)) {
        qCWarning(lcPrefReg) << "registerCategory: duplicate id" << id << "- overwriting";
    }
    CategoryMeta cat;
    cat.id = id;
    cat.name = name;
    cat.order = order;
    m_categories.insert(id, cat);
}

void PreferenceRegistry::registerPreference(const PreferenceMeta& meta) {
    if (meta.key.isEmpty()) {
        qCWarning(lcPrefReg) << "registerPreference: empty key";
        return;
    }
    if (m_preferences.contains(meta.key)) {
        qCWarning(lcPrefReg) << "registerPreference: duplicate key" << meta.key << "- overwriting";
    }
    m_preferences.insert(meta.key, meta);
}

bool PreferenceRegistry::hasPreference(const QString& key) const {
    return m_preferences.contains(key);
}

QVariant PreferenceRegistry::defaultValue(const QString& key) const {
    auto it = m_preferences.constFind(key);
    if (it == m_preferences.constEnd()) return {};
    return it->defaultValue;
}

QString PreferenceRegistry::displayName(const QString& key) const {
    auto it = m_preferences.constFind(key);
    if (it == m_preferences.constEnd()) return {};
    return translated(it->displayName);
}

QVariantMap PreferenceRegistry::preferenceMeta(const QString& key) const {
    const auto* m = meta(key);
    if (!m) return {};

    auto typeToString = [](PreferenceType type) {
        switch (type) {
        case PreferenceType::Bool:   return QStringLiteral("Bool");
        case PreferenceType::Enum:   return QStringLiteral("Enum");
        case PreferenceType::Int:    return QStringLiteral("Int");
        case PreferenceType::String: return QStringLiteral("String");
        }
        return QStringLiteral("String");
    };

    QVariantMap constraint;
    constraint.insert(QStringLiteral("min"), m->constraint.min);
    constraint.insert(QStringLiteral("max"), m->constraint.max);
    constraint.insert(QStringLiteral("hasRange"), m->constraint.hasRange);
    constraint.insert(QStringLiteral("maxLength"), m->constraint.maxLength);
    constraint.insert(QStringLiteral("required"), m->constraint.required);

    QVariantMap map;
    map.insert(QStringLiteral("key"), m->key);
    map.insert(QStringLiteral("type"), typeToString(m->type));
    map.insert(QStringLiteral("displayName"), m->displayName);
    map.insert(QStringLiteral("defaultValue"), m->defaultValue);
    map.insert(QStringLiteral("options"), m->constraint.options);
    map.insert(QStringLiteral("constraint"), constraint);
    return map;
}

QStringList PreferenceRegistry::preferencesInCategory(const QString& categoryId) const {
    QStringList result;
    for (auto it = m_preferences.constBegin(); it != m_preferences.constEnd(); ++it) {
        if (it->categoryId == categoryId)
            result.append(it.key());
    }
    return result;
}

QStringList PreferenceRegistry::categories() const {
    QList<CategoryMeta> sorted;
    for (auto it = m_categories.constBegin(); it != m_categories.constEnd(); ++it)
        sorted.append(it.value());
    std::sort(sorted.begin(), sorted.end(),
              [](const CategoryMeta& a, const CategoryMeta& b) { return a.order < b.order; });
    QStringList result;
    for (const auto& cat : sorted)
        result.append(cat.id);
    return result;
}

QString PreferenceRegistry::categoryName(const QString& id) const {
    auto it = m_categories.constFind(id);
    if (it == m_categories.constEnd()) return {};
    return translated(it->name);
}

int PreferenceRegistry::categoryOrder(const QString& id) const {
    auto it = m_categories.constFind(id);
    if (it == m_categories.constEnd()) return 0;
    return it->order;
}

const PreferenceMeta* PreferenceRegistry::meta(const QString& key) const {
    auto it = m_preferences.constFind(key);
    if (it == m_preferences.constEnd()) return nullptr;
    return &it.value();
}

const CategoryMeta* PreferenceRegistry::category(const QString& id) const {
    auto it = m_categories.constFind(id);
    if (it == m_categories.constEnd()) return nullptr;
    return &it.value();
}

} // namespace NexusAI
