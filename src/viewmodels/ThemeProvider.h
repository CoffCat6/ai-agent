#pragma once

#include <QColor>
#include <QDateTime>
#include <QLocale>
#include <QQmlEngine>
#include <QObject>

#include "ThemeManager.h"

namespace NexusAI {

class ThemeProvider : public QObject {
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(Theme)
public:
    explicit ThemeProvider(ThemeManager* manager, QObject* parent = nullptr)
        : QObject(parent), m_manager(manager) {
        connect(m_manager, &ThemeManager::themeChanged, this, [this]() {
            emit themeChanged();
        });
        connect(m_manager, &ThemeManager::fontChanged, this, [this]() {
            emit themeChanged();
        });
        connect(m_manager, &ThemeManager::motionChanged, this, [this]() {
            emit motionChanged();
        });
    }

    static ThemeProvider* instance() {
        if (!s_instance) s_instance = new ThemeProvider(ThemeManager::instance());
        return s_instance;
    }
    static ThemeProvider* create(QQmlEngine*, QJSEngine*) {
        QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
        return instance();
    }

    Q_PROPERTY(bool isDark READ isDark NOTIFY themeChanged FINAL)
    bool isDark() const { return m_manager->isDark(); }

    Q_PROPERTY(QColor bgPrimary READ bgPrimary NOTIFY themeChanged FINAL)
    QColor bgPrimary() const {
        return m_manager->isDark() ? "#0A0A0F" : "#F5F7FA";
    }

    Q_PROPERTY(QColor bgSecondary READ bgSecondary NOTIFY themeChanged FINAL)
    QColor bgSecondary() const {
        return m_manager->isDark() ? QColor("#13131A") : QColor("#FFFFFF");
    }

    Q_PROPERTY(QColor surfaceHover READ surfaceHover NOTIFY themeChanged FINAL)
    QColor surfaceHover() const {
        return m_manager->isDark() ? "#1C1C26" : "#EBEDF0";
    }

    Q_PROPERTY(QColor borderSubtle READ borderSubtle NOTIFY themeChanged FINAL)
    QColor borderSubtle() const {
        return m_manager->isDark() ? "#2A2A3A" : "#E5E8EB";
    }

    Q_PROPERTY(QColor textPrimary READ textPrimary NOTIFY themeChanged FINAL)
    QColor textPrimary() const {
        return m_manager->isDark() ? "#E4E4ED" : "#1F2329";
    }

    Q_PROPERTY(QColor textSecondary READ textSecondary NOTIFY themeChanged FINAL)
    QColor textSecondary() const {
        // 亮色侧旧值 #8F959E 在白底上仅 3.02:1，不达 WCAG AA（docs/19 D12）；
        // 下调为 #5C6673（白底 5.83:1、bgPrimary 5.43:1、卡片头 4.96:1）。
        return m_manager->isDark() ? QColor("#8A8A9F") : QColor("#5C6673");
    }

    Q_PROPERTY(QColor brandPrimary READ brandPrimary NOTIFY themeChanged FINAL)
    QColor brandPrimary() const {
        return m_manager->isDark() ? "#00F0FF" : "#0052D9";
    }

    Q_PROPERTY(QColor statusSuccess READ statusSuccess NOTIFY themeChanged FINAL)
    QColor statusSuccess() const {
        return m_manager->isDark() ? "#00FF94" : "#00A870";
    }

    Q_PROPERTY(QColor statusWarning READ statusWarning NOTIFY themeChanged FINAL)
    QColor statusWarning() const {
        return m_manager->isDark() ? "#FFB800" : "#ED7B2F";
    }

    Q_PROPERTY(QColor statusDanger READ statusDanger NOTIFY themeChanged FINAL)
    QColor statusDanger() const {
        return m_manager->isDark() ? "#FF3B30" : "#F5222D";
    }

    // 阴影参数（docs/19 §5.4 / D10）：当前未接入渲染。
    // 曾计划用 QtQuick.Effects 给气泡/卡片画轻阴影，因开闭项需 FBO 纹理缓存、
    // 且 RectangularShadow 仅 Qt 6.9+（项目下限 6.5），暂不采用，改用
    // 「边框 + 底色分层」的零特效方案（见 docs/19 §5.4）。
    // 令牌保留，供日后确有阴影需求时直接取用。
    Q_PROPERTY(int elevationRadius READ elevationRadius NOTIFY themeChanged FINAL)
    int elevationRadius() const {
        return m_manager->isDark() ? 0 : 8;
    }

