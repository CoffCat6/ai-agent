#pragma once

#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <QThreadStorage>
#include <QVector>
#include <memory>

#include "core/agent/SubAgentRunner.h"

namespace NexusAI {

// docs/35 P4.4: 一条已登记、等待主代理应用的提案。注册表**仅进程内**
// （已定）：重启后 apply_proposal 的 id 全部失效，回退到报告/证据工件里
// 的文本（模型可重新派生或手工 edit_file）。
struct StoredProposal {
    QString id;          // "<spawn trace node id>#<n>"，报告里的 Proposal ids
    QString path;        // 绝对路径（提案生成时校验过项目根）
    QString rationale;
    QVector<SubAgentProposalEdit> edits;
    bool applied{false};
};

// 会话级提案表（AIAgent 持有）。GUI 线程写（finishToolExecution），
// generatePreview 在 GUI 线程读，execute 在池线程读+标记 applied，
// 因此用互斥保护；条目少、操作 O(1)，锁开销可忽略。
class ProposalStore {
public:
    void put(const StoredProposal& proposal) {
        QMutexLocker lock(&m_mutex);
        m_items.insert(proposal.id, proposal);
    }

    bool get(const QString& id, StoredProposal* out) const {
        QMutexLocker lock(&m_mutex);
        const auto it = m_items.constFind(id);
        if (it == m_items.cend()) return false;
        if (out) *out = it.value();
        return true;
    }

    bool markApplied(const QString& id) {
        QMutexLocker lock(&m_mutex);
        const auto it = m_items.find(id);
        if (it == m_items.end()) return false;
        it->applied = true;
        return true;
    }

    void clear() {
        QMutexLocker lock(&m_mutex);
        m_items.clear();
    }

    int size() const {
        QMutexLocker lock(&m_mutex);
        return m_items.size();
    }

private:
    mutable QMutex m_mutex;
    QHash<QString, StoredProposal> m_items;
};

// docs/35 P4.4: 与 SubAgentProgressRouter 同型的线程局部路由。AIAgent 在
// generatePreview（GUI 线程）与派发 execute（池线程）的 lambda 里安装，
// ApplyProposalTool 从当前线程取表；未安装（例如子代理内部或测试直接
// 调用工具）时返回空 = 没有可应用的提案。
class ProposalStoreRouter {
public:
    explicit ProposalStoreRouter(std::shared_ptr<ProposalStore> store) {
        s_store.localData() = std::move(store);
    }
    ~ProposalStoreRouter() { s_store.localData().reset(); }
    ProposalStoreRouter(const ProposalStoreRouter&) = delete;
    ProposalStoreRouter& operator=(const ProposalStoreRouter&) = delete;

    static std::shared_ptr<ProposalStore> current() {
        return s_store.localData();
    }

private:
    static inline QThreadStorage<std::shared_ptr<ProposalStore>> s_store;
};

} // namespace NexusAI