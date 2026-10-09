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
    // Normalize using the same type metadata used for persistence and reset.
    auto* registry = PreferenceRegistry::instance();
    const QVariant coerced = registry ? registry->normalizeValue(key, value) : value;
    // Staging the value that is already stored un-dirties the buffer instead
    // of keeping a no-op entry alive (e.g. +/- clicked back to the original).
    if (!m_staged.contains(key)
            && coerced == PreferenceStore::instance()->load(key)) {
        if (m_staged.remove(key) > 0)
            emit stagedChanged();
        emit staged(key, coerced);
        emit dirtyChanged();
        return;
    }

    // docs/17（设置统一最终决策）: 即时保存语义——校验通过立即写穿，设置
    // 中心不再有"保存/取消"批次。校验失败时保留 staged 供字段级错误显示，
    // 对话框关闭时 onClosed 的 rollback 兜底丢弃。
    QString error;
    if (!validateOne(key, coerced, error)) {
        m_firstInvalidKey = key;
        m_validationError = error;
        m_staged[key] = coerced;
        emit validationErrorChanged();
        emit staged(key, coerced);
        emit stagedChanged();
        emit dirtyChanged();
        qCWarning(lcEditBuffer) << "stage: validation failed -" << error;
        return;
    }
    if (!PreferenceStore::instance()->saveBatch({{key, coerced}})) {
        m_staged[key] = coerced;
        emit staged(key, coerced);
        emit stagedChanged();
        emit dirtyChanged();
        return;
    }
    m_staged.remove(key);
    emit staged(key, coerced);
    emit stagedChanged();
    emit dirtyChanged();
}

bool EditBuffer::isDirty() const {
    return !m_staged.isEmpty();
}

bool EditBuffer::validateOne(const QString& key, const QVariant& value, QString& error) const {
    const auto* registry = PreferenceRegistry::instance();
    return !registry || registry->validateValue(key, value, error);
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

// docs/17: 重置同样立即写穿——把默认值批量落盘并清空暂存。
void EditBuffer::resetToDefault(const QStringList& keys) {
    auto* reg = PreferenceRegistry::instance();
    if (!reg) return;
    QVariantMap batch;
    for (const QString& key : keys)
        batch[key] = reg->defaultValue(key);
    clearValidationError();
    if (!batch.isEmpty() && !PreferenceStore::instance()->saveBatch(batch)) {
        m_staged = batch;
        emit stagedChanged();
        emit dirtyChanged();
        return;
    }
    m_staged.clear();
    emit stagedChanged();
    emit dirtyChanged();
}

void EditBuffer::resetAllToDefault() {
    auto* reg = PreferenceRegistry::instance();
    if (!reg) return;
    QVariantMap batch;
    for (const QString& catId : reg->categories()) {
        for (const QString& key : reg->preferencesInCategory(catId))
            batch[key] = reg->defaultValue(key);
    }
    clearValidationError();
    if (!batch.isEmpty() && !PreferenceStore::instance()->saveBatch(batch)) {
        m_staged = batch;
        emit stagedChanged();
        emit dirtyChanged();
        return;
    }
    m_staged.clear();
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
