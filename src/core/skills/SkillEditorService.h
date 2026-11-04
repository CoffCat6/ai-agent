#pragma once

#include <QString>
#include <QStringList>

namespace NexusAI {

// docs/37 §9 W5-2a：Skill 创建 / 编辑 / 删除的 core 层服务。
//
// 只负责单个 `<skillsRoot>/<id>/SKILL.md` 的读写与目录生命周期，不做 UI、
// 不执行 Skill 内的脚本/资源，也不依赖 QtGui/QML（属于 nexus_core）。
//
// 安全约束：
//   * id 只允许 `[A-Za-z0-9][A-Za-z0-9._-]{0,63}`，且拒绝 `..` 段/分隔符，
//     因此 id 永远只是 skillsRoot 下的单层目录名；
//   * 读写前对 skillsRoot / 技能目录 / SKILL.md 做 canonical containment，
//     指向根外的 symlink / junction 一律拒绝；
//   * SKILL.md 单文件最多 64 KiB（UTF-8），写入用 QSaveFile 原子替换；
//   * 覆盖前把旧文件复制为 `SKILL.md.bak`（先删除旧 bak）。
class SkillEditorService final {
public:
    static constexpr qint64 kMaxFileBytes = qint64(64) * 1024;

    // 编辑器中的完整草稿。id 同时是目录名；name/description 对应
    // frontmatter；body 是 frontmatter 之后的正文（原样保存）。
    struct Draft {
        QString id;
        QString name;
        QString description;
        QString body;
        QStringList allowedTools;
    };

    // 合法 id：1..64 个字符，首字符为字母或数字，其余允许 . _ -；
    // 另拒绝 "."、".."、任何含 ".." 的组合以及路径分隔符。
    static bool validateId(const QString& id, QString* error);

    // body 的 UTF-8 字节数不得超过 64 KiB。
    static bool validateBody(const QString& body, QString* error);

    // `<projectRoot>/.nexusai/skills`；projectRoot 为空时返回空串。
    static QString projectSkillsRoot(const QString& projectRoot);

    // 用户技能根目录（SkillRegistry::defaultUserRoot()）。
    static QString userSkillsRoot();

    // 读取 `<skillsRoot>/<id>/SKILL.md`。宽松解析 frontmatter（缺失/畸形
    // 时回退为空值，不报错），body 保留 frontmatter 之后的原始内容。
    // 文件不存在、超出大小上限或 canonical 越界时返回 false。
    static bool load(const QString& skillsRoot, const QString& id, Draft* out,
                     QString* error);

    // 创建或覆盖 `<skillsRoot>/<id>/SKILL.md`。
    // 文件已存在且 overwrite == false 时返回 false；overwrite == true 时
    // 先把旧文件复制为 SKILL.md.bak，再原子写入新内容。
    static bool save(const QString& skillsRoot, const Draft& draft, bool overwrite,
                     QString* error);

    // 删除 `<skillsRoot>/<id>/SKILL.md`，并在技能目录为空时删除该目录；
    // 永远不会删除 skillsRoot 本身。
    static bool remove(const QString& skillsRoot, const QString& id, QString* error);
};

} // namespace NexusAI