#include "core/skills/SkillEditorService.h"

#include "core/skills/SkillRegistry.h"
#include "core/tools/FileEditUtil.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>
#include <QVector>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace NexusAI {
namespace {

void setError(QString* error, const QString& message) {
    if (error)
        *error = message;
}

void clearError(QString* error) {
    if (error)
        error->clear();
}

// 结构化结果码：调用方（VM/QML）据此判断失败类型，不再匹配中文文案。
void setCode(QString* code, const char* value) {
    if (code)
        *code = QString::fromLatin1(value);
}

// QFileInfo::canonicalFilePath() resolves symlinks on POSIX but leaves Windows
// junctions untouched (Qt reports isSymLink() == false for them). Ask the OS
// for the final path of an open handle instead, so a junction inside a skills
// root cannot expose a SKILL.md that lives outside it. 与 SkillRegistry 的
// 实现保持一致：core 层两个调用点共用同一套安全语义。
QString canonicalForContainment(const QString& path) {
#ifdef Q_OS_WIN
    const QString native = QDir::toNativeSeparators(path);
    HANDLE handle = CreateFileW(
        reinterpret_cast<LPCWSTR>(native.utf16()), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return QString();

    QString result;
    const DWORD needed = GetFinalPathNameByHandleW(
        handle, nullptr, 0, FILE_NAME_NORMALIZED);
    if (needed > 0) {
        QVector<wchar_t> buffer(static_cast<int>(needed) + 1);
        const DWORD written = GetFinalPathNameByHandleW(
            handle, buffer.data(), static_cast<DWORD>(buffer.size()),
            FILE_NAME_NORMALIZED);
        if (written > 0 && written < static_cast<DWORD>(buffer.size())) {
            result = QString::fromWCharArray(buffer.constData(),
                                             static_cast<int>(written));
        }
    }
    CloseHandle(handle);

    // Strip the extended-length prefix the API returns.
    if (result.startsWith(QStringLiteral("\\\\?\\UNC\\")))
        result = QStringLiteral("\\\\") + result.mid(8);
    else if (result.startsWith(QStringLiteral("\\\\?\\")))
        result = result.mid(4);
    return QDir::cleanPath(QDir::fromNativeSeparators(result));
#else
    return QFileInfo(path).canonicalFilePath();
#endif
}

// 判断 canonicalPath 是否严格位于 canonicalRoot 之内。两个路径都必须已经
// 是 canonical 形式，这样 symlink/junction 越界会在调用前已被解析成真实
// 目标；调用方负责按平台选择大小写敏感性。
bool pathIsInside(const QString& canonicalRoot, const QString& canonicalPath) {
    if (canonicalRoot.isEmpty() || canonicalPath.isEmpty())
        return false;
    QString root = canonicalRoot;
    while (root.endsWith(QLatin1Char('/')) || root.endsWith(QLatin1Char('\\')))
        root.chop(1);
    if (root.isEmpty())
        return false;

#if defined(Q_OS_WIN)
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif

    if (canonicalPath.size() <= root.size())
        return false;
    if (!canonicalPath.startsWith(root, cs))
        return false;
    const QChar next = canonicalPath.at(root.size());
    return next == QLatin1Char('/') || next == QLatin1Char('\\');
}

QString decodeUtf8(const QByteArray& raw) {
    QString text = QString::fromUtf8(raw);
    // 有些编辑器会写入 BOM；frontmatter 的 `---` 必须仍然在首列。
    if (!text.isEmpty() && text.at(0) == QChar(0xFEFF))
        text.remove(0, 1);
    return text;
}

QString unquote(QString value) {
    if (value.size() >= 2) {
        const QChar first = value.at(0);
        const QChar last = value.at(value.size() - 1);
        if ((first == QLatin1Char('"') && last == QLatin1Char('"'))
            || (first == QLatin1Char('\'') && last == QLatin1Char('\''))) {
            return value.mid(1, value.size() - 2);
        }
    }
    return value;
}

// 与 SkillRegistry 相同：支持 YAML 行内数组与逗号分隔两种写法。
QStringList parseAllowedTools(const QString& rawValue) {
    QString value = rawValue.trimmed();
    if (value.startsWith(QLatin1Char('[')) && value.endsWith(QLatin1Char(']')))
        value = value.mid(1, value.size() - 2);

    QStringList tools;
    const QStringList parts = value.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        const QString tool = unquote(part.trimmed());
        if (!tool.isEmpty())
            tools.append(tool);
    }
    return tools;
}

struct Frontmatter {
    bool present{false};
    QString name;
    QString description;
    QStringList allowedTools;
    // docs/38 §3：键存在即视为已声明，即使值为空（`allowed-tools: []`）。
    bool allowedToolsSpecified{false};
    int bodyOffset{0};
};

// 宽松 frontmatter：首行必须是 `---`，遇到下一行 `---` 结束；未闭合、
// key 非法或值畸形都不报错，只是忽略该项。只按第一个 `:` 切分，不引入
// YAML 库。与 SkillRegistry 的解析保持一致。
Frontmatter parseFrontmatter(const QString& text) {
    Frontmatter meta;

    const int firstLineEnd = text.indexOf(QLatin1Char('\n'));
    const QString firstLine =
        (firstLineEnd < 0 ? text : text.left(firstLineEnd)).trimmed();
    if (firstLine != QLatin1String("---"))
        return meta;

    int pos = firstLineEnd + 1;
    while (pos <= text.size()) {
        const int eol = text.indexOf(QLatin1Char('\n'), pos);
        const int lineEnd = eol < 0 ? text.size() : eol;
        QString line = text.mid(pos, lineEnd - pos);
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);

        if (line.trimmed() == QLatin1String("---")) {
            meta.present = true;
            meta.bodyOffset = eol < 0 ? text.size() : eol + 1;
            return meta;
        }
        if (eol < 0)
            break;

        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon > 0) {
            const QString key = line.left(colon).trimmed().toLower();
            const QString value = unquote(line.mid(colon + 1).trimmed());
            if (key == QLatin1String("name"))
                meta.name = value;
            else if (key == QLatin1String("description"))
                meta.description = value;
            else if (key == QLatin1String("allowed-tools")) {
                meta.allowedToolsSpecified = true;
                meta.allowedTools = parseAllowedTools(value);
            }
        }
        pos = eol + 1;
    }

    // 未闭合：整篇当正文，绝不吃掉内容。
    return Frontmatter{};
}

