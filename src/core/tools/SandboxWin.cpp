// docs/22 F4: Windows-only reparse-point resolution for the sandbox.
// Qt's QFileInfo::canonicalFilePath() keeps junction/symlink path text
// as-is on Windows (probed on Qt 6.9 / Win11: a junction directory
// canonicalizes to itself), so the sandbox cannot see through a planted
// junction with Qt alone. GetFinalPathNameByHandle does traverse reparse
// points and returns the real destination.
#include "core/tools/Sandbox.h"

#include <QDir>
#include <QString>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace NexusAI {

#ifdef Q_OS_WIN

namespace {

// Normalize one candidate system directory: trim quotes/spaces, drop empty
// values, dedupe case-insensitively (Windows comparisons are insensitive).
void appendSystemDir(QStringList& dirs, const QString& raw)
{
    QString value = raw.trimmed();
    if (value.size() >= 2 && value.startsWith(QLatin1Char('"'))
            && value.endsWith(QLatin1Char('"'))) {
        value = value.mid(1, value.size() - 2).trimmed();
    }
    if (value.isEmpty())
        return;
    const QString native =
        QDir::toNativeSeparators(QDir::cleanPath(value));
    if (native.isEmpty())
        return;
    for (const QString& existing : dirs) {
        if (existing.compare(native, Qt::CaseInsensitive) == 0)
            return;
    }
    dirs.append(native);
}

} // namespace

// docs/37 B4-D: never assume C:. The OS-reported values are correct on a
// non-C: system drive and for a custom SystemRoot.
QStringList sandboxWindowsSystemDirectories()
{
    QStringList dirs;
    appendSystemDir(dirs, qEnvironmentVariable("SystemRoot"));
    appendSystemDir(dirs, qEnvironmentVariable("windir"));

    // Fallback for stripped environments (e.g. services with a minimal env).
    wchar_t winDir[MAX_PATH + 1] = {};
    const UINT len = ::GetWindowsDirectoryW(winDir, MAX_PATH + 1);
    if (len > 0 && len <= MAX_PATH)
        appendSystemDir(dirs, QString::fromWCharArray(winDir, int(len)));

    // 32-bit processes see ProgramFiles as the WOW64 folder; ProgramW6432
    // keeps the native one visible. ProgramData is machine-wide app data and
    // is never a user-writable location.
    const char* const kEnvNames[] = {
        "ProgramFiles", "ProgramFiles(x86)", "ProgramW6432", "ProgramData"
    };
    for (const char* name : kEnvNames)
        appendSystemDir(dirs, qEnvironmentVariable(name));
    return dirs;
}

QString sandboxNativeFinalPath(const QString& path)
{
    const QString native = QDir::toNativeSeparators(path);
    const HANDLE handle = ::CreateFileW(
        reinterpret_cast<const wchar_t*>(native.utf16()),
        0,   // query attributes only — no read/write access required
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return QString();

    wchar_t buffer[32768];
    constexpr DWORD kCapacity = sizeof(buffer) / sizeof(wchar_t);
    const DWORD ret = ::GetFinalPathNameByHandleW(
        handle, buffer, kCapacity, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    ::CloseHandle(handle);
    if (ret == 0 || ret >= kCapacity)
        return QString();

    QString out = QString::fromWCharArray(buffer, int(ret));
    // "\\?\C:\..." → "C:\..."; "\\?\UNC\server\share" → "\\server\share".
    if (out.startsWith(QStringLiteral("\\\\?\\UNC\\")))
        out.remove(0, 8);
    else if (out.startsWith(QStringLiteral("\\\\?\\")))
        out.remove(0, 4);
    return out;
}

#endif // Q_OS_WIN

} // namespace NexusAI
