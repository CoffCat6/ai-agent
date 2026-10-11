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