// 防止 name/description 把内容注入到 frontmatter：换行会在 frontmatter
// 里新增键或提前闭合，单独的 `---` 行则直接结束 frontmatter。
bool validateMetadataValue(const QString& value, const QString& field,
                           QString* error) {
    if (value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r'))) {
        setError(error, QStringLiteral("%1 不能包含换行符").arg(field));
        return false;
    }
    if (value.trimmed() == QLatin1String("---")) {
        setError(error, QStringLiteral("%1 不能是 frontmatter 分隔符").arg(field));
        return false;
    }
    return true;
}

bool validateToolName(const QString& tool, QString* error) {
    if (tool.contains(QLatin1Char('\n')) || tool.contains(QLatin1Char('\r'))
        || tool.contains(QLatin1Char(',')) || tool.contains(QLatin1Char('['))
        || tool.contains(QLatin1Char(']'))) {
        setError(error, QStringLiteral("allowed-tools 项不能包含换行或 , [ ]"));
        return false;
    }
    return true;
}

// 序列化为 SKILL.md 内容。只写非空的可选字段；body 紧跟在结束的
// `---\n` 之后，因此 load() 可以原样取回。
QByteArray serialize(const SkillEditorService::Draft& draft) {
    QString content;
    content += QStringLiteral("---\n");
    if (!draft.name.isEmpty())
        content += QStringLiteral("name: %1\n").arg(draft.name);
    if (!draft.description.isEmpty())
        content += QStringLiteral("description: %1\n").arg(draft.description);
    if (!draft.allowedTools.isEmpty())
        content += QStringLiteral("allowed-tools: [%1]\n")
                       .arg(draft.allowedTools.join(QStringLiteral(", ")));
    else if (draft.allowedToolsSpecified)
        // 已声明但为空：必须显式写出 `[]`，否则重新加载会退化为“未声明”。
        content += QStringLiteral("allowed-tools: []\n");
    content += QStringLiteral("---\n");
    content += draft.body;
    return content.toUtf8();
}

