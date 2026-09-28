// SettingsService multi-model profiles, defaults completion, model switching,
// addition, removal and persistence coverage.
#include "viewmodels/SettingsService.h"
#include "viewmodels/SettingsSchema.h"
#include "viewmodels/ThemeManager.h"
#include "core/tools/ToolRegistry.h"

#include "core/utils/Logger.h"

#include <QtTest>
#include <QGuiApplication>
#include <QStyleHints>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QSettings>

#include <memory>

using namespace NexusAI;

namespace {

class SettingsGuard {
public:
    SettingsGuard() {
        QSettings s("NexusAI", "NexusAI");
        rawProfiles = s.value("modelProfiles");
        activeId = s.value("activeProfileId");
        // Also guard the keys suites actually write; without this the tests
        // leaked settings into the user's real app (e.g. logging/level,
        // embedding/model, the security/allowFullDiskRead sandbox switch).
        loggingLevel = s.value("logging/level");
        embeddingModel = s.value("embedding/model");
        embeddingBaseUrl = s.value("embedding/baseUrl");
        allowFullDiskRead = s.value("security/allowFullDiskRead");
        localOnlyMode = s.value("security/localOnlyMode");
        allowLocalNetworkFetch = s.value("security/allowLocalNetworkFetch");
        systemPromptOverride = s.value("agent/systemPromptOverride");
        toolsDisabled = s.value("tools/disabled");
        webSearchProvider = s.value("webSearch/provider");
        webSearchApiKey = s.value("webSearch/apiKey");
        schemaVersion = s.value("schemaVersion");
    }
    ~SettingsGuard() {
        QSettings s("NexusAI", "NexusAI");
        if (rawProfiles.isValid()) s.setValue("modelProfiles", rawProfiles);
        else s.remove("modelProfiles");
        if (activeId.isValid()) s.setValue("activeProfileId", activeId);
        else s.remove("activeProfileId");
        if (loggingLevel.isValid()) s.setValue("logging/level", loggingLevel);
        else s.remove("logging/level");
        if (embeddingModel.isValid()) s.setValue("embedding/model", embeddingModel);
        else s.remove("embedding/model");
        if (embeddingBaseUrl.isValid()) s.setValue("embedding/baseUrl", embeddingBaseUrl);
        else s.remove("embedding/baseUrl");
        if (allowFullDiskRead.isValid())
            s.setValue("security/allowFullDiskRead", allowFullDiskRead);
        else s.remove("security/allowFullDiskRead");
        if (localOnlyMode.isValid())
            s.setValue("security/localOnlyMode", localOnlyMode);
        else s.remove("security/localOnlyMode");
        if (allowLocalNetworkFetch.isValid())
            s.setValue("security/allowLocalNetworkFetch", allowLocalNetworkFetch);
        else s.remove("security/allowLocalNetworkFetch");
        if (systemPromptOverride.isValid())
            s.setValue("agent/systemPromptOverride", systemPromptOverride);
        else s.remove("agent/systemPromptOverride");
        if (toolsDisabled.isValid())
            s.setValue("tools/disabled", toolsDisabled);
        else s.remove("tools/disabled");
        if (webSearchProvider.isValid())
            s.setValue("webSearch/provider", webSearchProvider);
        else s.remove("webSearch/provider");
        if (webSearchApiKey.isValid())
            s.setValue("webSearch/apiKey", webSearchApiKey);
        else s.remove("webSearch/apiKey");
        if (schemaVersion.isValid())
            s.setValue("schemaVersion", schemaVersion);
        else s.remove("schemaVersion");
    }
    QVariant rawProfiles;
    QVariant activeId;
    QVariant loggingLevel;
    QVariant embeddingModel;
    QVariant embeddingBaseUrl;
    QVariant allowFullDiskRead;
    QVariant localOnlyMode;
    QVariant allowLocalNetworkFetch;
    QVariant systemPromptOverride;
    QVariant toolsDisabled;
    QVariant webSearchProvider;
    QVariant webSearchApiKey;
    QVariant schemaVersion;
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

