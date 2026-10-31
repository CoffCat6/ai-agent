#pragma once

#include <QObject>
#include <QVariant>
#include <QVariantMap>
#include <QString>
#include <QQmlEngine>

#include "PreferenceRegistry.h"

namespace NexusAI {

struct ValidationResult {
    bool valid{true};
    QString firstInvalidKey;
    QString errorMessage;
};

// Validated write-through between the settings pages and PreferenceStore.
// docs/17（设置统一最终决策）: the settings center no longer batches edits —
// stage() validates against the registry constraints and persists
// immediately, so `dirty` is only ever true transiently (a failed
// validation keeps the rejected value staged for the field's error label;
// the dialog's onClosed rollback discards it). The QML controls bind against
// the `staged` map property — Q_INVOKABLE reads carry no change
// notification, so bindings on them would evaluate exactly once.
class EditBuffer : public QObject {
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(EditBuffer)
    Q_PROPERTY(bool dirty READ isDirty NOTIFY dirtyChanged FINAL)
    Q_PROPERTY(QVariantMap staged READ staged NOTIFY stagedChanged FINAL)
    // Set by a failed commit(), consumed by the per-field error labels;
    // cleared on the next stage/rollback/reset.
    Q_PROPERTY(QString firstInvalidKey READ firstInvalidKey NOTIFY validationErrorChanged FINAL)
    Q_PROPERTY(QString validationError READ validationError NOTIFY validationErrorChanged FINAL)
    // docs/31 S4.3 N3：底栏「已保存」回执可点「撤销」——只保留最后一次写穿
    // 的偏好键（不链式）。密钥类不会进入记录（undoLast 也会再拒一次）。
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged FINAL)
    Q_PROPERTY(QString undoKey READ undoKey NOTIFY undoChanged FINAL)
public:
    explicit EditBuffer(QObject* parent);

    static EditBuffer* instance();
    static EditBuffer* create(QQmlEngine*, QJSEngine*);

    QVariantMap staged() const { return m_staged; }
    QString firstInvalidKey() const { return m_firstInvalidKey; }
    QString validationError() const { return m_validationError; }
    bool canUndo() const { return !m_undoKey.isEmpty(); }
    QString undoKey() const { return m_undoKey; }

    Q_INVOKABLE void stage(const QString& key, const QVariant& value);
    Q_INVOKABLE bool isDirty() const;
    Q_INVOKABLE QVariantMap validate() const;
    Q_INVOKABLE bool commit();
    Q_INVOKABLE void rollback();
    Q_INVOKABLE void resetToDefault(const QStringList& keys);
    Q_INVOKABLE void resetAllToDefault();
    Q_INVOKABLE QStringList stagedKeys() const;
    // 撤销最近一次写穿：有旧落盘值就写回，否则删除物理键回到默认值。
    // 只回写一次（记录随即清空），失败返回 false。
    Q_INVOKABLE bool undoLast();

signals:
    void dirtyChanged();
    void stagedChanged();
    void validationErrorChanged();
    void undoChanged();
    void staged(const QString& key, const QVariant& value);

private:
    bool validateOne(const QString& key, const QVariant& value, QString& error) const;
    void clearValidationError();
    void setUndoRecord(const QString& key, bool hadPrevious, const QVariant& previous);
    void clearUndo();

    QVariantMap m_staged;
    QString m_firstInvalidKey;
    QString m_validationError;
    // 最近一次成功写穿的撤销点（空 key = 无可撤销）。
    QString m_undoKey;
    bool m_undoHadPrevious{false};
    QVariant m_undoPrevious;

    static EditBuffer* s_instance;
};

} // namespace NexusAI