// 创建（或确认存在）skillsRoot 并返回其 canonical 路径。
// 空串 = invalid_root；创建/解析失败 = root_unavailable。
bool ensureRoot(const QString& skillsRoot, QString* canonicalRoot, QString* error,
                QString* code) {
    if (skillsRoot.trimmed().isEmpty()) {
        setError(error, QStringLiteral("技能根目录不能为空"));
        setCode(code, "invalid_root");
        return false;
    }
    if (!QDir().mkpath(skillsRoot)) {
        setError(error, QStringLiteral("无法创建技能根目录：%1").arg(skillsRoot));
        setCode(code, "root_unavailable");
        return false;
    }
    const QString canonical = canonicalForContainment(skillsRoot);
    if (canonical.isEmpty()) {
        setError(error, QStringLiteral("技能根目录不可用：%1").arg(skillsRoot));
        setCode(code, "root_unavailable");
        return false;
    }
    *canonicalRoot = canonical;
    return true;
}

// load/remove 不创建根目录：根不存在等价于没有技能。
bool existingRoot(const QString& skillsRoot, QString* canonicalRoot, QString* error,
                  QString* code) {
    if (skillsRoot.trimmed().isEmpty()) {
        setError(error, QStringLiteral("技能根目录不能为空"));
        setCode(code, "invalid_root");
        return false;
    }
    const QFileInfo info(skillsRoot);
    if (!info.exists() || !info.isDir()) {
        setError(error, QStringLiteral("技能根目录不存在：%1").arg(skillsRoot));
        setCode(code, "root_unavailable");
        return false;
    }
    const QString canonical = canonicalForContainment(skillsRoot);
    if (canonical.isEmpty()) {
        setError(error, QStringLiteral("技能根目录不可用：%1").arg(skillsRoot));
        setCode(code, "root_unavailable");
        return false;
    }
    *canonicalRoot = canonical;
    return true;
}

} // namespace

bool SkillEditorService::validateId(const QString& id, QString* error) {
    // Windows 会去掉目录名末尾的点和空格（`review.` 与 `review` 是同一
    // 目录），提前拒绝，避免两个技能互相覆盖。先于正则检查，保证错误
    // 文案说明的是“结尾字符”而不是泛化的字符集限制。
    if (!id.isEmpty()
        && (id.endsWith(QLatin1Char('.')) || id.endsWith(QLatin1Char(' ')))) {
        setError(error, QStringLiteral("技能 id 不能以点或空格结尾（Windows "
                                       "会把它归一化成同名目录）"));
        return false;
    }
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$"));
    if (id.isEmpty() || !pattern.match(id).hasMatch()) {
        setError(error,
                 QStringLiteral("技能 id 必须为 1-64 个字母、数字、. _ -，"
                                "且以字母或数字开头"));
        return false;
    }
    // Windows 保留设备名：即使带扩展名（CON.txt）也会被系统特殊处理。
    // 取第一个点之前的基名，大小写不敏感比较。
    static const QStringList kReservedNames{
        QStringLiteral("CON"),  QStringLiteral("PRN"),  QStringLiteral("AUX"),
        QStringLiteral("NUL"),  QStringLiteral("COM1"), QStringLiteral("COM2"),
        QStringLiteral("COM3"), QStringLiteral("COM4"), QStringLiteral("COM5"),
        QStringLiteral("COM6"), QStringLiteral("COM7"), QStringLiteral("COM8"),
        QStringLiteral("COM9"), QStringLiteral("LPT1"), QStringLiteral("LPT2"),
        QStringLiteral("LPT3"), QStringLiteral("LPT4"), QStringLiteral("LPT5"),
        QStringLiteral("LPT6"), QStringLiteral("LPT7"), QStringLiteral("LPT8"),
        QStringLiteral("LPT9"),
    };
    const QString baseName =
        id.section(QLatin1Char('.'), 0, 0).toUpper();
    if (kReservedNames.contains(baseName)) {
        setError(error, QStringLiteral("技能 id 不能使用 Windows 保留设备名：%1")
                            .arg(baseName));
        return false;
    }
    // 正则已排除路径分隔符；这里再拦一次 "."/".." 以及任何 ".." 组合，
    // 避免调用方把 id 直接拼进路径时出现穿越。
    if (id == QLatin1String(".")
        || id.contains(QStringLiteral(".."))) {
        setError(error, QStringLiteral("技能 id 不能包含 .. 路径段"));
        return false;
    }
    clearError(error);
    return true;
}