    // Batch 8/日志开发: log level property (default, persistence, signal)
    void logLevelPersistence() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("logging/level");
        }

        {
            SettingsService service(nullptr);
            QCOMPARE(service.logLevel(), QStringLiteral("info"));

            QSignalSpy spy(&service, &SettingsService::logLevelChanged);
            service.setLogLevel(QStringLiteral("DEBUG"));
            QCOMPARE(spy.count(), 1);
            QCOMPARE(service.logLevel(), QStringLiteral("debug"));   // normalized
            service.setLogLevel(QStringLiteral(""));                  // blank → ignored
            QCOMPARE(service.logLevel(), QStringLiteral("debug"));
            // Instant effect on the logger itself (doc 6 §3.5)
            QCOMPARE(Logger::minLevel(), QtDebugMsg);
        }
        {
            SettingsService service(nullptr);
            QCOMPARE(service.logLevel(), QStringLiteral("debug"));
        }
        Logger::setMinLevel(QtInfoMsg);   // restore suite default
    }

    // Batch 22 (F8): user-editable system prompt; empty (after trim) clears
    // the override instead of being ignored.
    void systemPromptOverridePersistence() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("agent/systemPromptOverride");
        }

        {
            SettingsService service(nullptr);
            QCOMPARE(service.systemPromptOverride(), QString());

            QSignalSpy spy(&service, &SettingsService::systemPromptOverrideChanged);
            service.setSystemPromptOverride(QStringLiteral("  自定义提示词  "));
            QCOMPARE(spy.count(), 1);
            QCOMPARE(service.systemPromptOverride(), QStringLiteral("自定义提示词"));  // trimmed

            service.setSystemPromptOverride(QStringLiteral("自定义提示词"));          // no-op write
            QCOMPARE(spy.count(), 1);

            service.setSystemPromptOverride(QStringLiteral("   "));                   // blank clears
            QCOMPARE(spy.count(), 2);
            QCOMPARE(service.systemPromptOverride(), QString());
        }
        {
            SettingsService service(nullptr);
            service.setSystemPromptOverride(QStringLiteral("persisted prompt"));
        }
        {
            SettingsService service(nullptr);
            QCOMPARE(service.systemPromptOverride(), QStringLiteral("persisted prompt"));
        }
    }

    // FEAT-801: embedding model + Batch 5 full-disk-read persistence round trip
    // + Batch 14 local-only-mode persistence round trip
    void embeddingModelAndReadPolicyPersistence() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("embedding/model");
            s.remove("embedding/baseUrl");
            s.remove("security/allowFullDiskRead");
            s.remove("security/localOnlyMode");
            s.remove("security/allowLocalNetworkFetch");
        }

        {
            SettingsService service(nullptr);
            QCOMPARE(service.embeddingModel(), QStringLiteral("nomic-embed-text"));
            QCOMPARE(service.embeddingBaseUrl(), QStringLiteral("http://localhost:11434"));
            QVERIFY(!service.allowFullDiskRead());
            QVERIFY(!service.localOnlyMode());
            QVERIFY(!service.allowLocalNetworkFetch());

            QSignalSpy modelSpy(&service, &SettingsService::embeddingModelChanged);
            service.setEmbeddingModel(QStringLiteral("bge-m3"));
            QCOMPARE(modelSpy.count(), 1);
            service.setEmbeddingModel(QStringLiteral("bge-m3"));   // unchanged → silent
            QCOMPARE(modelSpy.count(), 1);
            service.setEmbeddingModel(QStringLiteral("   "));      // blank → ignored
            QCOMPARE(service.embeddingModel(), QStringLiteral("bge-m3"));

            // Batch 25 (G11): embedding endpoint persistence round trip.
            QSignalSpy urlSpy(&service, &SettingsService::embeddingBaseUrlChanged);
            service.setEmbeddingBaseUrl(QStringLiteral(" http://192.168.1.10:11434 "));
            QCOMPARE(urlSpy.count(), 1);
            QCOMPARE(service.embeddingBaseUrl(), QStringLiteral("http://192.168.1.10:11434"));
            service.setEmbeddingBaseUrl(QStringLiteral("http://192.168.1.10:11434"));  // unchanged → silent
            QCOMPARE(urlSpy.count(), 1);
            service.setEmbeddingBaseUrl(QStringLiteral("   "));    // blank → ignored
            QCOMPARE(service.embeddingBaseUrl(), QStringLiteral("http://192.168.1.10:11434"));

            service.setAllowFullDiskRead(true);

            QSignalSpy localOnlySpy(&service, &SettingsService::localOnlyModeChanged);
            service.setLocalOnlyMode(true);
            QCOMPARE(localOnlySpy.count(), 1);
            service.setLocalOnlyMode(true);                        // unchanged → silent
            QCOMPARE(localOnlySpy.count(), 1);

            QSignalSpy fetchSpy(&service, &SettingsService::allowLocalNetworkFetchChanged);
            service.setAllowLocalNetworkFetch(true);
            QCOMPARE(fetchSpy.count(), 1);
            service.setAllowLocalNetworkFetch(true);               // unchanged → silent
            QCOMPARE(fetchSpy.count(), 1);
        }
        {
            SettingsService service(nullptr);
            QCOMPARE(service.embeddingModel(), QStringLiteral("bge-m3"));
            QCOMPARE(service.embeddingBaseUrl(), QStringLiteral("http://192.168.1.10:11434"));
            QVERIFY(service.allowFullDiskRead());
            QVERIFY(service.localOnlyMode());
            QVERIFY(service.allowLocalNetworkFetch());
        }
    }

    // Regression (BUG settings-echo): GeneralPage's embedding-model combo
    // showed the default item again on every page rebuild while the value
    // itself was persisted correctly. Root cause (Qt 6.9 qquickcombobox.cpp):
    // componentComplete() calls updateCurrentText(), which force-writes
    // currentText (first item) over an editable combo's editText and thereby
    // wipes the `editText: Settings.embeddingModel` binding — unless
    // hasCurrentIndex is set. These cases lock the QML-side contract:
    //   - the pre-fix pattern must reproduce the default-echo defect
    //     (if a future Qt stops resetting editText, this workaround pair
    //     in GeneralPage.qml can be dropped together with this test);
    //   - the fixed pattern (currentIndex bound to the persisted index)
    //     must echo the saved value;
    //   - a persisted value outside the model list must still echo through
    //     the Component.onCompleted fallback.
    void embeddingModelComboEcho() {
        // Pre-fix layout: binding only, no currentIndex — Qt resets editText
        // to currentText (the first item) during component completion.
        static const char* kBeforeFix = R"QML(
import QtQuick
import QtQuick.Controls
ComboBox {
    editable: true
    model: ["nomic-embed-text", "bge-m3", "mxbai-embed-large", "all-minilm"]
    property string savedModel: "bge-m3"
    editText: savedModel
}
)QML";
        // Fixed layout: select the persisted index so componentComplete's
        // updateCurrentText() writes the same text (setEditText no-ops).
        static const char* kAfterFix = R"QML(
