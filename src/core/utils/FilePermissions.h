#pragma once

#include <QFile>
#include <QFileInfo>

namespace NexusAI {
namespace FilePermissions {

// docs/49 D4：QSaveFile 的 commit 是「写临时文件 → rename 替换目标」，Linux
// 上新文件拿到 umask 默认权限——原文件的执行位/组权限会丢（典型场景：AI
// 编辑过 shell 脚本后 +x 消失）。写前记录目标权限、commit 成功后恢复；目标
// 不存在（新建文件）时 saved 为空，恢复是 no-op，保持 umask 默认。
//
// Windows 上替换继承目标属性、QFile::Permissions 只覆盖只读位，两个函数按
// 平台门控为 no-op，保持既有行为零变化。用法：
//   const QFile::Permissions saved = FilePermissions::captureForReplace(path);
//   QSaveFile f(path); ... f.commit();
//   FilePermissions::restoreAfterReplace(path, saved);
// （commit 失败/取消写走的路径没有发生替换，不要调 restore。）

inline QFile::Permissions captureForReplace(const QString& target) {
#ifdef Q_OS_WIN
    Q_UNUSED(target);
    return {};
#else
    return QFileInfo::exists(target) ? QFile(target).permissions()
                                     : QFile::Permissions{};
#endif
}

inline void restoreAfterReplace(const QString& target,
                                QFile::Permissions saved) {
#ifdef Q_OS_WIN
    Q_UNUSED(target);
    Q_UNUSED(saved);
#else
    if (saved) QFile::setPermissions(target, saved);   // 空集 = 新建文件，保持 umask
#endif
}

} // namespace FilePermissions
} // namespace NexusAI
