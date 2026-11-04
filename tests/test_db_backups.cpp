// docs/31 S1（O3）：备份目录列表与删除路径校验的回归。
//
// 设置页不再让用户手工翻文件夹：列表由 SqliteMemory::listBackups() 直读
// 磁盘（最新在前），删除则由 AgentViewModel::deleteDatabaseBackup() 做
// 白名单校验——QML 属性不是可信输入，只允许删除备份目录自身的直接子文件。
//
// 红线 7：AppData 指到 QStandardPaths 测试目录，QSettings 指到临时目录。
#include <QtTest>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>

#include "core/memory/SqliteMemory.h"
#include "viewmodels/AgentViewModel.h"
#include "TestSettingsIsolation.h"

// 测试进程的 QSettings 重定向到临时 INI（见 TestSettingsIsolation.h），
// 防止测试改写用户真实设置。
namespace {
const NexusAI::TestSupport::SettingsIsolation kSettingsIsolation;
} // namespace

using namespace NexusAI;

namespace {

bool writeFile(const QString& path, const QByteArray& data, int secondsAgo = 0) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    if (file.write(data) != data.size()) return false;
    file.close();
    if (secondsAgo > 0) {
        const QDateTime stamp = QDateTime::currentDateTime().addSecs(-secondsAgo);
        file.open(QIODevice::ReadWrite);
        const bool ok = file.setFileTime(stamp, QFileDevice::FileModificationTime);
        file.close();
        return ok;
    }
    return true;
}

} // namespace