bool SkillEditorService::validateBody(const QString& body, QString* error) {
    if (body.toUtf8().size() > kMaxFileBytes) {
        setError(error, QStringLiteral("技能正文超过 64 KiB 上限"));
        return false;
    }
    clearError(error);
    return true;
}

QString SkillEditorService::projectSkillsRoot(const QString& projectRoot) {
    if (projectRoot.isEmpty())
        return QString();
    return QDir(projectRoot).filePath(QStringLiteral(".nexusai/skills"));
}

QString SkillEditorService::userSkillsRoot() {
    return SkillRegistry::defaultUserRoot();
}

bool SkillEditorService::load(const QString& skillsRoot, const QString& id,
                              Draft* out, QString* error, QString* code) {
    if (!out) {
        setError(error, QStringLiteral("Draft 输出参数为空"));
        setCode(code, "io_error");
        return false;
    }
    if (!validateId(id, error)) {
        setCode(code, "invalid_id");
        return false;
    }

    QString canonicalRoot;
    if (!existingRoot(skillsRoot, &canonicalRoot, error, code))
        return false;

    const QString skillDir = QDir(canonicalRoot).filePath(id);
    const QString filePath = QDir(skillDir).filePath(QStringLiteral("SKILL.md"));
    const QFileInfo fileInfo(filePath);
    // 悬空链接 exists()==false，但仍需以 symlink 归类，而不是 not_found。
    if (!fileInfo.exists() && !fileInfo.isSymLink()) {
        setError(error, QStringLiteral("技能不存在：%1").arg(id));
        setCode(code, "not_found");
        return false;
    }
    if (!fileInfo.isFile()) {
        if (fileInfo.isSymLink()) {
            setError(error, QStringLiteral("SKILL.md 是符号链接，无法读取：%1").arg(id));
            setCode(code, "symlink");
        } else {
            setError(error, QStringLiteral("SKILL.md 不是普通文件：%1").arg(id));
            setCode(code, "not_file");
        }
        return false;
    }

    const QString canonicalFile = canonicalForContainment(filePath);
    if (canonicalFile.isEmpty()) {
        setError(error, QStringLiteral("无法解析技能文件路径：%1").arg(id));
        setCode(code, fileInfo.isSymLink() ? "symlink" : "io_error");
        return false;
    }
    if (!pathIsInside(canonicalRoot, canonicalFile)) {
        setError(error, QStringLiteral("技能文件越出技能根目录：%1").arg(id));
        setCode(code, fileInfo.isSymLink() ? "symlink" : "outside_root");
        return false;
    }

    QFile file(canonicalFile);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("无法读取技能文件：%1").arg(id));
        setCode(code, "io_error");
        return false;
    }
    if (file.size() > kMaxFileBytes) {
        setError(error, QStringLiteral("技能文件超过 64 KiB 上限：%1").arg(id));
        setCode(code, "too_large");
        return false;
    }
    const QByteArray raw = file.read(kMaxFileBytes);
    file.close();

    const QString text = decodeUtf8(raw);
    const Frontmatter meta = parseFrontmatter(text);

    Draft draft;
    draft.id = id;
    draft.name = meta.name;
    draft.description = meta.description;
    draft.allowedTools = meta.allowedTools;
    draft.allowedToolsSpecified = meta.allowedToolsSpecified;
    // 正文保留 frontmatter 之后的原始内容（包括开头空行与行尾换行）。
    draft.body = meta.present ? text.mid(meta.bodyOffset) : text;

    *out = draft;
    clearError(error);
    setCode(code, "ok");
    return true;
}

