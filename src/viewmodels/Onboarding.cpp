#include "Onboarding.h"

#include "PreferenceStore.h"
#include "SettingsService.h"

namespace NexusAI {
namespace {

constexpr auto kOnboardedKey = "general.onboarded";

} // namespace

Onboarding* Onboarding::s_instance = nullptr;

Onboarding* Onboarding::instance() {
    if (!s_instance) s_instance = new Onboarding(nullptr);
    return s_instance;
}

Onboarding* Onboarding::create(QQmlEngine*, QJSEngine*) {
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}

Onboarding::Onboarding(QObject* parent)
    : QObject(parent) {
}

bool Onboarding::shouldShow() const {
    if (isComplete()) return false;
    // 已有任何模型配置就算"用过"：标记可能因旧版本/导入缺失，但配置在，
    // 弹一个"配置你的模型"向导只会打断用户。
    return SettingsService::instance()->profiles().isEmpty();
}

bool Onboarding::complete() {
    if (isComplete()) return true;
    return PreferenceStore::instance()->save(QString::fromLatin1(kOnboardedKey), true);
}

bool Onboarding::isComplete() const {
    return PreferenceStore::instance()->load(QString::fromLatin1(kOnboardedKey)).toBool();
}

} // namespace NexusAI
