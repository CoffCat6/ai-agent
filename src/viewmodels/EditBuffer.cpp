#include "EditBuffer.h"
#include "PreferenceStore.h"
#include "PreferenceRegistry.h"
#include "SettingsValueFormat.h"

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
    auto* store = PreferenceStore::instance();
    const QVariant coerced = registry ? registry->normalizeValue(key, value) : value;
    // Staging the value that is already stored un-dirties the buffer instead
    // of keeping a no-op entry alive (e.g. +/- clicked back to the original).
    if (!m_staged.contains(key)
            && coerced == store->load(key)) {
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
    // docs/31 S4.3 N3：撤销点必须在写盘**之前**登记——PreferenceStore::saved
    // 在 saveBatch 内同步发出，设置中心的回执槽要能立刻看到"这次改动可撤销"。
    // 写盘失败时恢复旧记录（含"原本没有"），不把没落盘的提交当成可撤销。
    const QString previousUndoKey = m_undoKey;
    const bool previousUndoHad = m_undoHadPrevious;
    const QVariant previousUndoValue = m_undoPrevious;
    setUndoRecord(key, store->hasStoredValue(key), store->load(key));
    if (!store->saveBatch({{key, coerced}})) {
        setUndoRecord(previousUndoKey, previousUndoHad, previousUndoValue);
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
        // 批量提交不提供撤销（无法用一次回写表达多个键），旧的单键撤销点
        // 也一并作废，避免撤销到更早的一次改动。
        clearUndo();
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
// docs/31 S4.3 N3：复位入口清空撤销记录（一次复位覆盖了此前所有改动，
// 再撤销只会把状态带回一个半复位中间态）。
void EditBuffer::resetToDefault(const QStringList& keys) {
    clearUndo();
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
    clearUndo();
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

// docs/31 S4.3 N3：只撤销最近一次写穿，且只回写一次（不链式）。
bool EditBuffer::undoLast() {
    if (m_undoKey.isEmpty()) return false;
    const QString key = m_undoKey;
    const bool hadPrevious = m_undoHadPrevious;
    const QVariant previous = m_undoPrevious;

    // 防御：只撤销已登记的偏好键，且密钥类永不回写（红线 1：密钥不进入
    // 内存缓存/落盘回写路径，撤销入口也不会成为例外）。
    auto* registry = PreferenceRegistry::instance();
    if (!registry || !registry->hasPreference(key)
            || SettingsValue::inspect(key).secret) {
        clearUndo();
        return false;
    }

    // 先清记录：撤销只回写一次，写盘失败也不留下"还能再撤销"的假象。
    clearUndo();
    auto* store = PreferenceStore::instance();
    const bool ok = hadPrevious ? store->save(key, previous) : store->reset(key);
    if (ok) {
        // 撤销没有改动 staged 映射，但控件是按 `EditBuffer.staged` 的
        // NOTIFY 重新求值的（stagedValue() 再回落到 PreferenceStore.load）；
        // 主动广播一次让滑块/下拉/开关同步到恢复后的值。
        m_staged.remove(key);
        emit staged(key, store->load(key));
        emit stagedChanged();
        emit dirtyChanged();
    }
    return ok;
}

void EditBuffer::setUndoRecord(const QString& key, bool hadPrevious,
                               const QVariant& previous) {
    if (key.isEmpty()) {
        clearUndo();
        return;
    }
    if (m_undoKey == key && m_undoHadPrevious == hadPrevious
            && m_undoPrevious == previous)
        return;
    m_undoKey = key;
    m_undoHadPrevious = hadPrevious;
    m_undoPrevious = previous;
    emit undoChanged();
}

void EditBuffer::clearUndo() {
    if (m_undoKey.isEmpty()) return;
    m_undoKey.clear();
    m_undoHadPrevious = false;
    m_undoPrevious.clear();
    emit undoChanged();
}

void EditBuffer::clearValidationError() {
    if (m_firstInvalidKey.isEmpty() && m_validationError.isEmpty()) return;
    m_firstInvalidKey.clear();
    m_validationError.clear();
    emit validationErrorChanged();
}

} // namespace NexusAI
