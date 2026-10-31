#pragma once

#include <QQmlEngine>
#include <QObject>


namespace NexusAI {

class ChangeBus;

class ThemeManager : public QObject {
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(ThemeManager)
    Q_PROPERTY(bool isDark READ isDark WRITE setIsDark NOTIFY themeChanged FINAL)
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged FINAL)
    Q_PROPERTY(QString fontSans READ fontSans WRITE setFontSans NOTIFY fontChanged FINAL)
    Q_PROPERTY(QString fontMono READ fontMono WRITE setFontMono NOTIFY fontChanged FINAL)
    Q_PROPERTY(int baseFontSize READ baseFontSize WRITE setBaseFontSize NOTIFY fontChanged FINAL)
    // docs/31 S4.7 O10：无障碍「减少动效」。只经设置页写偏好键（EditBuffer →
    // PreferenceStore → ChangeBus），这里不提供 WRITE——与 themeMode 的
    // Ctrl+D 快捷切换不同，动效没有命令式入口。
    Q_PROPERTY(bool reduceMotion READ reduceMotion NOTIFY motionChanged FINAL)
public:
    explicit ThemeManager(QObject* parent);

    static ThemeManager* instance();
    static ThemeManager* create(QQmlEngine*, QJSEngine*);

    bool isDark() const { return m_isDark; }
    void setIsDark(bool dark);

    QString themeMode() const { return m_themeMode; }
    void setThemeMode(const QString& mode);

    QString fontSans() const { return m_fontSans; }
    void setFontSans(const QString& family);

    QString fontMono() const { return m_fontMono; }
    void setFontMono(const QString& family);

    int baseFontSize() const { return m_baseFontSize; }
    void setBaseFontSize(int size);

    bool reduceMotion() const { return m_reduceMotion; }

signals:
    void themeChanged();
    void themeModeChanged();
    void fontChanged();
    void motionChanged();

private:
    void loadTheme();

    void loadFontPrefs();
    void loadMotionPrefs();
    void registerPreferences();
    void subscribeChanges();
    bool resolveSystemDark() const;
    void applySystemColorScheme();

    bool m_isDark{true};
    QString m_themeMode;
    QString m_fontSans;
    QString m_fontMono;
    int m_baseFontSize{};
    bool m_reduceMotion{false};

    ChangeBus* m_changeBus{nullptr};

    static ThemeManager* s_instance;
};

} // namespace NexusAI
