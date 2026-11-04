#include "core/skills/SkillRegistry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include <algorithm>

namespace NexusAI {
namespace {

// QFileInfo::canonicalFilePath resolves symlinks on POSIX but leaves Windows
// junctions untouched (Qt reports isSymLink() == false for them). Ask the OS
// for the final path of an open handle instead, so a junction inside a skills
// root cannot expose a SKILL.md that lives outside it.
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

constexpr int kDescriptionMaxChars = 200;

// 判断 canonicalPath 是否严格位于 canonicalRoot 之内。两个路径都必须已经
// 是 canonical 形式（QFileInfo::canonicalFilePath），这样 symlink/junction
// 越界会在调用前已被解析成真实目标。
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

// 截断发生在 UTF-8 多字节字符中间时，丢弃尾部不完整序列，避免注入系统提示
// 时出现 U+FFFD。
QByteArray trimIncompleteUtf8Tail(QByteArray raw) {
    if (raw.isEmpty())
        return raw;
    const qsizetype size = raw.size();
    qsizetype lead = size - 1;
    int continuation = 0;
    while (lead >= 0 && continuation < 3
           && (static_cast<unsigned char>(raw.at(lead)) & 0xC0) == 0x80) {
        --lead;
        ++continuation;
    }
    if (lead < 0)
        return raw;

    const unsigned char c = static_cast<unsigned char>(raw.at(lead));
    qsizetype needed = 0;
    if ((c & 0x80) == 0x00)
        needed = 1; // ASCII
    else if ((c & 0xE0) == 0xC0)
        needed = 2;
    else if ((c & 0xF0) == 0xE0)
        needed = 3;
    else if ((c & 0xF8) == 0xF0)
        needed = 4;
    else
        return raw; // 非法首字节：交给解码器替换，不猜测

    if (lead + needed <= size)
        return raw; // 尾部序列完整
    raw.truncate(lead);
    return raw;
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

// 宽松解析 allowed-tools：支持 YAML 行内数组与逗号分隔两种写法，也容忍引号。
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

struct FrontmatterMeta {
    bool present{false};
    QString name;
    QString description;
    QStringList allowedTools;
    qint64 maxBytes{-1};
    int bodyOffset{0}; // frontmatter 结束后的字符位置（body 起点）
};

// 宽松 frontmatter：
//   * 首行必须是 `---`；否则整篇当作正文；
//   * 结束 `---` 缺失、key 不合法、值畸形都不报错，只是回退到默认值；
//   * 不引入 YAML 库，只按第一个 `:` 切 key/value。
FrontmatterMeta parseFrontmatter(const QString& text) {
    FrontmatterMeta meta;

    const int firstLineEnd = text.indexOf(QLatin1Char('\n'));
    const QString firstLine =
        (firstLineEnd < 0 ? text : text.left(firstLineEnd)).trimmed();
    if (firstLine != QLatin1String("---"))
        return meta; // 无 frontmatter（或首行是空行）

    int pos = firstLineEnd + 1;
    bool closed = false;
    int bodyOffset = 0;
    while (pos <= text.size()) {
        const int eol = text.indexOf(QLatin1Char('\n'), pos);
        const int lineEnd = eol < 0 ? text.size() : eol;
        QString line = text.mid(pos, lineEnd - pos);
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        if (eol < 0) {
            pos = text.size();
            break;
        }
        pos = eol + 1;
        if (line.trimmed() == QLatin1String("---")) {
            closed = true;
            bodyOffset = pos;
            break;
        }

        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        const QString key = line.left(colon).trimmed().toLower();
        const QString value = unquote(line.mid(colon + 1).trimmed());
        if (key == QLatin1String("name")) {
            meta.name = value;
        } else if (key == QLatin1String("description")) {
            meta.description = value;
        } else if (key == QLatin1String("allowed-tools")) {
            meta.allowedTools = parseAllowedTools(value);
        } else if (key == QLatin1String("max-bytes")) {
            bool ok = false;
            const qint64 parsed = value.toLongLong(&ok);
            if (ok && parsed > 0)
                meta.maxBytes = parsed;
        }
        // version 也是合法键，本期不存储（docs/37 §2.2）。
    }

    if (!closed)
        return FrontmatterMeta{}; // 未闭合：整篇当正文，绝不吃掉内容
    meta.present = true;
    meta.bodyOffset = bodyOffset;
    return meta;
}

QString firstNonEmptyLine(const QString& body) {
    const QStringList lines = body.split(QLatin1Char('\n'));
    for (QString line : lines) {
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        line = line.trimmed();
        if (!line.isEmpty())
            return line.left(kDescriptionMaxChars);
    }
    return QString();
}

// 去掉 frontmatter 结束行留下的换行，避免注入时正文以空行开头。
QString trimLeadingLineBreaks(QString body) {
    while (!body.isEmpty()
           && (body.at(0) == QLatin1Char('\n') || body.at(0) == QLatin1Char('\r'))) {
        body.remove(0, 1);
    }
    return body;
}

bool loadSkill(const QString& id, const QString& canonicalPath, SkillSource source,
               Skill* out) {
    QFile file(canonicalPath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const qint64 fileSize = file.size();
    QByteArray raw = file.read(SkillRegistry::kMaxFileBytes);
    file.close();

    bool truncated = fileSize > SkillRegistry::kMaxFileBytes;
    QString text = decodeUtf8(raw);
    FrontmatterMeta meta = parseFrontmatter(text);

    // frontmatter 的 max-bytes 只能收紧上限，不能突破 64 KiB 安全边界。
    qint64 effectiveLimit = SkillRegistry::kMaxFileBytes;
    if (meta.maxBytes > 0 && meta.maxBytes < effectiveLimit)
        effectiveLimit = meta.maxBytes;

    if (fileSize > effectiveLimit) {
        truncated = true;
        if (raw.size() > effectiveLimit)
            raw.truncate(effectiveLimit);
        raw = trimIncompleteUtf8Tail(raw);
        text = decodeUtf8(raw);
        meta = parseFrontmatter(text);
    }

    Skill skill;
    skill.id = id;
    skill.source = source;
    skill.sourcePath = canonicalPath;
    skill.bytes = fileSize;
    skill.truncated = truncated;
    skill.allowedTools = meta.allowedTools;

    skill.name = meta.name.trimmed();
    if (skill.name.isEmpty())
        skill.name = id;

    skill.body = meta.present ? text.mid(meta.bodyOffset) : text;
    skill.body = trimLeadingLineBreaks(skill.body);

    skill.description = meta.description.trimmed();
    if (skill.description.isEmpty())
        skill.description = firstNonEmptyLine(skill.body);

    *out = skill;
    return true;
}

// 扫描一个 skills 根目录。先扫描用户再扫描项目，同 id 由项目覆盖用户。
void scanRoot(const QString& skillsRoot, SkillSource source, QVector<Skill>& merged,
              QHash<QString, int>& index) {
    if (skillsRoot.isEmpty())
        return;

    const QFileInfo rootInfo(skillsRoot);
    if (!rootInfo.exists() || !rootInfo.isDir())
        return;
    const QString rootCanonical = canonicalForContainment(skillsRoot);
    if (rootCanonical.isEmpty())
        return;

    const QFileInfoList entries = QDir(skillsRoot).entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& entry : entries) {
        const QString id = entry.fileName();
        // 跳过隐藏目录，避免临时目录/编辑器备份目录变成 Skill。
        if (id.isEmpty() || id.startsWith(QLatin1Char('.')))
            continue;

        const QFileInfo skillInfo(
            entry.absoluteFilePath() + QStringLiteral("/SKILL.md"));
        if (!skillInfo.isFile())
            continue;

        // 只有 canonical 目标仍在根目录内才接受；目录或 SKILL.md 本身是
        // 指向根外的 symlink/junction 时 canonicalFilePath 会落到根外。
        const QString canonical = canonicalForContainment(skillInfo.absoluteFilePath());
        if (canonical.isEmpty() || !pathIsInside(rootCanonical, canonical))
            continue;

        Skill skill;
        if (!loadSkill(id, canonical, source, &skill))
            continue;

        const auto existing = index.constFind(id);
        if (existing == index.cend()) {
            index.insert(id, merged.size());
            merged.append(skill);
        } else {
            // 项目扫描发生在用户扫描之后，因此这里只会用项目版本覆盖用户
            // 版本（同名去重）。
            merged[existing.value()] = skill;
        }
    }
}

} // namespace

SkillRegistry::SkillRegistry()
    : m_userRoot(defaultUserRoot()) {
}

QString SkillRegistry::defaultUserRoot() {
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (base.isEmpty())
        return QString();
    return QDir(base).filePath(QStringLiteral("skills"));
}

void SkillRegistry::setProjectRoot(const QString& projectRoot) {
    m_projectRoot = projectRoot;
}

void SkillRegistry::setUserRoot(const QString& userRoot) {
    m_userRoot = userRoot;
}

QString SkillRegistry::projectSkillsRoot() const {
    if (m_projectRoot.isEmpty())
        return QString();
    return QDir(m_projectRoot).filePath(QStringLiteral(".nexusai/skills"));
}

void SkillRegistry::reload() {
    QVector<Skill> merged;
    QHash<QString, int> index;

    // 顺序即优先级：用户先入，项目覆盖。
    scanRoot(m_userRoot, SkillSource::User, merged, index);
    scanRoot(projectSkillsRoot(), SkillSource::Project, merged, index);

    std::sort(merged.begin(), merged.end(), [](const Skill& a, const Skill& b) {
        const int cmp = a.id.compare(b.id, Qt::CaseInsensitive);
        if (cmp != 0)
            return cmp < 0;
        return a.id < b.id;
    });
    if (merged.size() > kMaxSkills)
        merged.resize(kMaxSkills);

    m_skills = std::move(merged);
    m_index.clear();
    m_index.reserve(m_skills.size());
    for (int i = 0; i < m_skills.size(); ++i)
        m_index.insert(m_skills.at(i).id, i);
}

QVector<Skill> SkillRegistry::search(const QString& query, int limit) const {
    if (limit <= 0)
        limit = kDefaultSearchLimit;
    const QString needle = query.trimmed();

    QVector<Skill> result;
    result.reserve(qMin(limit, static_cast<int>(m_skills.size())));
    for (const Skill& skill : m_skills) {
        if (result.size() >= limit)
            break;
        if (needle.isEmpty()
            || skill.name.contains(needle, Qt::CaseInsensitive)
            || skill.description.contains(needle, Qt::CaseInsensitive)) {
            result.append(skill);
        }
    }
    return result;
}

const Skill* SkillRegistry::find(const QString& id) const {
    const auto it = m_index.constFind(id);
    if (it == m_index.cend())
        return nullptr;
    return &m_skills.at(it.value());
}

} // namespace NexusAI