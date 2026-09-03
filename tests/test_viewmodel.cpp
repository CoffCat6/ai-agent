// AgentViewModel UI-layout property coverage: sidebar / context panel width
// clamping, debounced QSettings persistence and cross-instance restore.
#include "viewmodels/AgentViewModel.h"

#include <QtTest>
#include <QSignalSpy>
#include <QSettings>

using namespace NexusAI;

namespace {

// Restores the user's real ui/sidebarWidth & ui/panelWidth afterwards;
// the test shares the app's QSettings location.
class WidthSettingsGuard {
public:
    WidthSettingsGuard() {
        QSettings s("NexusAI", "NexusAI");
        sidebar = s.value("ui/sidebarWidth");
        panel = s.value("ui/panelWidth");
    }
    ~WidthSettingsGuard() {
        QSettings s("NexusAI", "NexusAI");
        if (sidebar.isValid()) s.setValue("ui/sidebarWidth", sidebar);
        else s.remove("ui/sidebarWidth");
        if (panel.isValid()) s.setValue("ui/panelWidth", panel);
        else s.remove("ui/panelWidth");
    }
    QVariant sidebar;
    QVariant panel;
};

} // namespace

class TestAgentViewModel : public QObject {
    Q_OBJECT

private slots:
    // Named AgentState constants exposed to QML must mirror the core enum
    // (QML compares AgentVM.state against them instead of magic numbers).
    void namedStateConstants() {
        AgentViewModel vm;
        QCOMPARE(vm.stateIdle(), 0);
        QCOMPARE(vm.stateThinking(), 1);
        QCOMPARE(vm.stateAwaitingToolConfirm(), 2);
        QCOMPARE(vm.stateExecutingTool(), 3);
        QCOMPARE(vm.stateSummarizing(), 4);
        QCOMPARE(vm.stateCompleted(), 5);
        QCOMPARE(vm.stateError(), 6);

        // Fresh VM: Idle is not busy; the busy window covers states 1..4
        QCOMPARE(vm.stateAsInt(), vm.stateIdle());
        QVERIFY(!vm.stateBusy());
    }

    void widthDefaultsClampingAndSignals() {
        WidthSettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("ui/sidebarWidth");
            s.remove("ui/panelWidth");
        }

        AgentViewModel vm;
        QCOMPARE(vm.sidebarWidth(), 260);
        QCOMPARE(vm.panelWidth(), 300);

        QSignalSpy sidebarSpy(&vm, &AgentViewModel::sidebarWidthChanged);
        QSignalSpy panelSpy(&vm, &AgentViewModel::panelWidthChanged);

        vm.setSidebarWidth(340);
        QCOMPARE(vm.sidebarWidth(), 340);
        QCOMPARE(sidebarSpy.count(), 1);

        // No-op write must not emit
        vm.setSidebarWidth(340);
        QCOMPARE(sidebarSpy.count(), 1);

        // Hard clamps applied in the setter (the QML drag handler applies
        // the finer window-aware limit on top of these)
        vm.setSidebarWidth(0);
        QCOMPARE(vm.sidebarWidth(), 180);
        vm.setSidebarWidth(9999);
        QCOMPARE(vm.sidebarWidth(), 560);
        vm.setPanelWidth(0);
        QCOMPARE(vm.panelWidth(), 220);
        vm.setPanelWidth(9999);
        QCOMPARE(vm.panelWidth(), 640);
        QVERIFY(sidebarSpy.count() >= 3);
        QVERIFY(panelSpy.count() >= 2);
    }

    void widthPersistenceDebouncedAndRestored() {
        WidthSettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("ui/sidebarWidth");
            s.remove("ui/panelWidth");
        }

        AgentViewModel vm;
        vm.setSidebarWidth(420);
        vm.setPanelWidth(480);

        // Writes are coalesced on a 400 ms single-shot timer; let it fire
        QTest::qWait(600);

        // A fresh instance restores both widths from QSettings
        AgentViewModel restored;
        QCOMPARE(restored.sidebarWidth(), 420);
        QCOMPARE(restored.panelWidth(), 480);
    }
};

QTEST_GUILESS_MAIN(TestAgentViewModel)
#include "test_viewmodel.moc"
