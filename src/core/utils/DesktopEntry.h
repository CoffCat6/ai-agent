#pragma once

#include <QChar>
#include <QDir>
#include <QFileInfo>
#include <QLatin1Char>
#include <QLatin1String>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <utility>   // std::as_const

namespace NexusAI {
namespace DesktopEntry {

// docs/49 §4 阶段 2：openWithKnownEditor 的 Linux 对应（Windows 分支在
// ProjectFileViewModel.cpp 里读注册表）。Linux 没有集中的关联存储可供枚举，
// 应急打开走「xdg-mime 查默认应用 → 读 .desktop 的 Exec 行 → 直接拉起」。
// 本头文件只放纯函数（分词/字段代码展开/数据目录定位/条目读取），xdg-mime
// 进程调用留在 ViewModel 侧——单测不必经过真机桌面环境。

// Desktop Entry Exec= 行 → argv（spec 8.2）。字段代码：
//   %f/%u/%F/%U  → 文件路径（%F/%U 本意多文件，这里永远只有一份）
//   %i/%c/%k     → 图标 / 应用名 / desktop 文件路径，剔除
//   %%           → 字面 %
//   其余 %X      → 已废弃代码（%d/%D/%n/%N/%v/%m），剔除
// spec 禁止在引号内展开字段代码，但常见实现（gio/xdg-open）并不区分引号，
// 这里按「引号只影响分词」的宽松策略处理。整行没有任何文件字段代码时，把
// 路径追加为最后一个参数（xdg-open 对缺字段代码条目的实践）。
inline QStringList desktopExecArguments(const QString& exec,
                                        const QString& filePath) {
    // 1) 分词：空白分隔，"..." 与 '...' 成组；双引号内反斜杠只转义 " $ ` \
    //    四个字符（spec 逐字规则，其余按字面反斜杠），单引号内无转义，引号
    //    外反斜杠转义任意下一字符（\% 是 Exec 里表达字面百分号的正规写法）。
    QStringList tokens;
    QString current;
    bool inToken = false;
    QChar quote;
    for (int i = 0; i < exec.size(); ++i) {
        const QChar c = exec.at(i);
        if (!quote.isNull()) {
            if (c == quote) {
                quote = QChar();
            } else if (quote == QLatin1Char('"') && c == QLatin1Char('\\')
                       && i + 1 < exec.size()) {
                const QChar next = exec.at(++i);
                if (next == QLatin1Char('"') || next == QLatin1Char('\\')
                        || next == QLatin1Char('$')
                        || next == QLatin1Char('`'))
                    current += next;
                else {
                    current += QLatin1Char('\\');
                    current += next;
                }
            } else {
                current += c;
            }
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            quote = c;
            inToken = true;
            continue;
        }
        if (c == QLatin1Char('\\') && i + 1 < exec.size()) {
            current += exec.at(++i);
            inToken = true;
            continue;
        }
        if (c.isSpace()) {
            if (inToken) {
                tokens.append(current);
                current.clear();
                inToken = false;
            }
            continue;
        }
        current += c;
        inToken = true;
    }
    if (inToken || !current.isEmpty())
        tokens.append(current);

    // 2) 字段代码展开。
    QStringList args;
    bool sawFileCode = false;
    for (const QString& token : std::as_const(tokens)) {
        QString arg;
        for (int i = 0; i < token.size(); ++i) {
            const QChar c = token.at(i);
            if (c != QLatin1Char('%')) {
                arg += c;
                continue;
            }
            if (i + 1 >= token.size()) {   // 尾随孤立 %，按字面保留
                arg += c;
                break;
            }
            const QChar code = token.at(++i);
            switch (code.unicode()) {
            case u'f': case u'F': case u'u': case u'U':
                arg += filePath;
                sawFileCode = true;
                break;
            case u'%':
                arg += QLatin1Char('%');
                break;
            default:
                break;   // %i/%c/%k 与已废弃代码：剔除
            }
        }
        if (!arg.isEmpty())
            args.append(arg);
    }
    if (!sawFileCode)
        args.append(filePath);
    return args;
}

// XDG 数据目录（applications/ 的查找根，spec 3.15）：XDG_DATA_HOME 缺省
// ~/.local/share，XDG_DATA_DIRS 缺省 /usr/local/share:/usr/share。测试可
// 直接传自己的列表给 locateDesktopFile，不必动进程环境。
inline QStringList desktopDataDirs() {
    QStringList dirs;
    const QString dataHome = qEnvironmentVariable("XDG_DATA_HOME");
    dirs << (dataHome.isEmpty()
                 ? QDir::homePath() + QStringLiteral("/.local/share")
                 : dataHome);
    const QString dataDirs = qEnvironmentVariable("XDG_DATA_DIRS");
    if (dataDirs.isEmpty())
        dirs << QStringLiteral("/usr/local/share")
             << QStringLiteral("/usr/share");
    else
        dirs << dataDirs.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    return dirs;
}

// application id（xdg-mime 返回 "org.gnome.Nautilus.desktop" 这类）→
// .desktop 绝对路径，按 dataDirs 顺序取第一个命中。id 本身是绝对路径时
// 直接采用；找不到返回空串。
inline QString locateDesktopFile(const QString& id,
                                 const QStringList& dataDirs) {
    if (id.isEmpty())
        return {};
    if (QDir::isAbsolutePath(id))
        return QFileInfo::exists(id) ? id : QString();
    for (const QString& base : dataDirs) {
        const QString path = base + QStringLiteral("/applications/") + id;
        if (QFileInfo::exists(path))
            return path;
    }
    return {};
}

struct DesktopEntryInfo {
    bool valid = false;      // [Desktop Entry] 组存在且有 Exec
    bool hidden = false;     // Hidden=true（卸载残留，不应拉起）
    bool noDisplay = false;  // NoDisplay=true（不在启动器展示，同样不拉起）
    QString tryExec;         // 可执行探测键（可空 = 不校验）
    QString exec;            // 启动命令行（可含字段代码）
};

inline DesktopEntryInfo readDesktopEntry(const QString& path) {
    DesktopEntryInfo info;
    QSettings desktop(path, QSettings::IniFormat);   // beginGroup/value 非 const
    desktop.beginGroup(QStringLiteral("Desktop Entry"));
    const QString exec = desktop.value(QStringLiteral("Exec")).toString();
    if (exec.isEmpty()) {
        desktop.endGroup();
        return info;   // valid=false
    }
    info.valid = true;
    info.exec = exec;
    info.hidden = desktop.value(QStringLiteral("Hidden"), false).toBool();
    info.noDisplay = desktop.value(QStringLiteral("NoDisplay"), false).toBool();
    info.tryExec = desktop.value(QStringLiteral("TryExec")).toString();
    desktop.endGroup();
    return info;
}

// TryExec 解析（spec：可执行名或绝对路径；空 = 不校验）。
inline bool tryExecAvailable(const QString& tryExec) {
    if (tryExec.isEmpty())
        return true;
    if (QDir::isAbsolutePath(tryExec))
        return QFileInfo(tryExec).isExecutable();
    return !QStandardPaths::findExecutable(tryExec).isEmpty();
}

} // namespace DesktopEntry
} // namespace NexusAI
