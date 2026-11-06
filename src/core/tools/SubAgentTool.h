#pragma once

#include "ITool.h"
#include "core/agent/SubAgentRunner.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMutex>
#include <QVariant>
#include <QVector>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>

namespace NexusAI {

class IAIService;

// P1 (docs/32 §3.1): the primary agent's entry point into sub-agents.
// Spawning is itself read-only (the sub-agent's tool set is filtered to
// Safe+readOnly upstream), so the tool is Safe level with readOnly=true —
// it auto-executes and stays available in PlanOnly sessions.
//
// Concurrency: several spawn_agent calls in one assistant round each get
// their own QtConcurrent pool thread; a counting gate caps how many run at
// once (overflow queues instead of failing). The gate, the config knobs and
// the tool catalog are written from the GUI thread and read from pool
// threads — atomics / a mutex guard everything shared.
//
// Registration is opt-in: main.cpp registers/unregisters this tool when
// agent/subagentEnabled flips, so a disabled feature leaves no trace in the
// model's schema.
class SubAgentTool : public ITool {
public:
    static constexpr const char* kName = "spawn_agent";
    // docs/43 P3-18: 排队等待的墙钟上限（与 subagent_wait 的
    // kMaxWaitMs 对齐）。上游网络挂起时同步 join 不再无限期滞留：
    // 超时返回可读错误，后台任务归类 stopCode=slot_timeout。测试
    // 用 setMaxSlotWaitMs 调小注入。
    static constexpr qint64 kMaxSlotWaitMs = 10 * 60 * 1000;

    // Creates one parentless IAIService per run. docs/35 P4.6: the role may
    // select a model profile and an effort override (both empty = the active
    // profile / its default). Ownership passes to the run, which deletes it
    // on the executing thread. Returns null when no profile is usable — the
    // run reports that as a readable failure. main.cpp wires the real
    // factory; tests inject fakes.
    using ServiceFactory =
        std::function<IAIService*(const QString& profileId,
                                  const QString& effort)>;

    void setServiceFactory(ServiceFactory factory) {
        m_factory = std::move(factory);
    }
    // Legacy zero-argument factory (tests and simple hosts): ignores the
    // role's model selection and always uses whatever the fake is configured
    // with. Kept as an overload so existing call sites stay source-compatible.
    void setServiceFactory(const std::function<IAIService*()>& factory) {
        m_factory = [factory](const QString&, const QString&) {
            return factory();
        };
    }
    // docs/35 P4.6: per-profile context window for the runner's budget.
    // main.cpp resolves it from the same profile snapshots as the factory;
    // when unset, the fixed setContextLimit() value applies.
    using ContextLimitResolver = std::function<int(const QString& profileId)>;
    void setContextLimitResolver(ContextLimitResolver resolver) {
        m_contextLimitResolver = std::move(resolver);
    }

    // GUI-thread refresh of the read-only tool snapshot (ToolRegistry has no
    // internal locking, so pool threads must never query it directly).
    // Connect ToolRegistry::toolsChanged and settings changes here.
    void refreshCatalog(bool localOnly);

    // docs/33 E2: built-in role roster; user-defined profiles are appended
    // via setUserRoles() (a user role with a preset's name overrides it).
    static QVector<SubAgentRole> presetRoles();
    // Parses the agent/subagentRoles settings blob ({name, prompt, tools[]});
    // entries without a name or prompt are dropped and duplicate names (case-
    // insensitive) keep only their first occurrence.
    static QVector<SubAgentRole> rolesFromVariant(const QVariantList& raw);
    // GUI-thread install of the parsed user roles; executed runs read a
    // mutex-guarded snapshot (pool threads must not touch settings).
    void setUserRoles(QVector<SubAgentRole> roles);
    // 内置角色（explore/research/review/coder）的默认后台开关。开启后
    // rolesSnapshot() 给预设角色统一打上 background=true；用户同名档案
    // 仍然遮蔽预设，其自身设置优先。
    void setPresetBackground(bool enabled) { m_presetBackground.store(enabled); }