class TestDbBackups : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(m_settingsDir.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_settingsDir.path());
        QStandardPaths::setTestModeEnabled(true);
        // 每次运行一个独立 AppData 目录，别的测试/真实应用留下的快照
        // 不会混进列表断言。
        QCoreApplication::setOrganizationName(QStringLiteral("NexusAIDbBackupTests"));
        QCoreApplication::setApplicationName(
            QStringLiteral("NexusAIDbBackupTests_%1")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));

        m_backupsDir = SqliteMemory::defaultBackupsDir();
        QVERIFY(QDir().mkpath(m_backupsDir));
    }

    // 列表：auto/manual 都收、其他文件忽略、最新在前、大小与来源正确。
    void listBackupsNewestFirstAndClassified() {
        QVERIFY(writeFile(m_backupsDir + QStringLiteral("/auto_20250101_000000.db"),
                          QByteArray("auto-old"), 120));
        QVERIFY(writeFile(m_backupsDir + QStringLiteral("/manual_20250102_000000.db"),
                          QByteArray("manual-newer"), 60));
        QVERIFY(writeFile(m_backupsDir + QStringLiteral("/notes.txt"),
                          QByteArray("ignore me")));

        const QVariantList backups = SqliteMemory::listBackups();
        QCOMPARE(backups.size(), 2);

        const QVariantMap newest = backups.at(0).toMap();
        QCOMPARE(newest.value(QStringLiteral("fileName")).toString(),
                 QStringLiteral("manual_20250102_000000.db"));
        QCOMPARE(newest.value(QStringLiteral("isAuto")).toBool(), false);
        QCOMPARE(newest.value(QStringLiteral("sizeBytes")).toLongLong(),
                 Q_INT64_C(12));
        QVERIFY(newest.value(QStringLiteral("mtimeMs")).toLongLong() > 0);
        QVERIFY(QFileInfo(newest.value(QStringLiteral("path")).toString()).isAbsolute());
        QVERIFY(QFile::exists(newest.value(QStringLiteral("path")).toString()));

        const QVariantMap oldest = backups.at(1).toMap();
        QCOMPARE(oldest.value(QStringLiteral("fileName")).toString(),
                 QStringLiteral("auto_20250101_000000.db"));
        QCOMPARE(oldest.value(QStringLiteral("isAuto")).toBool(), true);
        QVERIFY(oldest.value(QStringLiteral("mtimeMs")).toLongLong()
                <= newest.value(QStringLiteral("mtimeMs")).toLongLong());

        // QML 的错误路径：非 *.db 的文件不会出现在列表里。
        for (const QVariant& value : backups) {
            QVERIFY(!value.toMap().value(QStringLiteral("fileName")).toString()
                         .endsWith(QStringLiteral(".txt")));
        }
    }

    // 删除：只允许备份目录的直接子文件；外部路径、子目录、缺失文件一律拒绝。
    void deleteBackupValidatesPath() {
        AgentViewModel vm(nullptr);
        const QString backupsDir = SqliteMemory::defaultBackupsDir();

        // 目录外（同卷不同目录）——即使文件真实存在也不能删。
        QTemporaryDir outside;
        QVERIFY(outside.isValid());
        const QString outsideFile =
            outside.filePath(QStringLiteral("manual_outside.db"));
        QVERIFY(writeFile(outsideFile, QByteArray("x")));
        QVERIFY(!vm.deleteDatabaseBackup(outsideFile).isEmpty());
        QVERIFY(QFile::exists(outsideFile));

        // 备份目录下的子目录文件：相对路径含分隔符，仍然拒绝。
        const QString nestedDir = backupsDir + QStringLiteral("/nested");
        QVERIFY(QDir().mkpath(nestedDir));
        const QString nestedFile = nestedDir + QStringLiteral("/manual_nested.db");
        QVERIFY(writeFile(nestedFile, QByteArray("x")));
        QVERIFY(!vm.deleteDatabaseBackup(nestedFile).isEmpty());
        QVERIFY(QFile::exists(nestedFile));

        // 空路径 / 不存在的文件。
        QVERIFY(!vm.deleteDatabaseBackup(QString()).isEmpty());
        QVERIFY(!vm.deleteDatabaseBackup(
                     backupsDir + QStringLiteral("/manual_missing.db")).isEmpty());

        // 正常路径：直接子文件，删除成功且文件消失。
        const QString valid = backupsDir + QStringLiteral("/manual_deletable.db");
        QVERIFY(writeFile(valid, QByteArray("x")));
        QCOMPARE(vm.deleteDatabaseBackup(valid), QString());
        QVERIFY(!QFile::exists(valid));

        // QML FileDialog 交来的是 file URL —— 两种形式都要接受。
        const QString validUrl = backupsDir + QStringLiteral("/auto_deletable.db");
        QVERIFY(writeFile(validUrl, QByteArray("x")));
        QCOMPARE(vm.deleteDatabaseBackup(QUrl::fromLocalFile(validUrl).toString()),
                 QString());
        QVERIFY(!QFile::exists(validUrl));
    }

    // 删除后列表立即少一项（页面刷新走同一条数据源）。
    void listReflectsDeletion() {
        AgentViewModel vm(nullptr);
        const QString path = m_backupsDir + QStringLiteral("/auto_refresh.db");
        QVERIFY(writeFile(path, QByteArray("x")));
        const int before = vm.databaseBackups().size();
        QVERIFY(before > 0);
        QCOMPARE(vm.deleteDatabaseBackup(path), QString());
        QCOMPARE(vm.databaseBackups().size(), before - 1);
    }

    // ── B2-B（docs/36 §3.5）：恢复 swap 崩溃的启动自愈 ──────────
    void startupRecoversMissingDatabaseFromAnchor();
    void startupCleansStaleRestoreTmp();
    void startupRetiresAnchorWhenLiveDatabaseValid();
    // B2 收口（P1-1/P2-5）：锚点归属 + restore intent。
    void staleAnchorEntryIsNotUsedWhenAnchorStepNeverRan();
    void restoreIntentWithEmptyDatabaseRecoversAnchor();
    void restoreIntentWithoutRecoverableAnchorFailsClosed();

// swap 在 remove(dbPath) 之后、rename 之前断电：dbPath 缺失、`.pre-restore`
// 仍持有原库。启动必须自动恢复并消费锚点，而不是新建空库。
private:
    QTemporaryDir m_settingsDir;
    QString m_backupsDir;
};