    Q_PROPERTY(QColor elevationColor READ elevationColor NOTIFY themeChanged FINAL)
    QColor elevationColor() const {
        // #AARRGGBB：暗色全透明；亮色约 6% 黑
        return m_manager->isDark() ? QColor("transparent") : QColor("#10000000");
    }

    // ── Bubble / card surfaces (docs/19 §3.2) ──────────
    // 用户气泡 / 助手气泡 / 过程卡片 / 代码块的分层底色。气泡靠对齐、
    // 圆角与头像标识区分，底色对比度天然偏低（见 docs/19 注①）。
    Q_PROPERTY(QColor bubbleUser READ bubbleUser NOTIFY themeChanged FINAL)
    QColor bubbleUser() const {
        return m_manager->isDark() ? QColor("#14333D") : QColor("#E8F1FF");
    }

    Q_PROPERTY(QColor bubbleAssistant READ bubbleAssistant NOTIFY themeChanged FINAL)
    QColor bubbleAssistant() const {
        return m_manager->isDark() ? QColor("#13131A") : QColor("#FFFFFF");
    }

    Q_PROPERTY(QColor cardBackground READ cardBackground NOTIFY themeChanged FINAL)
    QColor cardBackground() const {
        return m_manager->isDark() ? QColor("#111118") : QColor("#FFFFFF");
    }

    Q_PROPERTY(QColor cardHeaderBackground READ cardHeaderBackground NOTIFY themeChanged FINAL)
    QColor cardHeaderBackground() const {
        return m_manager->isDark() ? QColor("#171722") : QColor("#E9EDF2");
    }

    Q_PROPERTY(QColor cardBorder READ cardBorder NOTIFY themeChanged FINAL)
    QColor cardBorder() const {
        return m_manager->isDark() ? QColor("#3A3A52") : QColor("#CBD2DA");
    }

    Q_PROPERTY(QColor codeBackground READ codeBackground NOTIFY themeChanged FINAL)
    QColor codeBackground() const {
        return m_manager->isDark() ? QColor("#0D0D13") : QColor("#F2F4F7");
    }

    // ── Control states (docs/44 §3.1) ─────────────────
    // Qt Quick Controls 样式层（src/ui/style/NexusStyle）的四态专用色。
    // 现有 surfaceHover 只有 hover 一档；pressed / disabled / 品牌填充态若
    // 由每个控件各自 Qt.lighter/darker 派生，17 个控件样式文件（+ NexusMetrics.qml）必然漂移，故收口。
    Q_PROPERTY(QColor controlBackgroundPressed READ controlBackgroundPressed NOTIFY themeChanged FINAL)
    QColor controlBackgroundPressed() const {
        return m_manager->isDark() ? QColor("#20202C") : QColor("#DEE2E7");
    }

    Q_PROPERTY(QColor controlBackgroundDisabled READ controlBackgroundDisabled NOTIFY themeChanged FINAL)
    QColor controlBackgroundDisabled() const {
        return m_manager->isDark() ? QColor("#101016") : QColor("#F2F3F5");
    }

    Q_PROPERTY(QColor controlBorderDisabled READ controlBorderDisabled NOTIFY themeChanged FINAL)
    QColor controlBorderDisabled() const {
        return m_manager->isDark() ? QColor("#262631") : QColor("#E8EBEF");
    }

    Q_PROPERTY(QColor controlTextDisabled READ controlTextDisabled NOTIFY themeChanged FINAL)
    QColor controlTextDisabled() const {
        return m_manager->isDark() ? QColor("#55556A") : QColor("#A0A6AE");
    }

    Q_PROPERTY(QColor controlBrandHover READ controlBrandHover NOTIFY themeChanged FINAL)
    QColor controlBrandHover() const {
        return m_manager->isDark() ? QColor("#33F3FF") : QColor("#1A63DD");
    }

    Q_PROPERTY(QColor controlBrandPressed READ controlBrandPressed NOTIFY themeChanged FINAL)
    QColor controlBrandPressed() const {
        return m_manager->isDark() ? QColor("#00C4D1") : QColor("#0047BE");
    }

    // 极弱提示色：仅承载冗余信息（已折叠提示、耗时），对比度允许 3:1~4.5:1。
    Q_PROPERTY(QColor textTertiary READ textTertiary NOTIFY themeChanged FINAL)
    QColor textTertiary() const {
        return m_manager->isDark() ? QColor("#6E6E85") : QColor("#8A9099");
    }

