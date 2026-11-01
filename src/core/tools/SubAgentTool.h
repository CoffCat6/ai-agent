#pragma once

#include "ITool.h"
#include "core/agent/SubAgentRunner.h"

#include <QMutex>
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

    // Creates one parentless IAIService per run, already configured for the
    // active profile; ownership passes to the run, which deletes it on the
    // executing thread. Returns null when no model profile is usable — the
    // run reports that as a readable failure. main.cpp wires the real
    // factory; tests inject fakes.
    using ServiceFactory = std::function<IAIService*()>;

    void setServiceFactory(ServiceFactory factory) { m_factory = std::move(factory); }

    // GUI-thread refresh of the read-only tool snapshot (ToolRegistry has no
    // internal locking, so pool threads must never query it directly).
    // Connect ToolRegistry::toolsChanged and settings changes here.
    void refreshCatalog(bool localOnly);

    // Runtime knobs (any thread; cheap atomic/lock writes).
    void setMaxParallel(int n);       // clamped to [1, 8]
    void setMaxSteps(int steps) { m_maxSteps.store(steps > 0 ? steps : 1); }
    void setTimeoutMs(qint64 ms) { m_timeoutMs.store(ms > 0 ? ms : 0); }

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
    // Counting gate with cancel-aware queueing.
    bool acquireSlot(const Cancellation& cancelled);
    void releaseSlot();
    QVector<SubAgentToolEntry> catalogSnapshot() const;

    ServiceFactory m_factory;
    mutable QMutex m_catalogMutex;
    QVector<SubAgentToolEntry> m_catalog;   // guarded by m_catalogMutex

    mutable std::mutex m_gateMutex;
    std::condition_variable m_gateCv;
    int m_activeRuns{0};                    // guarded by m_gateMutex
    int m_maxParallel{2};                   // guarded by m_gateMutex

    std::atomic<int> m_maxSteps{12};
    std::atomic<qint64> m_timeoutMs{300000};
};

} // namespace NexusAI
