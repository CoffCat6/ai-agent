#include "PreferenceRegistry.h"
#include "core/utils/SettingsSchema.h"

#include <QCoreApplication>
#include <QLoggingCategory>

#include <algorithm>

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
    for (const CategoryGroupMeta& group : SettingsSchema::categoryGroups())
        registerGroup(group);
    for (const CategoryMeta& category : SettingsSchema::categories())
        registerCategory(category.id, category.name, category.order, category.groupId);
    for (const PreferenceMeta& preference : SettingsSchema::preferences())
        registerPreference(preference);
}

void PreferenceRegistry::registerGroup(const CategoryGroupMeta& meta) {
    if (meta.id.isEmpty()) {
        qCWarning(lcPrefReg) << "registerGroup: empty id";
        return;
    }
    if (m_groups.contains(meta.id)) {
        qCWarning(lcPrefReg) << "registerGroup: duplicate id" << meta.id << "- ignoring";
        return;
    }
    m_groups.insert(meta.id, meta);
}

void PreferenceRegistry::registerCategory(const QString& id, const QString& name, int order,
                                          const QString& groupId) {
    if (id.isEmpty()) {
        qCWarning(lcPrefReg) << "registerCategory: empty id";
        return;
    }
    if (m_categories.contains(id)) {
        qCWarning(lcPrefReg) << "registerCategory: duplicate id" << id << "- ignoring";
        return;
    }
    if (!groupId.isEmpty() && !m_groups.contains(groupId)) {
        qCWarning(lcPrefReg) << "registerCategory: unknown group" << groupId
                             << "for" << id << "- category will be ungrouped";
    }
    CategoryMeta cat;
    cat.id = id;
    cat.name = name;
    cat.order = order;
    cat.groupId = groupId;
    m_categories.insert(id, cat);
}

void PreferenceRegistry::registerPreference(const PreferenceMeta& meta) {
    if (meta.key.isEmpty()) {
        qCWarning(lcPrefReg) << "registerPreference: empty key";
        return;
    }
    if (m_preferences.contains(meta.key)) {
        qCWarning(lcPrefReg) << "registerPreference: duplicate key" << meta.key << "- ignoring";
        return;
    }
    m_preferences.insert(meta.key, meta);
}

bool PreferenceRegistry::setDefaultValue(const QString& key, const QVariant& value) {
    auto it = m_preferences.find(key);
    if (it == m_preferences.end()) return false;
    it->defaultValue = value;
    return true;
}

QVariant PreferenceRegistry::normalizeValue(const QString& key, const QVariant& value) const {
    const PreferenceMeta* m = meta(key);
    if (!m) return value;

    switch (m->type) {
    case PreferenceType::Bool:
        if (value.typeId() == QMetaType::Bool) return value;
        if (value.typeId() == QMetaType::QString) {
            const QString text = value.toString().trimmed().toLower();
            if (text == QLatin1String("true") || text == QLatin1String("1"))
                return true;
            if (text == QLatin1String("false") || text == QLatin1String("0"))
                return false;
        }
        return value;
    case PreferenceType::Int: {
        bool ok = false;
        const int integer = value.toInt(&ok);
        return ok ? QVariant(integer) : value;
    }
    case PreferenceType::Enum:
    case PreferenceType::String:
        return value.typeId() == QMetaType::QString ? value : QVariant(value.toString());
    }
    return value;
}

QVariant PreferenceRegistry::coerceStoredValue(const QString& key, const QVariant& value) const {
    const QVariant normalized = normalizeValue(key, value);
    QString error;
    return validateValue(key, normalized, error) ? normalized : defaultValue(key);
}

bool PreferenceRegistry::validateValue(const QString& key, const QVariant& value,
                                       QString& error) const {
    const PreferenceMeta* m = meta(key);
    if (!m) return true;

    switch (m->type) {
    case PreferenceType::Bool:
        if (value.typeId() != QMetaType::Bool) {
            error = QCoreApplication::translate("EditBuffer", "值必须是布尔值");
            return false;
        }
        break;
    case PreferenceType::Int: {
        bool ok = false;
        const int integer = value.toInt(&ok);
        if (!ok) {
            error = QCoreApplication::translate("EditBuffer", "值必须是整数");
            return false;
        }
        if (m->constraint.hasRange
                && (integer < m->constraint.min || integer > m->constraint.max)) {
            error = QCoreApplication::translate("EditBuffer", "值必须在 %1-%2 之间")
                        .arg(m->constraint.min).arg(m->constraint.max);
            return false;
        }
        break;
    }
    case PreferenceType::Enum:
        if (!m->constraint.options.isEmpty()
                && value.toString() != m->defaultValue.toString()
                && !m->constraint.options.contains(value.toString())) {
            error = QCoreApplication::translate("EditBuffer", "值必须是以下之一: ")
                + m->constraint.options.join(", ");
            return false;
        }
        break;
    case PreferenceType::String:
        if (m->constraint.required && value.toString().isEmpty()) {
            error = QCoreApplication::translate("EditBuffer", "此字段为必填");
            return false;
        }
        if (m->constraint.maxLength > 0
                && value.toString().length() > m->constraint.maxLength) {
            error = QCoreApplication::translate("EditBuffer", "长度不能超过 %1 个字符")
                        .arg(m->constraint.maxLength);
            return false;
        }
        break;
    }
    return true;
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

QStringList PreferenceRegistry::categoryGroups() const {
    QList<CategoryGroupMeta> sorted;
    for (auto it = m_groups.constBegin(); it != m_groups.constEnd(); ++it)
        sorted.append(it.value());
    std::sort(sorted.begin(), sorted.end(),
              [](const CategoryGroupMeta& a, const CategoryGroupMeta& b) {
                  return a.order < b.order;
              });
    QStringList result;
    for (const auto& group : sorted)
        result.append(group.id);
    return result;
}

QString PreferenceRegistry::categoryGroupName(const QString& groupId) const {
    auto it = m_groups.constFind(groupId);
    if (it == m_groups.constEnd()) return {};
    return translated(it->name);
}

QString PreferenceRegistry::categoryGroup(const QString& categoryId) const {
    auto it = m_categories.constFind(categoryId);
    if (it == m_categories.constEnd()) return {};
    return it->groupId;
}

QStringList PreferenceRegistry::categoriesInGroup(const QString& groupId) const {
    QList<CategoryMeta> sorted;
    for (auto it = m_categories.constBegin(); it != m_categories.constEnd(); ++it) {
        if (it->groupId == groupId)
            sorted.append(it.value());
    }
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

const CategoryGroupMeta* PreferenceRegistry::group(const QString& id) const {
    auto it = m_groups.constFind(id);
    if (it == m_groups.constEnd()) return nullptr;
    return &it.value();
}

} // namespace NexusAI
