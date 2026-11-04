#pragma once

#include "core/skills/Skill.h"

#include <QHash>
#include <QString>
#include <QVector>

namespace NexusAI {

// docs/37 §2：发现 / 解析 / 搜索 Skill。
//
// 目录约定：
//   * 项目：`<projectRoot>/.nexusai/skills/<id>/SKILL.md`（高优先级）
//   * 用户：`<userRoot>/<id>/SKILL.md`（默认 AppConfigLocation/skills）
//
// 安全边界：
//   * 只接受两个根目录内、canonical 路径落在根目录内的文件；
//     指向根目录外的 symlink / junction 一律丢弃；
//   * 单文件最多 64 KiB（按 frontmatter `max-bytes` 可进一步收紧），超出截断
//     并标记 truncated；
//   * 合并后最多 200 条，按 id（大小写不敏感）排序后截断。
//
// 该类不是 QObject，也不发信号：调用方在需要时显式 reload()。所有方法都
// 应在同一线程调用（W3 的 AgentViewModel 在 GUI 线程持有一份）。
class SkillRegistry final {
public:
    static constexpr int kMaxSkills = 200;
    static constexpr qint64 kMaxFileBytes = qint64(64) * 1024;
    static constexpr int kDefaultSearchLimit = 50;

    SkillRegistry();

    // projectRoot 是项目根目录，不是 skills 目录；内部拼接 `.nexusai/skills`。
    // 置空表示不扫描项目 Skill。设置后需自行调用 reload()。
    void setProjectRoot(const QString& projectRoot);
    void setUserRoot(const QString& userRoot);
    QString projectRoot() const { return m_projectRoot; }
    QString userRoot() const { return m_userRoot; }
    QString projectSkillsRoot() const;

    // 默认用户目录：QStandardPaths::AppConfigLocation + "/skills"。
    static QString defaultUserRoot();

    // 重新扫描两个根目录：项目覆盖用户同名 Skill，按 id 排序并应用总量上限。
    void reload();

    const QVector<Skill>& all() const { return m_skills; }

    // name/description 子串匹配，大小写不敏感；空查询返回全部（受 limit 约束）。
    QVector<Skill> search(const QString& query, int limit = kDefaultSearchLimit) const;

    // 按目录 id 精确查找；返回的指针在下一次 reload() 前有效。
    const Skill* find(const QString& id) const;

private:
    QString m_projectRoot;
    QString m_userRoot;
    QVector<Skill> m_skills;
    QHash<QString, int> m_index; // id -> m_skills 下标
};

} // namespace NexusAI