void TestDbBackups::startupRecoversMissingDatabaseFromAnchor() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString livePath = dir.filePath(QStringLiteral("live.db"));
    {
        SqliteMemory writer(livePath);
        writer.beginSession(QStringLiteral("anchor-session"),
                            QStringLiteral("Original"));
        writer.saveMessage(QStringLiteral("anchor-session"),
                           QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                       {QStringLiteral("content"), QStringLiteral("original data")}});
    }
    const QString anchor = livePath + QStringLiteral(".pre-restore");
    QVERIFY(QFile::rename(livePath, anchor));
    QVERIFY(!QFile::exists(livePath));

    SqliteMemory recovered(livePath);
    const QJsonArray sessions = recovered.listSessions(10);
    QCOMPARE(sessions.size(), 1);
    QCOMPARE(sessions.at(0).toObject().value(QStringLiteral("id")).toString(),
             QStringLiteral("anchor-session"));
    QCOMPARE(recovered.loadMessages(QStringLiteral("anchor-session"), 10).size(), 1);
    QVERIFY(QFile::exists(livePath));
    QVERIFY(!QFile::exists(anchor));   // 恢复成功后锚点被消费
    // 恢复出来的库可继续写入。
    recovered.beginSession(QStringLiteral("after"), QStringLiteral("After"));
    QCOMPARE(recovered.listSessions(10).size(), 2);
}

// `.restore-tmp` 是 swap 前的暂存副本；上次崩溃留下的残片必须在启动时清掉，
// 否则下一次 restore 会撞上陈旧目标。
void TestDbBackups::startupCleansStaleRestoreTmp() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString livePath = dir.filePath(QStringLiteral("live.db"));
    {
        SqliteMemory writer(livePath);
        writer.beginSession(QStringLiteral("keep"), QStringLiteral("Keep"));
        writer.saveMessage(QStringLiteral("keep"),
                           QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                       {QStringLiteral("content"), QStringLiteral("kept")}});
    }
    const QString tmp = livePath + QStringLiteral(".restore-tmp");
    QVERIFY(writeFile(tmp, QByteArray("stale staging copy")));

    SqliteMemory reopened(livePath);
    QVERIFY(!QFile::exists(tmp));
    QCOMPARE(reopened.listSessions(10).size(), 1);
    QCOMPARE(reopened.loadMessages(QStringLiteral("keep"), 10).size(), 1);
}

// swap 已完成（新库已落盘）但进程在删除锚点前退出：启动保留已恢复的库并
// 清理锚点，不能把新库又换回旧数据。
void TestDbBackups::startupRetiresAnchorWhenLiveDatabaseValid() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString livePath = dir.filePath(QStringLiteral("live.db"));
    {
        SqliteMemory writer(livePath);
        writer.beginSession(QStringLiteral("new-session"), QStringLiteral("New"));
        writer.saveMessage(QStringLiteral("new-session"),
                           QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                       {QStringLiteral("content"), QStringLiteral("new data")}});
    }
    // 原库（锚点）与已恢复的新库内容不同。
    const QString anchor = livePath + QStringLiteral(".pre-restore");
    {
        SqliteMemory old(livePath + QStringLiteral(".old-src.db"));
        old.beginSession(QStringLiteral("old-session"), QStringLiteral("Old"));
    }
    QVERIFY(QFile::rename(livePath + QStringLiteral(".old-src.db"), anchor));

    SqliteMemory reopened(livePath);
    const QJsonArray sessions = reopened.listSessions(10);
    QCOMPARE(sessions.size(), 1);
    QCOMPARE(sessions.at(0).toObject().value(QStringLiteral("id")).toString(),
             QStringLiteral("new-session"));
    QVERIFY(!QFile::exists(anchor));
}


// ── B2 收口：失败路径 / restore intent ───────────────────────

