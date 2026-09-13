#include "EditBuffer.h"
#include "PreferenceStore.h"
#include "PreferenceRegistry.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcEditBuffer, "nexus.preferences.editbuffer")

namespace NexusAI {

EditBuffer* EditBuffer::s_instance = nullptr;

EditBuffer* EditBuffer::instance() {
    if (!s_instance) s_instance = new EditBuffer(nullptr);
    return s_instance;
}

EditBuffer* EditBuffer::create(QQmlEngine*, QJSEngine*) {
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}

EditBuffer::EditBuffer(QObject* parent)
    : QObject(parent) {
}

void EditBuffer::stage(const QString& key, const QVariant& value) {
    clearValidationError();
    // Coerce to the declared preference type: QML numbers arrive as doubles
    // while the store keeps ints, and QVariant equality is type-sensitive -
    // without coercion, staging the stored value would never un-dirty.
    QVariant coerced = value;
    if (const auto* reg = PreferenceRegistry::instance()) {
        if (const auto* meta = reg->meta(key)) {
            switch (meta->type) {
            case PreferenceType::Int:    coerced = value.toInt(); break;
            case PreferenceType::Bool:   coerced = value.toBool(); break;
            case PreferenceType::Enum:   // fall through
            case PreferenceType::String: coerced = value.toString(); break;
            }
        }
    }
    // Staging the value that is already stored un-dirties the buffer instead
    // of keeping a no-op entry alive (e.g. +/- clicked back to the original).
    if (coerced == PreferenceStore::instance()->load(key)) {
        if (m_staged.remove(key) > 0)
            emit stagedChanged();
        emit staged(key, coerced);
        emit dirtyChanged();
        return;
    }
    m_staged[key] = coerced;
    emit staged(key, coerced);
    emit stagedChanged();
    emit dirtyChanged();
}

bool EditBuffer::isDirty() const {
    return !m_staged.isEmpty();
}

bool EditBuffer::validateOne(const QString& key, const QVariant& value, QString& error) const {
    const auto* reg = PreferenceRegistry::instance();
    if (!reg) return true;
    const auto* meta = reg->meta(key);
    if (!meta) return true;

    if (meta->type == PreferenceType::Int && meta->constraint.hasRange) {
        bool ok = false;
        const int v = value.toInt(&ok);
        if (!ok) {
            error = tr("值必须是整数");
            return false;
        }
        if (v < meta->constraint.min || v > meta->constraint.max) {
            error = tr("值必须在 %1-%2 之间").arg(meta->constraint.min).arg(meta->constraint.max);
            return false;
        }
    } else if (meta->type == PreferenceType::Enum) {
        if (!meta->constraint.options.isEmpty()) {
            const QString v = value.toString();
            if (!meta->constraint.options.contains(v)) {
                error = tr("值必须是以下之一: ") + meta->constraint.options.join(", ");
                return false;
            }
        }
    } else if (meta->type == PreferenceType::String) {
        if (meta->constraint.required && value.toString().isEmpty()) {
            error = tr("此字段为必填");
            return false;
        }
        if (meta->constraint.maxLength > 0 && value.toString().length() > meta->constraint.maxLength) {
            error = tr("长度不能超过 %1 个字符").arg(meta->constraint.maxLength);
            return false;
        }
    }
    return true;
}

QVariantMap EditBuffer::validate() const {
    QVariantMap result;
    result.insert(QStringLiteral("valid"), true);
    result.insert(QStringLiteral("firstInvalidKey"), QString());
    result.insert(QStringLiteral("errorMessage"), QString());

    for (auto it = m_staged.constBegin(); it != m_staged.constEnd(); ++it) {
        QString error;
        if (!validateOne(it.key(), it.value(), error)) {
            result[QStringLiteral("valid")] = false;
            result[QStringLiteral("firstInvalidKey")] = it.key();
            result[QStringLiteral("errorMessage")] = error;
            return result;
        }
    }
    return result;
}

bool EditBuffer::commit() {
    if (m_staged.isEmpty()) return true;

    const QVariantMap validation = validate();
    if (!validation.value(QStringLiteral("valid")).toBool()) {
        m_firstInvalidKey = validation.value(QStringLiteral("firstInvalidKey")).toString();
        m_validationError = validation.value(QStringLiteral("errorMessage")).toString();
        emit validationErrorChanged();
        qCWarning(lcEditBuffer) << "commit: validation failed -" << m_validationError;
        return false;
    }

    const bool ok = PreferenceStore::instance()->saveBatch(m_staged);
    if (ok) {
        m_staged.clear();
        clearValidationError();
        emit stagedChanged();
        emit dirtyChanged();
    }
    return ok;
}

void EditBuffer::rollback() {
    m_staged.clear();
    clearValidationError();
    emit stagedChanged();
    emit dirtyChanged();
}

void EditBuffer::resetToDefault(const QStringList& keys) {
    auto* reg = PreferenceRegistry::instance();
    if (!reg) return;
    clearValidationError();
    for (const QString& key : keys)
        m_staged[key] = reg->defaultValue(key);
    emit stagedChanged();
    emit dirtyChanged();
}

void EditBuffer::resetAllToDefault() {
    auto* reg = PreferenceRegistry::instance();
    if (!reg) return;
    m_staged.clear();
    clearValidationError();
    for (const QString& catId : reg->categories()) {
        for (const QString& key : reg->preferencesInCategory(catId))
            m_staged[key] = reg->defaultValue(key);
    }
    emit stagedChanged();
    emit dirtyChanged();
}

QStringList EditBuffer::stagedKeys() const {
    return m_staged.keys();
}

void EditBuffer::clearValidationError() {
    if (m_firstInvalidKey.isEmpty() && m_validationError.isEmpty()) return;
    m_firstInvalidKey.clear();
    m_validationError.clear();
    emit validationErrorChanged();
}

} // namespace NexusAI
