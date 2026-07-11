#pragma once

#include <QObject>
#include <QColor>

#include "ThemeManager.h"

namespace NexusAI {

// C++ Theme provider - exposes all theme colors as Q_PROPERTY
// Registered as "Theme" context property, accessible everywhere in QML
class ThemeProvider : public QObject {
    Q_OBJECT
public:
    explicit ThemeProvider(ThemeManager* manager, QObject* parent = nullptr)
        : QObject(parent), m_manager(manager) {
        connect(m_manager, &ThemeManager::themeChanged, this, [this]() {
            emit themeChanged();
        });
    }

    Q_PROPERTY(bool isDark READ isDark NOTIFY themeChanged FINAL)
    bool isDark() const { return m_manager->isDark(); }

    // ── Background Colors ──────────────────────────────
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

    // ── Borders ────────────────────────────────────────
    Q_PROPERTY(QColor borderSubtle READ borderSubtle NOTIFY themeChanged FINAL)
    QColor borderSubtle() const {
        return m_manager->isDark() ? "#2A2A3A" : "#E5E8EB";
    }

    // ── Text Colors ────────────────────────────────────
    Q_PROPERTY(QColor textPrimary READ textPrimary NOTIFY themeChanged FINAL)
    QColor textPrimary() const {
        return m_manager->isDark() ? "#E4E4ED" : "#1F2329";
    }

    Q_PROPERTY(QColor textSecondary READ textSecondary NOTIFY themeChanged FINAL)
    QColor textSecondary() const {
        return m_manager->isDark() ? "#8A8A9F" : "#8F959E";
    }

    // ── Brand & Status Colors ──────────────────────────
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

    // ── Elevation / Shadow ─────────────────────────────
    Q_PROPERTY(int elevationRadius READ elevationRadius NOTIFY themeChanged FINAL)
    int elevationRadius() const {
        return m_manager->isDark() ? 0 : 4;
    }

    Q_PROPERTY(QColor elevationColor READ elevationColor NOTIFY themeChanged FINAL)
    QColor elevationColor() const {
        return m_manager->isDark() ? QColor("transparent") : QColor("#10000000");
    }

    // ── Typography ─────────────────────────────────────
    Q_PROPERTY(QString fontMono READ fontMono CONSTANT FINAL)
    QString fontMono() const { return QStringLiteral("JetBrains Mono"); }

    Q_PROPERTY(QString fontSans READ fontSans CONSTANT FINAL)
    QString fontSans() const { return QStringLiteral("Inter"); }

    Q_PROPERTY(int fontSizeH1 READ fontSizeH1 CONSTANT FINAL)
    int fontSizeH1() const { return 20; }

    Q_PROPERTY(int fontSizeH2 READ fontSizeH2 CONSTANT FINAL)
    int fontSizeH2() const { return 16; }

    Q_PROPERTY(int fontSizeBody READ fontSizeBody CONSTANT FINAL)
    int fontSizeBody() const { return 14; }

    Q_PROPERTY(int fontSizeCaption READ fontSizeCaption CONSTANT FINAL)
    int fontSizeCaption() const { return 12; }

signals:
    void themeChanged();

private:
    ThemeManager* m_manager;
};

} // namespace NexusAI
