#pragma once

#include <QObject>
#include <QSettings>

namespace NexusAI {

// Theme manager - persists user's dark/light preference
class ThemeManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isDark READ isDark WRITE setIsDark NOTIFY themeChanged FINAL)
public:
    explicit ThemeManager(QObject* parent = nullptr);

    bool isDark() const { return m_isDark; }
    void setIsDark(bool dark);

signals:
    void themeChanged();

private:
    void loadTheme();
    void saveTheme(bool dark);

    bool m_isDark{true};
    QSettings m_settings;
};

} // namespace NexusAI
