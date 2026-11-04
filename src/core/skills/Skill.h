#pragma once

#include <QString>
#include <QStringList>

namespace NexusAI {

// docs/37 §2：一个 Skill 对应 `<root>/<id>/SKILL.md` 的解析结果。纯值类型，
// 不持有文件句柄，也不依赖 QtGui/QML —— 该头文件属于 nexus_core。
enum class SkillSource {
    Project, // <projectRoot>/.nexusai/skills
    User,    // <AppConfigLocation>/skills（或测试注入目录）
};

// UI/metadata 中使用的稳定来源标识（"project" / "user"）。
inline QString skillSourceId(SkillSource source) {
    return source == SkillSource::Project ? QStringLiteral("project")
                                          : QStringLiteral("user");
}

struct Skill {
    QString id;          // 目录名；去重与 find() 的键
    QString name;        // frontmatter name；缺省 = id
    QString description; // frontmatter description；缺省 = 首个非空正文行
    QString body;        // frontmatter 之后的正文（注入系统提示的唯一内容）
    QString sourcePath;  // SKILL.md 的 canonical 路径
    SkillSource source{SkillSource::User};
    qint64 bytes{0};     // 磁盘原始字节数（即使 truncated 也记录完整大小）
    QStringList allowedTools; // docs/38：W6 起作为本轮工具白名单生效
    // docs/38 §3：解析到 `allowed-tools` 键即为 true（即使值为空）；用于
    // 区分“未声明（不限制）”与“声明为空数组（限制为空集）”。
    bool allowedToolsSpecified{false};
    bool truncated{false};

    QString sourceId() const { return skillSourceId(source); }
};

} // namespace NexusAI