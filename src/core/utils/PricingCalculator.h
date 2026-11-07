#pragma once

#include <QHash>
#include <QMutex>
#include <QSqlDatabase>
#include <QString>

namespace NexusAI {

// docs/16 §6: 模型定价与单次请求成本核算。价格单位为"分/1M token"，
// 全程整数运算避免浮点累计误差。本地模型（Ollama）与未知模型定价为 0
// （docs/16 §4.2/§6.2）—— 仍统计 token 用于上下文管理，但不产生费用。
//
// 与 TokenEstimator 同目录（core/utils）：估算产出 token 数，本类把 token
// 数折算为费用（分）。
class PricingCalculator {
public:
    // docs/16 §6.1 修正公式（单位：分）：
    //   ceil((inputTokens - cacheHitTokens) * inputPriceCents     / 1_000_000
    //      +  cacheHitTokens           * cacheHitPriceCents  / 1_000_000
    //      +  outputTokens             * outputPriceCents    / 1_000_000)
    // 缓存命中部分从 inputTokens 中剥离，单独按缓存单价计费（缓存单价通常
    // 0.1-0.5x 输入）；剩余输入按输入单价。cacheHitTokens 是 inputTokens 的
    // 子集：实现内部先钳制 cacheHitTokens = min(max(cacheHitTokens,0),
    // max(inputTokens,0))，保证 billableInput 不为负、缓存部分不被重复计费。
    // model 仅用于扩展/日志，不参与公式——调用方应先 getPricing(model,...)
    // 取价再调本函数。
    static qint64 calculateCost(const QString& model,
                                int inputTokens, int outputTokens, int cacheHitTokens,
                                int inputPriceCents, int outputPriceCents,
                                int cacheHitPriceCents);

    // 从 model_pricing 表查定价（分/1M token）。命中写回三个出参；未命中
    // 返回 (0,0,0)：本地模型与未知模型均定价 0（docs/16 §6.2）。
    static void getPricing(const QString& model,
                           int& inputPrice, int& outputPrice, int& cacheHitPrice);

    // 把 model_pricing 表全量载入进程级缓存。SqliteMemory 在 initDatabase
    // 末尾调用一次；getPricing 只读缓存，避免每次计费都查库。用户在设置中
    // 改定价后应再次调用以刷新。
    static void refreshCache(const QSqlDatabase& db);

private:
    struct Pricing {
        int inputCents = 0;
        int outputCents = 0;
        int cacheHitCents = 0;
    };
    // 函数局部静态：跨翻译单元唯一实例，且避免静态初始化顺序问题。
    static QHash<QString, Pricing>& cache();
    static QMutex& mutex();
};

} // namespace NexusAI