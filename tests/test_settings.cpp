// SettingsService multi-model profiles, defaults completion, model switching,
// addition, removal and persistence coverage.
#include "viewmodels/SettingsService.h"

#include <QtTest>
#include <QSignalSpy>
#include <QSettings>

using namespace NexusAI;

namespace {

class SettingsGuard {
public:
    SettingsGuard() {
        QSettings s("NexusAI", "NexusAI");
        rawProfiles = s.value("modelProfiles");
        activeId = s.value("activeProfileId");
    }
    ~SettingsGuard() {
        QSettings s("NexusAI", "NexusAI");
        if (rawProfiles.isValid()) s.setValue("modelProfiles", rawProfiles);
        else s.remove("modelProfiles");
        if (activeId.isValid()) s.setValue("activeProfileId", activeId);
        else s.remove("activeProfileId");
    }
    QVariant rawProfiles;
    QVariant activeId;
};

} // namespace

class TestSettingsService : public QObject {
    Q_OBJECT

private slots:
    void defaultProfilesAndModelList() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("modelProfiles");
            s.remove("activeProfileId");
        }

        SettingsService service(nullptr);
        const QVariantList profiles = service.profiles();
        QVERIFY(!profiles.isEmpty());

        const QVariantMap active = service.activeProfile();
        QVERIFY(!active.isEmpty());
        QVERIFY(active.contains("models"));

        const QStringList models = active.value("models").toStringList();
        QVERIFY(!models.isEmpty());
        QVERIFY(models.contains(active.value("model").toString()));
    }

    void modelSelectionAndSignals() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("modelProfiles");
            s.remove("activeProfileId");
        }

        SettingsService service(nullptr);
        const QVariantMap active = service.activeProfile();
        const QString activeId = active.value("id").toString();
        const QString currentModel = active.value("model").toString();

        QSignalSpy activeProfileSpy(&service, &SettingsService::activeProfileChanged);
        QSignalSpy profilesSpy(&service, &SettingsService::profilesChanged);

        // Select a new model for active profile
        const QString newModel = QStringLiteral("custom-gpt-4o-test");
        service.selectModelForProfile(activeId, newModel);

        QCOMPARE(activeProfileSpy.count(), 1);
        QCOMPARE(profilesSpy.count(), 1);
        QCOMPARE(service.activeProfile().value("model").toString(), newModel);

        const QStringList models = service.activeProfile().value("models").toStringList();
        QVERIFY(models.contains(newModel));

        // Selecting same model should be no-op
        service.selectModelForProfile(activeId, newModel);
        QCOMPARE(activeProfileSpy.count(), 1);
    }

    void addAndRemoveModels() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("modelProfiles");
            s.remove("activeProfileId");
        }

        SettingsService service(nullptr);
        const QString activeId = service.activeProfileId();

        service.addModelToProfile(activeId, QStringLiteral("extra-model-1"));
        QStringList models = service.activeProfile().value("models").toStringList();
        QVERIFY(models.contains(QStringLiteral("extra-model-1")));

        // Remove non-active model
        service.removeModelFromProfile(activeId, QStringLiteral("extra-model-1"));
        models = service.activeProfile().value("models").toStringList();
        QVERIFY(!models.contains(QStringLiteral("extra-model-1")));
    }

    void persistenceAcrossInstances() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("modelProfiles");
            s.remove("activeProfileId");
        }

        {
            SettingsService s1(nullptr);
            const QString activeId = s1.activeProfileId();
            s1.addModelToProfile(activeId, QStringLiteral("persisted-model-x"));
            s1.selectModelForProfile(activeId, QStringLiteral("persisted-model-x"));
        }

        SettingsService s2(nullptr);
        QCOMPARE(s2.activeProfile().value("model").toString(), QStringLiteral("persisted-model-x"));
        QVERIFY(s2.activeProfile().value("models").toStringList().contains(QStringLiteral("persisted-model-x")));
    }

    // FEAT-801: embedding model + Batch 5 full-disk-read persistence round trip
    void embeddingModelAndReadPolicyPersistence() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("embedding/model");
            s.remove("security/allowFullDiskRead");
        }

        {
            SettingsService service(nullptr);
            QCOMPARE(service.embeddingModel(), QStringLiteral("nomic-embed-text"));
            QVERIFY(!service.allowFullDiskRead());

            QSignalSpy modelSpy(&service, &SettingsService::embeddingModelChanged);
            service.setEmbeddingModel(QStringLiteral("bge-m3"));
            QCOMPARE(modelSpy.count(), 1);
            service.setEmbeddingModel(QStringLiteral("bge-m3"));   // unchanged → silent
            QCOMPARE(modelSpy.count(), 1);
            service.setEmbeddingModel(QStringLiteral("   "));      // blank → ignored
            QCOMPARE(service.embeddingModel(), QStringLiteral("bge-m3"));

            service.setAllowFullDiskRead(true);
        }
        {
            SettingsService service(nullptr);
            QCOMPARE(service.embeddingModel(), QStringLiteral("bge-m3"));
            QVERIFY(service.allowFullDiskRead());
        }
    }
};

int main(int argc, char *argv[]) {
    QCoreApplication::setOrganizationName("NexusAI");
    QCoreApplication::setApplicationName("NexusAI");
    TestSettingsService tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_settings.moc"
