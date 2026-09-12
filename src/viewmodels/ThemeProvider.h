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
        return m_manager->isDark() ? "#13131A" : "#FFFFFF";
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
