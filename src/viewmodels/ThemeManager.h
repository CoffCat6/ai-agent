#pragma once

#include <QQmlEngine>
#include <QObject>
#include <QSettings>

namespace NexusAI {

// Theme manager - persists user's dark/light preference
class ThemeManager : public QObject {
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(ThemeManager)
    Q_PROPERTY(bool isDark READ isDark WRITE setIsDark NOTIFY themeChanged FINAL)
public:
    // No default constructor on purpose (see AgentViewModel: the engine
    // would prefer Constructor over the create() factory).
    explicit ThemeManager(QObject* parent);

    // QML singleton plumbing (same pattern as AgentViewModel)
    static ThemeManager* instance();
    static ThemeManager* create(QQmlEngine*, QJSEngine*);

    bool isDark() const { return m_isDark; }
    void setIsDark(bool dark);

signals:
    void themeChanged();

private:
    void loadTheme();
    void saveTheme(bool dark);

    bool m_isDark{true};
    QSettings m_settings;

    static ThemeManager* s_instance;
};

} // namespace NexusAI
