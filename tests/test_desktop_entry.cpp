// docs/49 阶段 2: DesktopEntry — pure parsing for the Linux emergency
// "open with" chain (xdg-mime → .desktop Exec → direct launch).
#include "core/utils/DesktopEntry.h"

#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace NexusAI::DesktopEntry;

class TestDesktopEntry : public QObject {
    Q_OBJECT

private slots:
    void execExpandsFileFieldCodes() {
        const QStringList gedit = desktopExecArguments(
            QStringLiteral("gedit %F"), QStringLiteral("/a/b.md"));
        QCOMPARE(gedit, (QStringList{QStringLiteral("gedit"),
                                     QStringLiteral("/a/b.md")}));

        const QStringList code = desktopExecArguments(
            QStringLiteral("code -n %u"), QStringLiteral("/x.md"));
        QCOMPARE(code, (QStringList{QStringLiteral("code"),
                                    QStringLiteral("-n"), QStringLiteral("/x.md")}));

        // Embedded code builds a URI out of the path.
        const QStringList uri = desktopExecArguments(
            QStringLiteral("xdg-open file://%u"), QStringLiteral("/x.md"));
        QCOMPARE(uri, (QStringList{QStringLiteral("xdg-open"),
                                   QStringLiteral("file:///x.md")}));
    }

    void execAppendsPathWhenNoFieldCode() {
        // xdg-open practice: a handler without file field codes gets the path
        // appended as the last argument.
        const QStringList kate = desktopExecArguments(
            QStringLiteral("kate"), QStringLiteral("/x.md"));
        QCOMPARE(kate, (QStringList{QStringLiteral("kate"),
                                    QStringLiteral("/x.md")}));

        const QStringList empty = desktopExecArguments(QString(),
                                                       QStringLiteral("/x.md"));
        QCOMPARE(empty, QStringList{QStringLiteral("/x.md")});
    }

    void execRespectsQuotesAndEscapes() {
        // Quoted program path with a space stays one argument.
        const QStringList quoted = desktopExecArguments(
            QStringLiteral("\"/opt/My App/bin/app\" %f"), QStringLiteral("/x"));
        QCOMPARE(quoted, (QStringList{QStringLiteral("/opt/My App/bin/app"),
                                      QStringLiteral("/x")}));
        // Single-quoted segment keeps its inner whitespace.
        const QStringList single = desktopExecArguments(
            QStringLiteral("'my app' %f"), QStringLiteral("/x"));
        QCOMPARE(single, (QStringList{QStringLiteral("my app"),
                                      QStringLiteral("/x")}));
        // Backslash escapes the field-code percent outside quotes.
        const QStringList pct = desktopExecArguments(
            QStringLiteral("app --pct\\% %f"), QStringLiteral("/x"));
        QCOMPARE(pct, (QStringList{QStringLiteral("app"),
                                   QStringLiteral("--pct%"),
                                   QStringLiteral("/x")}));
        // %% collapses to one literal percent.
        const QStringList dbl = desktopExecArguments(
            QStringLiteral("app 100%% %f"), QStringLiteral("/x"));
        QCOMPARE(dbl, (QStringList{QStringLiteral("app"),
                                   QStringLiteral("100%"), QStringLiteral("/x")}));
    }

    void execDropsAuxiliaryAndDeprecatedCodes() {
        const QStringList aux = desktopExecArguments(
            QStringLiteral("app %i %c %k %f"), QStringLiteral("/x"));
        QCOMPARE(aux, (QStringList{QStringLiteral("app"), QStringLiteral("/x")}));

        const QStringList deprecated = desktopExecArguments(
            QStringLiteral("app %d %f"), QStringLiteral("/x"));
        QCOMPARE(deprecated, (QStringList{QStringLiteral("app"),
                                          QStringLiteral("/x")}));
    }

