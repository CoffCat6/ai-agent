#pragma once

#include <QColor>
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
        return m_manager->isDark() ? "#8A8A9F" : "#8F959E";
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

    Q_PROPERTY(int elevationRadius READ elevationRadius NOTIFY themeChanged FINAL)
    int elevationRadius() const {
        return m_manager->isDark() ? 0 : 4;
    }

    Q_PROPERTY(QColor elevationColor READ elevationColor NOTIFY themeChanged FINAL)
    QColor elevationColor() const {
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

    // ── Radius scale (docs/19 §3.3) ────────────────────
    Q_PROPERTY(int radiusSm READ radiusSm CONSTANT FINAL)
    int radiusSm() const { return 4; }

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
    Q_PROPERTY(int motionFast READ motionFast CONSTANT FINAL)
    int motionFast() const { return 100; }

    Q_PROPERTY(int motionBase READ motionBase CONSTANT FINAL)
    int motionBase() const { return 180; }

    Q_PROPERTY(int motionSlow READ motionSlow CONSTANT FINAL)
    int motionSlow() const { return 260; }

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

    Q_PROPERTY(int fontSizeCaption READ fontSizeCaption NOTIFY themeChanged FINAL)
    int fontSizeCaption() const { return m_manager->baseFontSize() - 2; }

signals:
    void themeChanged();

private:
    ThemeManager* m_manager;

    inline static ThemeProvider* s_instance = nullptr;
};

} // namespace NexusAI
