#pragma once

#include <QSettings>
#include <QTemporaryDir>

// 测试进程的 QSettings 结构性隔离。
//
// 此前各套件靠 SettingsGuard 手工枚举「可能被写到的键」守护用户真实存储，
// 新增键（如 agent/subagent*）一旦漏登记就会被测试改写并遗留——用户报告的
// 「子代理设置每次打开软件都要重新设置」，根因正是开发迭代中反复运行测试
// 把真实注册表里的设置冲成测试值。改为结构性隔离后：进程内 QSettings 默认
// 格式切到 INI 并重定向到临时目录，任何键的读写都随临时目录一起消失，
// 不再依赖键名清单的完整性。
//
// setPath 只对 INI 生效（Windows 上 NativeFormat 是注册表，无法重定向），
// 所以同时把默认格式切到 INI——SettingsRepository 与测试里的 QSettings
// 均按 defaultFormat / 默认作用域构造，会整体跟随重定向。
//
// 注意：Qt 6.9 的两参构造 QSettings(org, app) 无视 setDefaultFormat、在
// Windows 上永远落注册表（探针实测），因此测试代码一律用 testSettings()
// 构造，不要写 QSettings("NexusAI", "NexusAI")。
//
// 隔离后的存储是全新空库，schemaVersion 未写入，SettingsService 首次构造
// 会走一次 v0→最新迁移（对空库是无害的 no-op）；需要精确控制迁移起点的
// 用例（如 test_settings 的迁移组）自行写入版本号或沿用 SettingsGuard 的
// 预置约定。既有键级守护（SettingsGuard / KeyGuard 等）保持原样：它们守
// 的已是一个不再被测试触碰的真实存储，退化为对临时库的例行清理，无害。
namespace NexusAI::TestSupport {

class SettingsIsolation {
public:
    SettingsIsolation() {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_dir.path());
    }

    SettingsIsolation(const SettingsIsolation&) = delete;
    SettingsIsolation& operator=(const SettingsIsolation&) = delete;

private:
    QTemporaryDir m_dir;
};

// 与 SettingsRepository 相同的构造路径（defaultFormat + UserScope），
// 在隔离生效时落到临时 INI。
[[nodiscard]] inline QSettings testSettings() {
    return QSettings(QSettings::defaultFormat(), QSettings::UserScope,
                     QStringLiteral("NexusAI"), QStringLiteral("NexusAI"));
}

} // namespace NexusAI::TestSupport