bool SkillEditorService::save(const QString& skillsRoot, const Draft& draft,
                              bool overwrite, QString* error, QString* code) {
    if (!validateId(draft.id, error)) {
        setCode(code, "invalid_id");
        return false;
    }
    if (!validateBody(draft.body, error)) {
        setCode(code, "too_large");
        return false;
    }
    if (!validateMetadataValue(draft.name, QStringLiteral("name"), error)) {
        setCode(code, "invalid_metadata");
        return false;
    }
    if (!validateMetadataValue(draft.description, QStringLiteral("description"),
                               error)) {
        setCode(code, "invalid_metadata");
        return false;
    }
    for (const QString& tool : draft.allowedTools) {
        if (!validateToolName(tool, error)) {
            setCode(code, "invalid_tools");
            return false;
        }
    }

    const QByteArray content = serialize(draft);
    if (content.size() > kMaxFileBytes) {
        setError(error, QStringLiteral("技能文件超过 64 KiB 上限"));
        setCode(code, "too_large");
        return false;
    }

    QString canonicalRoot;
    if (!ensureRoot(skillsRoot, &canonicalRoot, error, code))
        return false;

    const QString skillDir = QDir(canonicalRoot).filePath(draft.id);
    if (!QDir().mkpath(skillDir)) {
        setError(error, QStringLiteral("无法创建技能目录：%1").arg(draft.id));
        setCode(code, "io_error");
        return false;
    }

    // 目录可能是预先存在的 junction/symlink；必须在写入前确认 canonical
    // 目标仍在技能根内。
    const QString canonicalDir = canonicalForContainment(skillDir);
    if (canonicalDir.isEmpty()) {
        setError(error, QStringLiteral("无法解析技能目录：%1").arg(draft.id));
        setCode(code, "io_error");
        return false;
    }
    if (!pathIsInside(canonicalRoot, canonicalDir)) {
        setError(error, QStringLiteral("技能目录越出技能根目录：%1").arg(draft.id));
        setCode(code, "outside_root");
        return false;
    }
    // 根内目录级 symlink/junction（例如 alias -> victim）的 canonical
    // 目标仍在根内，但写入会落到被指向技能。canonical 路径与词法期望
    // 路径不一致即说明经过了链接/重解析点，一律拒绝。
    const QString expectedDir =
        QDir::cleanPath(QDir(canonicalRoot).filePath(draft.id));
#ifdef Q_OS_WIN
    const bool sameDir =
        canonicalDir.compare(expectedDir, Qt::CaseInsensitive) == 0;
#else
    const bool sameDir = canonicalDir == expectedDir;
#endif
    if (!sameDir) {
        setError(error,
                 QStringLiteral("技能目录经过链接/重解析点，拒绝保存：%1")
                     .arg(draft.id));
        setCode(code, "symlink");
        return false;
    }

    const QString filePath = QDir(canonicalDir).filePath(QStringLiteral("SKILL.md"));
    const PathWriteGuard guard(filePath);

    const QFileInfo existing(filePath);
    // 根内 symlink 的 canonical 目标也可能落在根内（指向另一个技能）；
    // 若继续写入会覆盖被指向的技能。QSaveFile 提交时会替换链接本身，
    // 但备份阶段可能顺链读到目标，且 replace 语义在不同平台不一致，
    // 因此一律拒绝（与 remove 保持一致）。
    if (existing.isSymLink()) {
        setError(error,
                 QStringLiteral("SKILL.md 是符号链接，拒绝保存：%1").arg(draft.id));
        setCode(code, "symlink");
        return false;
    }
    if (existing.exists()) {
        if (!existing.isFile()) {
            setError(error, QStringLiteral("SKILL.md 不是普通文件：%1").arg(draft.id));
            setCode(code, "not_file");
            return false;
        }
        const QString canonicalFile = canonicalForContainment(filePath);
        if (canonicalFile.isEmpty()) {
            setError(error, QStringLiteral("无法解析技能文件路径：%1").arg(draft.id));
            setCode(code, "io_error");
            return false;
        }
        if (!pathIsInside(canonicalRoot, canonicalFile)) {
            setError(error, QStringLiteral("技能文件越出技能根目录：%1").arg(draft.id));
            setCode(code, "outside_root");
            return false;
        }
        if (!overwrite) {
            setError(error, QStringLiteral("技能已存在：%1").arg(draft.id));
            setCode(code, "exists");
            return false;
        }

        // 覆盖前保留一份旧内容；先删除旧 .bak，避免 QFile::copy 因目标
        // 已存在而失败（也保证 .bak 始终对应最近一次覆盖前的版本）。
        const QString backupPath =
            QDir(canonicalDir).filePath(QStringLiteral("SKILL.md.bak"));
        // isSymLink() 覆盖悬空链接：QFileInfo::exists() 会跟随链接返回
        // false，若不删除，QFile::copy 可能顺着链接写到根外。QFile::remove
        // 只删除链接本身，不触碰目标。
        const QFileInfo backupInfo(backupPath);
        if ((backupInfo.exists() || backupInfo.isSymLink())
            && !QFile::remove(backupPath)) {
            setError(error, QStringLiteral("无法替换旧的 SKILL.md.bak"));
            setCode(code, "io_error");
            return false;
        }
        if (!QFile::copy(filePath, backupPath)) {
            setError(error, QStringLiteral("无法备份旧的 SKILL.md"));
            setCode(code, "io_error");
            return false;
        }
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, QStringLiteral("无法写入技能文件：%1").arg(draft.id));
        setCode(code, "io_error");
        return false;
    }
    if (file.write(content) != content.size()) {
        setError(error, QStringLiteral("技能文件写入不完整：%1").arg(draft.id));
        setCode(code, "io_error");
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        setError(error, QStringLiteral("技能文件保存失败：%1").arg(draft.id));
        setCode(code, "io_error");
        return false;
    }

    clearError(error);
    setCode(code, "ok");
    return true;
}

