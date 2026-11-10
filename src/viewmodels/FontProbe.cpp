#include "FontProbe.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QFont>
#include <QFontInfo>

namespace NexusAI {

FontProbe* FontProbe::s_instance = nullptr;

FontProbe* FontProbe::instance() {
    if (!s_instance) s_instance = new FontProbe(nullptr);
    return s_instance;
}

FontProbe* FontProbe::create(QQmlEngine*, QJSEngine*) {
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}

FontProbe::FontProbe(QObject* parent)
    : QObject(parent) {
}

bool FontProbe::isAvailable(const QString& family) const {
    if (family.isEmpty()) return false;
    return QFontDatabase::hasFamily(family);
}

QString FontProbe::systemDefaultSans() const {
#ifdef Q_OS_WIN
    return QStringLiteral("Segoe UI");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("SF Pro Text");
#else
    // "Sans Serif" 是 fontconfig 的通用别名，不是 QFontDatabase::families()
    // 列出的具体族名——hasFamily() 探空后 ThemeManager 拿不到「可命名」的
    // 默认值（设置页下拉框无法高亮）。defaultFamily() 返回 fontconfig
    // 解析后的具体族名（如 DejaVu Sans / Noto Sans CJK SC），isAvailable()
    // 对它成立（docs/49 D6）。
    return QFont().defaultFamily();
#endif
}

QString FontProbe::systemDefaultMono() const {
#ifdef Q_OS_WIN
    return QStringLiteral("Consolas");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("Menlo");
#else
    // 同上：别名 "Monospace" 不在具体族名清单里。systemFont(FixedFont) 经
    // QFontInfo 解析出实际命中的等宽族（docs/49 D6）。
    return QFontInfo(QFontDatabase::systemFont(QFontDatabase::FixedFont))
        .family();
#endif
}

QStringList FontProbe::availableFamilies() const {
    return QFontDatabase::families();
}

bool FontProbe::isFixedPitch(const QString& family) const {
    if (family.isEmpty()) return false;
    return QFontDatabase::isFixedPitch(family);
}

QStringList FontProbe::fixedPitchFamilies() const {
    if (!m_fixedPitchCache.isEmpty()) return m_fixedPitchCache;
    QStringList families;
    for (const QString& family : QFontDatabase::families()) {
        if (QFontDatabase::isFixedPitch(family)) families.append(family);
    }
    families.sort(Qt::CaseInsensitive);
    m_fixedPitchCache = families;
    return m_fixedPitchCache;
}

} // namespace NexusAI