    // 错误卡片底色（docs/19 §4.3 ErrorCard）。
    Q_PROPERTY(QColor statusDangerSurface READ statusDangerSurface NOTIFY themeChanged FINAL)
    QColor statusDangerSurface() const {
        return m_manager->isDark() ? QColor("#2A1618") : QColor("#FFF1F0");
    }

    // ── docs/25 C1: 统一时间格式化出口 ─────────────────
    // QML 各处原先直接 Qt.formatDateTime 且 hh/HH 口径不一（12/24 小时
    // 混排：记忆页/日志/审计为 12h，消息气泡为 24h）。统一收口到 Theme
    // 单例：内部走 QLocale（等价 QML 的 Qt.locale()，数字排版随界面
    // locale），一律 24 小时制，与日志/审计口径一致。
    Q_INVOKABLE QString formatDateTime(const QDateTime& dt) const {
        // 完整时间戳（记忆条目等）：yyyy-MM-dd HH:mm
        return QLocale().toString(dt, QStringLiteral("yyyy-MM-dd HH:mm"));
    }
    Q_INVOKABLE QString formatDateTimeSec(const QDateTime& dt) const {
        // 带秒时间戳（审计导出等）：yyyy-MM-dd HH:mm:ss
        return QLocale().toString(dt, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    }
    Q_INVOKABLE QString formatTime(const QDateTime& dt) const {
        // 消息气泡短时间：HH:mm
        return QLocale().toString(dt, QStringLiteral("HH:mm"));
    }
    Q_INVOKABLE QString formatLogTime(const QDateTime& dt) const {
        // 日志面板紧凑时间：MM-dd HH:mm:ss
        return QLocale().toString(dt, QStringLiteral("MM-dd HH:mm:ss"));
    }

    // ── docs/25 C6: 文本选区前景色 token ───────────────
    // 10+ 处 QML 硬编码 selectedTextColor: "#ffffff" 的统一替代；与
    // selectionColor: Theme.brandPrimary 搭配（白字对品牌色底满足对比度）。
    Q_PROPERTY(QColor selectedTextColor READ selectedTextColor CONSTANT FINAL)
    QColor selectedTextColor() const { return QColor("#FFFFFF"); }

    // ── Spacing scale (docs/19 §3.3, theme-independent) ──
    Q_PROPERTY(int spaceXs READ spaceXs CONSTANT FINAL)
    int spaceXs() const { return 4; }

    Q_PROPERTY(int spaceSm READ spaceSm CONSTANT FINAL)
    int spaceSm() const { return 8; }

    Q_PROPERTY(int spaceMd READ spaceMd CONSTANT FINAL)
    int spaceMd() const { return 12; }

    Q_PROPERTY(int spaceLg READ spaceLg CONSTANT FINAL)
    int spaceLg() const { return 16; }

    Q_PROPERTY(int spaceXl READ spaceXl CONSTANT FINAL)
    int spaceXl() const { return 24; }

    // ── Control width scale ────────────────────────────
    // 设置页右侧控件的统一宽度刻度：同列控件尽量落在同一档，
    // 避免每个页面各写 120/140/160/200/220/260 导致右边界参差。
    Q_PROPERTY(int controlWidthXs READ controlWidthXs CONSTANT FINAL)
    int controlWidthXs() const { return 120; }

    Q_PROPERTY(int controlWidthSm READ controlWidthSm CONSTANT FINAL)
    int controlWidthSm() const { return 160; }

    Q_PROPERTY(int controlWidthMd READ controlWidthMd CONSTANT FINAL)
    int controlWidthMd() const { return 200; }

    Q_PROPERTY(int controlWidthLg READ controlWidthLg CONSTANT FINAL)
    int controlWidthLg() const { return 220; }

    Q_PROPERTY(int controlWidthXl READ controlWidthXl CONSTANT FINAL)
    int controlWidthXl() const { return 260; }

    // ── Radius scale (docs/19 §3.3) ────────────────────
    Q_PROPERTY(int radiusSm READ radiusSm CONSTANT FINAL)
    int radiusSm() const { return 4; }

    // 表单输入框与按钮共用的 6px 圆角；徽标/胶囊用 9px（高度 18 的一半）。
    Q_PROPERTY(int radiusField READ radiusField CONSTANT FINAL)
    int radiusField() const { return 6; }

    Q_PROPERTY(int radiusBadge READ radiusBadge CONSTANT FINAL)
    int radiusBadge() const { return 9; }

    Q_PROPERTY(int radiusMd READ radiusMd CONSTANT FINAL)
    int radiusMd() const { return 8; }

    Q_PROPERTY(int radiusLg READ radiusLg CONSTANT FINAL)
    int radiusLg() const { return 12; }

    Q_PROPERTY(int radiusBubble READ radiusBubble CONSTANT FINAL)
    int radiusBubble() const { return 14; }

    Q_PROPERTY(int radiusBubbleTail READ radiusBubbleTail CONSTANT FINAL)
    int radiusBubbleTail() const { return 4; }

    // ── Motion durations & layout scalars (docs/19 §3.4) ──
    // 时长以毫秒暴露；缓动曲线在 QML 侧用 Easing 枚举内联。
    // docs/31 S4.7 O10：「减少动效」开启时三个令牌归零——时长为 0 的
    // Behavior/Transition 直接落定，布局不抖。不能改成 CONSTANT：令牌
    // 必须随偏好即时重算，32 处 Theme.motion* 绑定靠 NOTIFY 刷新。
    Q_PROPERTY(int motionFast READ motionFast NOTIFY motionChanged FINAL)
    int motionFast() const { return m_manager->reduceMotion() ? 0 : 100; }

    Q_PROPERTY(int motionBase READ motionBase NOTIFY motionChanged FINAL)
    int motionBase() const { return m_manager->reduceMotion() ? 0 : 180; }

    Q_PROPERTY(int motionSlow READ motionSlow NOTIFY motionChanged FINAL)
    int motionSlow() const { return m_manager->reduceMotion() ? 0 : 260; }

    // 对话正文最大宽度：超宽屏下内容居中，避免行长过长（docs/19 §4.5）。
    Q_PROPERTY(int maxContentWidth READ maxContentWidth CONSTANT FINAL)
    int maxContentWidth() const { return 820; }

    // 气泡最大宽度 = 内容区可用宽 × 该比例。
    Q_PROPERTY(double bubbleMaxRatio READ bubbleMaxRatio CONSTANT FINAL)
    double bubbleMaxRatio() const { return 0.72; }

    Q_PROPERTY(int avatarSize READ avatarSize CONSTANT FINAL)
    int avatarSize() const { return 28; }

    Q_PROPERTY(QString fontMono READ fontMono NOTIFY themeChanged FINAL)
    QString fontMono() const { return m_manager->fontMono(); }

    Q_PROPERTY(QString fontSans READ fontSans NOTIFY themeChanged FINAL)
    QString fontSans() const { return m_manager->fontSans(); }

    Q_PROPERTY(int fontSizeH1 READ fontSizeH1 NOTIFY themeChanged FINAL)
    int fontSizeH1() const { return m_manager->baseFontSize() + 6; }

    Q_PROPERTY(int fontSizeH2 READ fontSizeH2 NOTIFY themeChanged FINAL)
    int fontSizeH2() const { return m_manager->baseFontSize() + 2; }

    Q_PROPERTY(int fontSizeBody READ fontSizeBody NOTIFY themeChanged FINAL)
    int fontSizeBody() const { return m_manager->baseFontSize(); }

    // 说明文字与微文案也跟随「基础字号」缩放，但各有下限：基础字号可以调到
    // 10，直接减 2 会掉到 8px。设置页统一用这几档，不要再写死 pixelSize。
    Q_PROPERTY(int fontSizeCaption READ fontSizeCaption NOTIFY themeChanged FINAL)
    int fontSizeCaption() const { return qMax(11, m_manager->baseFontSize() - 2); }

    Q_PROPERTY(int fontSizeMicro READ fontSizeMicro NOTIFY themeChanged FINAL)
    int fontSizeMicro() const { return qMax(10, m_manager->baseFontSize() - 4); }

    // 密集面板（Git 差异 / 日志 / 文件树 / 分支图）的 9px 级小字。这些区域
    // 此前在 QML 里写死 8/9，导致「基础字号」设置对它们完全无效。
    Q_PROPERTY(int fontSizeNano READ fontSizeNano NOTIFY themeChanged FINAL)
    int fontSizeNano() const { return qMax(9, m_manager->baseFontSize() - 5); }

signals:
    void themeChanged();
    void motionChanged();

private:
    ThemeManager* m_manager;

    inline static ThemeProvider* s_instance = nullptr;
};

} // namespace NexusAI