import QtQuick
import QtQuick.Controls
ComboBox {
    editable: true
    model: ["nomic-embed-text", "bge-m3", "mxbai-embed-large", "all-minilm"]
    property string savedModel: "bge-m3"
    currentIndex: model.indexOf(savedModel)
    editText: savedModel
    Component.onCompleted: if (editText !== savedModel) editText = savedModel
}
)QML";
        // Persisted value outside the model list: indexOf is -1, so the echo
        // relies on the onCompleted fallback after textAt(-1) == "".
        static const char* kCustomValue = R"QML(
import QtQuick
import QtQuick.Controls
ComboBox {
    editable: true
    model: ["nomic-embed-text", "bge-m3", "mxbai-embed-large", "all-minilm"]
    property string savedModel: "custom-embed"
    currentIndex: model.indexOf(savedModel)
    editText: savedModel
    Component.onCompleted: if (editText !== savedModel) editText = savedModel
}
)QML";

        const auto editTextAfterBuild = [](const char* qml) -> QString {
            QQmlEngine engine;
            QQmlComponent component(&engine);
            component.setData(QByteArray(qml), QUrl());
            if (!component.isReady())
                return component.errorString();
            std::unique_ptr<QObject> item(component.create());
            if (!item)
                return component.errorString();
            return item->property("editText").toString();
        };

        // Defect reproduction: the pre-fix combo echoed the default item.
        QCOMPARE(editTextAfterBuild(kBeforeFix), QStringLiteral("nomic-embed-text"));

        // The fixed combo echoes the persisted model.
        QCOMPARE(editTextAfterBuild(kAfterFix), QStringLiteral("bge-m3"));

        // Out-of-list persisted values fall back to the raw saved text.
        QCOMPARE(editTextAfterBuild(kCustomValue), QStringLiteral("custom-embed"));
    }

    void toolRegistryEmitsToolsChanged() {
        SettingsGuard guard;
        auto& reg = ToolRegistry::instance();
        QSignalSpy spy(&reg, &ToolRegistry::toolsChanged);
        QVERIFY(spy.isValid());

        reg.setToolEnabled("read_file", false);
        QCOMPARE(spy.count(), 1);
        reg.setToolEnabled("read_file", true);
        QCOMPARE(spy.count(), 2);

        spy.clear();
        auto taken = reg.takeTool("list_dir");
        QVERIFY(taken != nullptr);
        QCOMPARE(spy.count(), 1);
        reg.registerTool(std::move(taken));
        QCOMPARE(spy.count(), 2);
    }

    void toolDisabledStatePersists() {
        SettingsGuard guard;
        auto& reg = ToolRegistry::instance();

        reg.setToolEnabled("read_file", false);
        {
            QSettings s("NexusAI", "NexusAI");
            QVERIFY(s.value("tools/disabled").toStringList().contains("read_file"));
        }
        reg.setToolEnabled("read_file", true);
        {
            QSettings s("NexusAI", "NexusAI");
            QVERIFY(!s.value("tools/disabled").toStringList().contains("read_file"));
        }
    }

    // Built-in tools must not carry the Dangerous level: destructive work
    // goes through the confirm-gated shell_exec instead (the rm_rf sentinel
    // was removed). Re-adding a Dangerous built-in must be a conscious act.
    void noBuiltinToolIsDangerous() {
        auto& reg = ToolRegistry::instance();
        for (const auto& def : reg.listAll()) {
            QVERIFY2(def.level != ToolLevel::Dangerous, qPrintable(def.name));
        }
    }

    // ── T5 theme mode (auto / dark / light) ─────────────────────────
    void themeModeDefaultsToAuto() {
        SettingsGuard guard;
        clearThemeKeys();
        ThemeManager tm(nullptr);
        QCOMPARE(tm.themeMode(), QStringLiteral("auto"));
    }

    void themeModeExplicitPersistsAndReadsBack() {
        SettingsGuard guard;
        clearThemeKeys();
        {
            ThemeManager tm(nullptr);
            tm.setThemeMode(QStringLiteral("light"));
            QCOMPARE(tm.themeMode(), QStringLiteral("light"));
            QCOMPARE(tm.isDark(), false);
        }
        {
            QSettings s("NexusAI", "NexusAI");
            QCOMPARE(s.value("prefs/v1/ui.themeMode").toString(), QStringLiteral("light"));
        }
        // A fresh instance reads the persisted explicit choice back.
        ThemeManager tm2(nullptr);
        QCOMPARE(tm2.themeMode(), QStringLiteral("light"));
        QCOMPARE(tm2.isDark(), false);
    }

    void themeModeAutoUpgradesOnExplicitToggle() {
        SettingsGuard guard;
        clearThemeKeys();
        ThemeManager tm(nullptr);
        QCOMPARE(tm.themeMode(), QStringLiteral("auto"));
        // REQ-T5-04: an explicit toggle must upgrade "auto" to a concrete mode.
        tm.setIsDark(false);
        QCOMPARE(tm.themeMode(), QStringLiteral("light"));
        {
            QSettings s("NexusAI", "NexusAI");
            QCOMPARE(s.value("prefs/v1/ui.themeMode").toString(), QStringLiteral("light"));
        }
    }

    void themeModeLegacyDarkKeyCompat() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("prefs/v1/ui.themeMode");
            s.setValue("theme/dark", false);   // legacy light preference
        }
        ThemeManager tm(nullptr);
        QCOMPARE(tm.themeMode(), QStringLiteral("light"));
        QCOMPARE(tm.isDark(), false);
    }

    void themeModeAutoResolvesSystemScheme() {
        SettingsGuard guard;
        clearThemeKeys();
        ThemeManager tm(nullptr);
        QCOMPARE(tm.themeMode(), QStringLiteral("auto"));
        // auto follows the OS scheme; Unknown keeps the historical dark look.
        const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
        QCOMPARE(tm.isDark(), scheme != Qt::ColorScheme::Light);
    }

    // L4: setWebSearchProvider rejects unknown provider keys — the setting
    // stays at its default and no signal fires.
    void webSearchProviderRejectsUnknownKey() {
        SettingsGuard guard;
        SettingsService svc(nullptr);
        svc.setWebSearchProvider(QStringLiteral("tavily"));
        QSignalSpy spy(&svc, &SettingsService::webSearchProviderChanged);
        svc.setWebSearchProvider(QStringLiteral("google"));
        QCOMPARE(spy.count(), 0);
        QCOMPARE(svc.webSearchProvider(), QStringLiteral("tavily"));
    }

    // L2: SettingsSchema key registry is populated and contains known keys.
    void schemaHasKeyRegistry() {
        const auto& keys = SettingsSchema::registeredKeys();
        QVERIFY(!keys.isEmpty());
        bool foundProvider = false;
        bool foundApiKey = false;
        for (const auto& k : keys) {
            if (k.key == QStringLiteral("webSearch/provider")) foundProvider = true;
            if (k.key == QStringLiteral("webSearch/apiKey")) {
                foundApiKey = true;
                QVERIFY(k.encrypted);
            }
        }
        QVERIFY(foundProvider);
        QVERIFY(foundApiKey);
    }

    // L2: load() stamps the schema version into QSettings.
    void schemaVersionStampedOnLoad() {
        SettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("schemaVersion");
        }
        SettingsService svc(nullptr);
        QSettings s("NexusAI", "NexusAI");
        QCOMPARE(s.value("schemaVersion").toInt(), SettingsSchema::kSchemaVersion);
    }

private:
    static void clearThemeKeys() {
        QSettings s("NexusAI", "NexusAI");
        s.remove("prefs/v1/ui.themeMode");
        s.remove("theme/dark");
    }
};

// Sampling-param normalization lives in test_sampling_params (separate
// executable): Qt documents qExec as single-call per test application, and
// two calls made CLI options / XML output ambiguous.

int main(int argc, char *argv[]) {
    QCoreApplication::setOrganizationName("NexusAI");
    QCoreApplication::setApplicationName("NexusAI");
    // QML combo-echo tests instantiate QtQuick items: QGuiApplication is
    // required (QCoreApplication would abort on item creation).
    QGuiApplication app(argc, argv);
    TestSettingsService tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_settings.moc"
