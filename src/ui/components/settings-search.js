// docs/17 B6: settings search — merged data source.
//
// Pure data-in/data-out: the caller (SettingsCenterDialog) supplies the
// registry-driven entries (declarative preferences with display names,
// built from PreferenceRegistry at runtime) plus the category order map;
// this library owns the hand-maintained index for page-specific settings
// that have no PreferenceMeta (模型配置页字段、联网搜索、数据库备份等)
// and the matching/dedup/sort logic.
//
// Deliberately dependency-free (no Qt/QML imports) so tests/qml can load
// it through a relative import without resolving the NexusAI module.
//
// Entry shape: { title, category, categoryName, keywords } — keywords is a
// space-joined alias string; matching is a case-insensitive substring over
// title + keywords + categoryName.
//
// docs/25 C3: 手工索引标题用 QT_TRANSLATE_NOOP 标记翻译键（该函数是纯
// no-op，在 .pragma library 中可用，不破坏 dependency-free 约束）；运行
// 时由调用方（SettingsCenterDialog）以 qsTranslate("SettingsSearch", …)
// 查询，英文界面搜索结果不再中英混杂。lupdate 会从本文件提取条目。
//
// 手工索引表（页, 标题, 关键词）：声明式偏好（界面/通用各偏好）由注册表
// 自动覆盖，不在此重复；此处只登记页面自绘的设置项与操作入口。新增设置
// 页字段时同步补一行。
.pragma library

var handIndex = [
    // ── 模型配置 ──────────────────────────────────────────
    { title: QT_TRANSLATE_NOOP("SettingsSearch", "配置来源"), category: "model", keywords: "profile 配置 协议 厂商 新建" },
    { title: QT_TRANSLATE_NOOP("SettingsSearch", "服务连接"), category: "model", keywords: "baseUrl api 地址 密钥 apiKey 连接 测试连接" },
    { title: QT_TRANSLATE_NOOP("SettingsSearch", "模型"), category: "model", keywords: "模型 上下文 context 窗口 模型列表" },
    { title: QT_TRANSLATE_NOOP("SettingsSearch", "生成参数"), category: "model", keywords: "温度 temperature 采样 top_p 生成" },
    { title: QT_TRANSLATE_NOOP("SettingsSearch", "思考与上下文"), category: "model", keywords: "思考 推理 reasoning 思考强度 上下文长度" },

    // ── 通用（声明式偏好之外的自绘项）─────────────────────
    { title: QT_TRANSLATE_NOOP("SettingsSearch", "模型行为"), category: "general", keywords: "步数 预算 超时 maxSteps timeout 任务上限" },
    { title: QT_TRANSLATE_NOOP("SettingsSearch", "自定义系统提示词"), category: "general", keywords: "系统提示词 prompt override 提示词" },
    { title: QT_TRANSLATE_NOOP("SettingsSearch", "日志"), category: "general", keywords: "日志 级别 目录 大小 保留 log" },
    { title: QT_TRANSLATE_NOOP("SettingsSearch", "知识库嵌入"), category: "general", keywords: "嵌入 embedding ollama 向量 嵌入模型 服务地址" },

    // ── 安全 ─────────────────────────────────────────────
    { title: QT_TRANSLATE_NOOP("SettingsSearch", "文件读取沙箱"), category: "security", keywords: "读取 沙箱 全盘 磁盘 read 策略" },
    { title: QT_TRANSLATE_NOOP("SettingsSearch", "允许写入当前项目目录"), category: "security", keywords: "写入 项目 write project" },
    { title: QT_TRANSLATE_NOOP("SettingsSearch", "黑名单"), category: "security", keywords: "黑名单 拒绝 deny 敏感 追加" },
    { title: QT_TRANSLATE_NOOP("SettingsSearch", "隐私门"), category: "security", keywords: "仅本地 本地模式 local 隐私 offline" },

    // ── MCP ──────────────────────────────────────────────
    { title: QT_TRANSLATE_NOOP("SettingsSearch", "MCP 服务器"), category: "mcp", keywords: "mcp 服务器 添加 命令 url headers 环境变量 连接 重连 启用" },

    // ── 工具管理 ─────────────────────────────────────────
    { title: QT_TRANSLATE_NOOP("SettingsSearch", "工具管理"), category: "tools", keywords: "工具 禁用 启用 shell read_file write_file 需确认" },
    { title: QT_TRANSLATE_NOOP("SettingsSearch", "联网搜索"), category: "tools", keywords: "联网 搜索 web_search tavily searxng api key 密钥 搜索服务" },
    { title: QT_TRANSLATE_NOOP("SettingsSearch", "默认权限模式"), category: "tools", keywords: "权限 模式 确认 自动编辑 完全访问 permission" },

    // ── 记忆管理 ─────────────────────────────────────────
    { title: QT_TRANSLATE_NOOP("SettingsSearch", "长期记忆"), category: "memory", keywords: "记忆 episodic 摘要 长期 检索" },
    { title: QT_TRANSLATE_NOOP("SettingsSearch", "数据保留"), category: "memory", keywords: "保留 审计 清理 天数 retention token" },

    // ── 用量统计 ─────────────────────────────────────────
    { title: QT_TRANSLATE_NOOP("SettingsSearch", "配额设置"), category: "usage", keywords: "配额 token 费用 每日 每月 超限 quota" },
    { title: QT_TRANSLATE_NOOP("SettingsSearch", "用量统计"), category: "usage", keywords: "用量 统计 消耗 成本 成本明细" },

    // ── 活动记录 ─────────────────────────────────────────
    { title: QT_TRANSLATE_NOOP("SettingsSearch", "活动记录"), category: "activity", keywords: "活动 轨迹 usage 操作 记录 audit trail 时间线" },

    // ── 设置与备份 ───────────────────────────────────────
    { title: QT_TRANSLATE_NOOP("SettingsSearch", "设置导出与导入"), category: "backup", keywords: "导出 导入 备份 json 设置" },
    { title: QT_TRANSLATE_NOOP("SettingsSearch", "数据库备份"), category: "backup", keywords: "数据库 备份 恢复 快照 sqlite 预览" }
];

function search(query, registryEntries, categoryOrder, categoryNames) {
    var q = String(query || "").trim().toLowerCase();
    if (q.length === 0) return [];

    var order = categoryOrder || {};
    var names = categoryNames || {};
    var results = [];
    var seen = {};

    var consider = function (entry) {
        var categoryName = entry.categoryName || names[entry.category]
            || entry.category;
        var haystack = (entry.title + " " + (entry.keywords || "") + " "
                        + categoryName).toLowerCase();
        if (haystack.indexOf(q) < 0) return;
        var dedupeKey = entry.category + "/" + entry.title;
        if (seen[dedupeKey]) return;
        seen[dedupeKey] = true;
        results.push({
            title: entry.title,
            category: entry.category,
            categoryName: categoryName,
            order: order[entry.category] !== undefined ? order[entry.category] : 99
        });
    };

    // Registry-driven entries first (declarative preferences), then the
    // hand-maintained table; dedup + category-order sort normalise both.
    for (var i = 0; i < registryEntries.length; ++i) consider(registryEntries[i]);
    for (var j = 0; j < handIndex.length; ++j) consider(handIndex[j]);

    results.sort(function (a, b) {
        if (a.order !== b.order) return a.order - b.order;
        return a.title < b.title ? -1 : (a.title > b.title ? 1 : 0);
    });
    return results;
}
