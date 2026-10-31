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
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>

#include "core/memory/SqliteMemory.h"
#include "viewmodels/AgentViewModel.h"

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

private:
    QTemporaryDir m_settingsDir;
    QString m_backupsDir;
};

QTEST_GUILESS_MAIN(TestDbBackups)
#include "test_db_backups.moc"