    // Runtime knobs (any thread; cheap atomic/lock writes).
    void setMaxParallel(int n);       // clamped to [1, 8]
    // 步数/超时不再是用户可配置限制（2026-11-01，与主任务一致）：默认 0 =
    // 不限（Runner 内部留 kSubStepCap 防失控）。setter 保留给测试做确定性
    // 预算用例。
    void setMaxSteps(int steps) { m_maxSteps.store(steps > 0 ? steps : 0); }
    void setTimeoutMs(qint64 ms) { m_timeoutMs.store(ms > 0 ? ms : 0); }
    // docs/35 P4.1: 子代理所在 profile 的上下文窗口（token）；0 = 未知/不限。
    // 由 main.cpp 在 applySettings 里随 profile 快照一起刷新。
    void setContextLimit(int tokens) {
        m_contextLimit.store(tokens > 0 ? tokens : 0);
    }

    // docs/35 P4.3: render a finished run into the primary-history tool text
    // and its structured metadata. Shared by spawn_agent's synchronous path
    // and subagent_wait's background retrieval, so both produce identical
    // report shapes (evidence path, todo echo, proposal index/full data).
    static QString reportOutput(const SubAgentReport& report,
                                const QString& todoId = {});
    static QJsonArray proposalIndex(const SubAgentReport& report);
    static QJsonArray proposalData(const SubAgentReport& report);
    // docs/43 O2: machine-readable report (usage, stopCode, attempts,
    // findings, ...) carried in ToolResult.metadata["subagentReport"] by both
    // the synchronous spawn path and subagent_wait.
    static QJsonObject reportJson(const SubAgentReport& report);
    // docs/43 O3: live slot pressure for the model's scheduling decisions
    // ("slots: 2/3 running, 1 queued"). Shared by the synchronous/background
    // path (ack) and subagent_status.
    QString gateSummary() const;

    ToolDefinition definition() const override;
    // ITool's pure virtual; the dispatch path always uses the cancellation
    // overload, this one exists so the type is concrete (no cancellation).
    ToolResult execute(const QJsonObject& args) override {
        return execute(args, {});
    }
    ToolResult execute(const QJsonObject& args, const Cancellation& cancelled) override;
    // cancel(cancelled): inherited default flips the shared flag; the runner
    // polls it (kAbortPollMs) and unwinds.

private:
    // Counting gate with cancel-aware queueing. onQueued fires once per
    // kSlotWaitSliceMs while the call has no slot (docs/35 P4.5).
    // docs/43 P3-18: the queue is wall-clock bounded (kMaxSlotWaitMs);
    // TimedOut tells the caller the wait itself gave up (distinct from a
    // user cancellation).
    enum class SlotResult { Acquired, Cancelled, TimedOut };
    SlotResult acquireSlot(const Cancellation& cancelled,
                           const std::function<void()>& onQueued);
    void releaseSlot();
    QVector<SubAgentToolEntry> catalogSnapshot() const;
    // presets + user roles, user entries first (override by name).
    QVector<SubAgentRole> rolesSnapshot() const;
    // Resolves the "role"/"hint" argument against rolesSnapshot(); an empty
    // roleName picks the default (explore). Returns false for an unknown name.
    bool resolveRole(const QString& roleName, SubAgentRole* out) const;

    ServiceFactory m_factory;
    ContextLimitResolver m_contextLimitResolver;
    mutable QMutex m_catalogMutex;
    QVector<SubAgentToolEntry> m_catalog;   // guarded by m_catalogMutex

    mutable QMutex m_rolesMutex;
    QVector<SubAgentRole> m_userRoles;      // guarded by m_rolesMutex

    mutable std::mutex m_gateMutex;
    std::condition_variable m_gateCv;
    int m_activeRuns{0};                    // guarded by m_gateMutex
    int m_queuedRuns{0};                    // guarded by m_gateMutex
    int m_maxParallel{2};                   // guarded by m_gateMutex

    std::atomic<int> m_maxSteps{0};         // 0 = unlimited (kSubStepCap)
    std::atomic<qint64> m_timeoutMs{0};     // 0 = no wall-clock budget
    std::atomic<int> m_contextLimit{0};     // 0 = no compression (docs/35 P4.1)
    std::atomic<bool> m_presetBackground{false};  // built-in role default
    // docs/43 P3-18: slot-queue wall-clock cap (default kMaxSlotWaitMs).
    std::atomic<qint64> m_maxSlotWaitMs{kMaxSlotWaitMs};

public:
    // Test/host injection for the slot-queue deadline (GUI thread).
    void setMaxSlotWaitMs(qint64 ms) { m_maxSlotWaitMs.store(ms); }
};

} // namespace NexusAI