bool SkillEditorService::remove(const QString& skillsRoot, const QString& id,
                                QString* error, QString* code) {
    if (!validateId(id, error)) {
        setCode(code, "invalid_id");
        return false;
    }

    QString canonicalRoot;
    if (!existingRoot(skillsRoot, &canonicalRoot, error, code))
        return false;

    const QString skillDir = QDir(canonicalRoot).filePath(id);
    const QFileInfo dirInfo(skillDir);
    if (!dirInfo.exists() || !dirInfo.isDir()) {
        setError(error, QStringLiteral("技能不存在：%1").arg(id));
        setCode(code, "not_found");
        return false;
    }

    const QString canonicalDir = canonicalForContainment(skillDir);
    if (canonicalDir.isEmpty()) {
        setError(error, QStringLiteral("无法解析技能目录：%1").arg(id));
        setCode(code, dirInfo.isSymLink() ? "symlink" : "io_error");
        return false;
    }
    if (!pathIsInside(canonicalRoot, canonicalDir)) {
        setError(error, QStringLiteral("技能目录越出技能根目录：%1").arg(id));
        setCode(code, dirInfo.isSymLink() ? "symlink" : "outside_root");
        return false;
    }
    // 根内目录 junction/symlink 同样会删除被指向技能的文件/空目录；
    // canonical 与词法期望路径不一致即拒绝（与 save 的规则一致）。
    const QString expectedDir =
        QDir::cleanPath(QDir(canonicalRoot).filePath(id));
#ifdef Q_OS_WIN
    const bool sameDir =
        canonicalDir.compare(expectedDir, Qt::CaseInsensitive) == 0;
#else
    const bool sameDir = canonicalDir == expectedDir;
#endif
    if (!sameDir) {
        setError(error,
                 QStringLiteral("技能目录经过链接/重解析点，拒绝删除：%1").arg(id));
        setCode(code, "symlink");
        return false;
    }

    const QString filePath = QDir(canonicalDir).filePath(QStringLiteral("SKILL.md"));
    const QFileInfo fileInfo(filePath);
    if (fileInfo.exists() || fileInfo.isSymLink()) {
        if (fileInfo.isSymLink()) {
            setError(error,
                     QStringLiteral("SKILL.md 是符号链接，拒绝删除：%1").arg(id));
            setCode(code, "symlink");
            return false;
        }
        if (!fileInfo.isFile()) {
            setError(error, QStringLiteral("SKILL.md 不是普通文件：%1").arg(id));
            setCode(code, "not_file");
            return false;
        }
        const QString canonicalFile = canonicalForContainment(filePath);
        if (canonicalFile.isEmpty()) {
            setError(error, QStringLiteral("无法解析技能文件路径：%1").arg(id));
            setCode(code, "io_error");
            return false;
        }
        if (!pathIsInside(canonicalRoot, canonicalFile)) {
            setError(error, QStringLiteral("技能文件越出技能根目录：%1").arg(id));
            setCode(code, "outside_root");
            return false;
        }
        const PathWriteGuard guard(filePath);
        if (!QFile::remove(canonicalFile)) {
            setError(error, QStringLiteral("无法删除技能文件：%1").arg(id));
            setCode(code, "io_error");
            return false;
        }
    }

    // 仅清理空目录；目录里还有 .bak 或用户资源时保留。rmdir 只会删除
    // 空目录，失败视为「非空」，不算删除失败。canonicalDir 已在根内且
    // 不等于根，因此不会误删 skillsRoot。
    QDir().rmdir(canonicalDir);

    clearError(error);
    setCode(code, "ok");
    return true;
}

} // namespace NexusAI