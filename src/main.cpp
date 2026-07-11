#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "viewmodels/ThemeManager.h"
#include "viewmodels/ThemeProvider.h"
#include "viewmodels/AgentViewModel.h"

using namespace NexusAI;

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("NexusAI");
    app.setApplicationName("NexusAI");
    app.setApplicationVersion("0.1.0");

    // ── ViewModels ───────────────────────────────────────
    auto *themeManager = new ThemeManager(&app);
    auto *themeProvider = new ThemeProvider(themeManager, &app);
    auto *agentVM = new AgentViewModel(&app);

    // ── QML Engine ───────────────────────────────────────
    QQmlApplicationEngine engine;

    // Register context properties for QML
    engine.rootContext()->setContextProperty("Theme", themeProvider);
    engine.rootContext()->setContextProperty("ThemeManager", themeManager);
    engine.rootContext()->setContextProperty("AgentVM", agentVM);

    // Load main QML
    const QUrl url(QStringLiteral("qrc:/qt/qml/NexusAI/src/ui/main.qml"));

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.load(url);

    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