    void locateSearchesDataDirsInOrder() {
        QTemporaryDir a, b;
        QVERIFY(QDir().mkpath(a.filePath(QStringLiteral("applications"))));
        QVERIFY(QDir().mkpath(b.filePath(QStringLiteral("applications"))));
        const QString shared = QStringLiteral("org.example.app.desktop");
        const QString aFile = a.filePath(QStringLiteral("applications/") + shared);
        QFile a1(aFile);
        QVERIFY(a1.open(QIODevice::WriteOnly));   // content irrelevant
        a1.close();
        const QString bFile = b.filePath(QStringLiteral("applications/") + shared);
        QFile b1(bFile);
        QVERIFY(b1.open(QIODevice::WriteOnly));
        b1.close();
        const QString onlyB = QStringLiteral("only-b.desktop");
        const QString bOnlyFile = b.filePath(QStringLiteral("applications/") + onlyB);
        QFile b2(bOnlyFile);
        QVERIFY(b2.open(QIODevice::WriteOnly));
        b2.close();

        const QStringList dirs{a.path(), b.path()};
        // First matching data dir wins.
        QCOMPARE(locateDesktopFile(shared, dirs), aFile);
        // Falls through to later dirs.
        QCOMPARE(locateDesktopFile(onlyB, dirs), bOnlyFile);
        // Absolute ids pass through as-is.
        QCOMPARE(locateDesktopFile(bOnlyFile, dirs), bOnlyFile);
        // Unknown or empty id → empty.
        QVERIFY(locateDesktopFile(QStringLiteral("missing.desktop"), dirs).isEmpty());
        QVERIFY(locateDesktopFile(QString(), dirs).isEmpty());
    }

    void desktopDataDirsHonoursXdgEnvironment() {
        QTemporaryDir home, sys1, sys2;
        const QString oldDataHome = qEnvironmentVariable("XDG_DATA_HOME");
        const QString oldDataDirs = qEnvironmentVariable("XDG_DATA_DIRS");
        qputenv("XDG_DATA_HOME", home.path().toUtf8());
        qputenv("XDG_DATA_DIRS",
                (sys1.path() + QLatin1Char(':') + sys2.path()).toUtf8());

        const QStringList dirs = desktopDataDirs();
        QCOMPARE(dirs.size(), 3);
        QCOMPARE(dirs.at(0), home.path());
        QCOMPARE(dirs.at(1), sys1.path());
        QCOMPARE(dirs.at(2), sys2.path());

        // Restore so the change cannot leak into other tests in this process.
        if (oldDataHome.isEmpty())
            qunsetenv("XDG_DATA_HOME");
        else
            qputenv("XDG_DATA_HOME", oldDataHome.toUtf8());
        if (oldDataDirs.isEmpty())
            qunsetenv("XDG_DATA_DIRS");
        else
            qputenv("XDG_DATA_DIRS", oldDataDirs.toUtf8());
    }

    void readParsesDesktopEntryKeys() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("demo.desktop"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("[Desktop Entry]\n"
                "Type=Application\n"
                "Name=Demo\n"
                "Name[zh_CN]=演示\n"
                "Exec=demo --flag %f\n"
                "TryExec=demo-bin\n"
                "Hidden=false\n"
                "NoDisplay=true\n");
        f.close();

        const DesktopEntryInfo entry = readDesktopEntry(path);
        QVERIFY(entry.valid);
        QCOMPARE(entry.exec, QStringLiteral("demo --flag %f"));
        QCOMPARE(entry.tryExec, QStringLiteral("demo-bin"));
        QVERIFY(!entry.hidden);
        QVERIFY(entry.noDisplay);
    }

    void readRejectsEntriesWithoutExec() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("noexec.desktop"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("[Desktop Entry]\nType=Link\nName=Nope\n");
        f.close();

        const DesktopEntryInfo entry = readDesktopEntry(path);
        QVERIFY(!entry.valid);
    }

    void tryExecResolution() {
        QVERIFY(tryExecAvailable(QString()));
        // /bin/sh is guaranteed by POSIX/FHS.
        QVERIFY(tryExecAvailable(QStringLiteral("/bin/sh")));
        QVERIFY(tryExecAvailable(QStringLiteral("sh")));
        QVERIFY(!tryExecAvailable(QStringLiteral("nexusai-definitely-not-a-binary")));
        QVERIFY(!tryExecAvailable(QStringLiteral("/nonexistent/nexusai-binary")));
    }
};

QTEST_GUILESS_MAIN(TestDesktopEntry)
#include "test_desktop_entry.moc"