// P1 复现：锚点路径被非空目录占据时，锚点 rename 根本不会发生；失败回滚
// 必须保持 live 库原样（不能删掉它再把目录 rename 上去）。
void TestDbBackups::staleAnchorEntryIsNotUsedWhenAnchorStepNeverRan() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString livePath = dir.filePath(QStringLiteral("live.db"));
    {
        SqliteMemory writer(livePath);
        writer.beginSession(QStringLiteral("before"), QStringLiteral("恢复前会话"));
        writer.saveMessage(QStringLiteral("before"),
                           QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                       {QStringLiteral("content"), QStringLiteral("仍然可用")}});
    }

    // test_viewmodel::restoreFailureKeepsTabSteadyState 的同款条件。
    const QString anchor = livePath + QStringLiteral(".pre-restore");
    QVERIFY(QDir().mkpath(anchor));
    {
        QFile blocker(anchor + QStringLiteral("/blocker.dat"));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        QCOMPARE(blocker.write("x"), qint64(1));
    }

    // 损坏备份：integrity gate 在 swap 之前失败，走 failAndReopen。
    const QString badBackup = dir.filePath(QStringLiteral("bad.db"));
    QVERIFY(writeFile(badBackup, QByteArray("not a sqlite database")));

    SqliteMemory memory(livePath);
    QVERIFY(!memory.restoreFromBackup(badBackup));
    QVERIFY(QFileInfo(livePath).isFile());     // live 库没被删/替换成目录
    QVERIFY(QFileInfo(anchor).isDir());        // 目录锚点未被消费
    QCOMPARE(memory.loadMessages(QStringLiteral("before"), 10).size(), 1);
    QVERIFY(memory.saveMessage(
                QStringLiteral("before"),
                QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                            {QStringLiteral("content"), QStringLiteral("仍然可用")}})
            > 0);
}

// 断电在 swap 中途：intent 已落盘、dbPath 是 0 字节残片、锚点是完整原库。
// 启动必须以锚点为准回滚，0 字节文件不算“完好库”。
void TestDbBackups::restoreIntentWithEmptyDatabaseRecoversAnchor() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString livePath = dir.filePath(QStringLiteral("live.db"));
    const QString anchor = livePath + QStringLiteral(".pre-restore");
    const QString intent = livePath + QStringLiteral(".restore-intent");
    {
        SqliteMemory writer(livePath);
        writer.beginSession(QStringLiteral("anchor-session"), QStringLiteral("Anchor"));
        writer.saveMessage(QStringLiteral("anchor-session"),
                           QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                       {QStringLiteral("content"), QStringLiteral("原库数据")}});
    }
    QVERIFY(QFile::rename(livePath, anchor));
    { QFile empty(livePath); QVERIFY(empty.open(QIODevice::WriteOnly)); }  // 0 字节
    QCOMPARE(QFileInfo(livePath).size(), qint64(0));
    { QFile marker(intent); QVERIFY(marker.open(QIODevice::WriteOnly));
      QCOMPARE(marker.write(QByteArrayLiteral("restore-intent")), qint64(14)); }

    SqliteMemory recovered(livePath);
    const QJsonArray sessions = recovered.listSessions(10);
    QCOMPARE(sessions.size(), 1);
    QCOMPARE(sessions.at(0).toObject().value(QStringLiteral("id")).toString(),
             QStringLiteral("anchor-session"));
    QCOMPARE(recovered.loadMessages(QStringLiteral("anchor-session"), 10).size(), 1);
    QVERIFY(QFileInfo(livePath).size() > 0);
    QVERIFY(!QFile::exists(anchor));    // rename 恢复，锚点已消费
    QVERIFY(!QFile::exists(intent));    // 意图已清
}

// intent 存在但没有任何可恢复锚点：必须 fail-closed，不能静默新建空库。
void TestDbBackups::restoreIntentWithoutRecoverableAnchorFailsClosed() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString livePath = dir.filePath(QStringLiteral("live.db"));
    const QString intent = livePath + QStringLiteral(".restore-intent");
    QVERIFY(!QFile::exists(livePath));
    { QFile marker(intent); QVERIFY(marker.open(QIODevice::WriteOnly));
      QCOMPARE(marker.write(QByteArrayLiteral("restore-intent")), qint64(14)); }

    SqliteMemory memory(livePath);
    // 连接未建立：写入失败且没有落下空库，意图保留供下次重试。
    QCOMPARE(memory.saveMessage(
                 QStringLiteral("s"),
                 QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                             {QStringLiteral("content"), QStringLiteral("x")}}),
             qint64(0));
    QVERIFY(!QFile::exists(livePath));
    QVERIFY(QFile::exists(intent));
}

QTEST_GUILESS_MAIN(TestDbBackups)
#include "test_db_backups.moc"
