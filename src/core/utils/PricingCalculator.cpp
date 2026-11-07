#include "PricingCalculator.h"

#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>

namespace NexusAI {

namespace {
// 1M token 基准（价格单位为分/1M token）。
constexpr qint64 kPerMillion = 1000000;
} // namespace

qint64 PricingCalculator::calculateCost(const QString& model,
                                        int inputTokens, int outputTokens,
                                        int cacheHitTokens,
                                        int inputPriceCents, int outputPriceCents,
                                        int cacheHitPriceCents) {
    Q_UNUSED(model);
    // 缓存命中部分从输入中剥离，单独按缓存单价计费；剩余输入按输入单价。
    // cacheHitTokens 语义上是 inputTokens 的子集（OpenAI cached_tokens 已含
    // 在 prompt_tokens 内，Anthropic cache_read 也已并入 input）。上层若传
    // 入大于 input 的脏数据，必须先钳到 input：否则 billableInput 只被钳到
    // 0，而 cacheHit 仍按完整值计费，缓存部分会重复/虚高。
    const qint64 cacheHit = static_cast<qint64>(
        qBound(0, cacheHitTokens, qMax(0, inputTokens)));
    const qint64 billableInput =
        static_cast<qint64>(qMax(0, inputTokens)) - cacheHit;
    const qint64 output        = static_cast<qint64>(qMax(0, outputTokens));
    // qint64 防溢出：1M token * 7500 分 = 7.5e9，已超 int32 上限。
    const qint64 inputCost  = billableInput * inputPriceCents;
    const qint64 cacheCost  = cacheHit      * cacheHitPriceCents;
    const qint64 outputCost = output        * outputPriceCents;
    const qint64 total = inputCost + cacheCost + outputCost;
    // ceil(total / 1M)，整数向上取整（total >= 0）。
    return (total + kPerMillion - 1) / kPerMillion;
}

void PricingCalculator::getPricing(const QString& model,
                                   int& inputPrice, int& outputPrice,
                                   int& cacheHitPrice) {
    QMutexLocker lock(&mutex());
    const auto& c = cache();
    auto it = c.constFind(model);
    if (it != c.constEnd()) {
        inputPrice    = it->inputCents;
        outputPrice   = it->outputCents;
        cacheHitPrice = it->cacheHitCents;
    } else {
        // 未知模型定价 0（docs/16 §6.2）。
        inputPrice    = 0;
        outputPrice   = 0;
        cacheHitPrice = 0;
    }
}

void PricingCalculator::refreshCache(const QSqlDatabase& db) {
    if (!db.isValid() || !db.isOpen()) return;
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral(
            "SELECT model, input_price_cents, output_price_cents, "
            "cache_hit_price_cents FROM model_pricing"))) {
        qWarning() << "PricingCalculator: failed to load model_pricing:"
                   << query.lastError().text();
        return;
    }
    QMutexLocker lock(&mutex());
    auto& c = cache();
    c.clear();
    while (query.next()) {
        Pricing p;
        p.inputCents    = query.value(1).toInt();
        p.outputCents   = query.value(2).toInt();
        p.cacheHitCents = query.value(3).toInt();
        c.insert(query.value(0).toString(), p);
    }
}

QHash<QString, PricingCalculator::Pricing>& PricingCalculator::cache() {
    static QHash<QString, Pricing> c;
    return c;
}

QMutex& PricingCalculator::mutex() {
    static QMutex m;
    return m;
}

} // namespace NexusAI