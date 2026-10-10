#pragma once

#include <QJsonObject>
#include <QString>
#include <atomic>
#include <memory>

namespace NexusAI {

// Tool safety level - determines execution policy
enum class ToolLevel {
    Safe,               // Read-only, execute immediately
    RequiresConfirm,    // Modifies state, needs user approval
    Dangerous           // Blocked by sandbox, never exposed to LLM
};

// Tool definition / schema
struct ToolDefinition {
    QString name;
    QString description;
    QJsonObject parameters;  // JSON Schema for arguments
    ToolLevel level{ToolLevel::Safe};
    // 会话权限模式（PermissionMode::AutoEdit）下可自动放行的工具：文件
    // 编辑类与搜索/抓取类声明为 true；shell_exec、MCP 工具与一切未声明者
    // 在任何自动模式下仍逐次确认。Dangerous 不受影响。
    bool autoEditSafe{false};
};

// Tool execution result
struct ToolResult {
    bool success{false};
    QString output;
    QString error;

    static ToolResult ok(const QString& out) {
        return {true, out, {}};
    }
    static ToolResult fail(const QString& err) {
        return {false, {}, err};
    }
};

// Tool call info from LLM
struct ToolCallInfo {
    QString id;
    QString name;
    QJsonObject arguments;
};

// Abstract tool interface
class ITool {
public:
    using Cancellation = std::shared_ptr<std::atomic<bool>>;
    virtual ~ITool() = default;
    virtual ToolDefinition definition() const = 0;
    virtual ToolResult execute(const QJsonObject& args) = 0;
    // Created before dispatch, so cancellation survives queueing and is
    // scoped to one invocation even when multiple tabs share the tool.
    virtual ToolResult execute(const QJsonObject& args, const Cancellation& cancelled) {
        if (cancelled && cancelled->load()) return ToolResult::fail("Tool cancelled");
        return execute(args);
    }
    // Batch 13 (SEC-1301): cooperative cancellation. cancel() is invoked on
    // the GUI thread while execute() runs on a QtConcurrent pool thread, so
    // implementations may only flip atomic flags (or terminate child
    // handles) - never block, never touch execute()'s non-atomic state from
    // this thread. The pool thread running execute() observes the flag
    // between wait slices and performs its own teardown. Default no-op:
    // second-scale tools don't need it.
    virtual void cancel() {}
    virtual void cancel(const Cancellation& cancelled) {
        if (cancelled) cancelled->store(true);
        cancel();
    }
};

} // namespace NexusAI
