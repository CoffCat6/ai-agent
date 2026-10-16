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

signals:
    void themeChanged();
    void themeModeChanged();
    void fontChanged();

private:
    void loadTheme();

    void loadFontPrefs();
    void registerPreferences();
    void subscribeChanges();
    bool resolveSystemDark() const;
    void applySystemColorScheme();

    bool m_isDark{true};
    QString m_themeMode;
    QString m_fontSans;
    QString m_fontMono;
    int m_baseFontSize{};

    ChangeBus* m_changeBus{nullptr};

    static ThemeManager* s_instance;
};

} // namespace NexusAI
