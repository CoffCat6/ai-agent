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
    // docs/31 O9: 等宽下拉只列真正等宽的字体；isFixedPitch() 单独暴露给
    // 正文下拉做标记/测试断言。
    Q_INVOKABLE bool isFixedPitch(const QString& family) const;
    Q_INVOKABLE QString systemDefaultSans() const;
    Q_INVOKABLE QString systemDefaultMono() const;
    Q_INVOKABLE QStringList availableFamilies() const;
    // 按名称排序的等宽字体；结果缓存（isFixedPitch 会加载字体文件，
    // 搜索框每敲一个字都重算模板会把设置页拖慢）。
    Q_INVOKABLE QStringList fixedPitchFamilies() const;

private:
    static FontProbe* s_instance;
    mutable QStringList m_fixedPitchCache;
};

} // namespace NexusAI
