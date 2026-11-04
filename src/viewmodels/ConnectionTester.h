#pragma once

#include <QQmlEngine>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include "infrastructure/llm/IAIService.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace NexusAI {

// Fires a minimal chat completion against a provider configuration from the
// settings dialog. Creates a throwaway IAIService so the live agent pipeline
// is never touched. Exactly one testFinished() is emitted per test() call,
// after which the temporary service is destroyed.
//
// 同时负责「从端点拉取模型列表」：设置页不该逼用户手打模型名，拼错一个字母
// 要等到聊天失败才发现。拉取是独立的 GET 请求（各协议的列举端点不同），
// 不复用 chat 服务，避免把一次列举失败误报成「连接不可用」。
class ConnectionTester : public QObject {
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(ConnectionTester)
public:
    // No default constructor on purpose (see AgentViewModel: the engine
    // would prefer Constructor over the create() factory).
    explicit ConnectionTester(QObject* parent);

    // QML singleton plumbing (same pattern as AgentViewModel)
    static ConnectionTester* instance();
    static ConnectionTester* create(QQmlEngine*, QJSEngine*);

    // 生产入口（B1-1）：QML 只传 profileId，密钥在 C++ 内部从
    // SettingsService 的 WithSecrets 出口解析，绝不再经过 QML。
    // model 为空时用 profile 自己的当前模型。
    Q_INVOKABLE void testProfile(const QString& profileId, const QString& model);
    Q_INVOKABLE void fetchModelsForProfile(const QString& profileId);

    // 兼容旧 API（连接测试单元测试与第三方调用仍在使用）。生产 QML 不再
    // 走这两个入口：它们要求调用方持有明文密钥，本身就是泄露面。
    // 按线协议（profile 的 apiProtocol）建一次性服务打一发最小请求。
    Q_INVOKABLE void test(const QString& apiProtocol, const QString& baseUrl,
                          const QString& apiKey, const QString& model);

    // 拉取端点声明的模型列表。协议决定列举端点与鉴权头：
    //   openai-chat / openai-responses → {base}/models（Bearer）
    //   anthropic-messages             → {base}/v1/models（x-api-key + version）
    //   gemini-generate                → {base}/v1beta/models?key=…
    //   ollama-chat                    → {base}/api/tags
    // Gemini 的 key 走查询串，其余走请求头；空 key 不发鉴权（本地服务）。
    Q_INVOKABLE void fetchModels(const QString& apiProtocol,
                                 const QString& baseUrl,
                                 const QString& apiKey);

    // 纯函数，便于单测：按 id 从给定 profile 列表里取一项；profileId 空则
    // 回退 active，非空但不存在则返回空 map（不静默拿别的配置去测）。
    // 单测用 profilesWithSecrets() 传入以锁定「按 id 取真实密钥」的契约。
    static QVariantMap selectConnectionProfile(const QVariantList& profiles,
                                               const QVariantMap& activeProfile,
                                               const QString& profileId);

    // 纯函数，便于单测：只负责「协议 → 列举端点」，不碰网络。
    static QUrl modelsUrl(const QString& apiProtocol, const QString& baseUrl);
    // 纯函数：把响应体解析成模型名列表（去重、去空、保持端点顺序）。
    // 失败时返回空列表并写 *error（解析错误 / 没有任何模型）。
    static QStringList parseModelList(const QString& apiProtocol,
                                      const QByteArray& body,
                                      QString* error);

signals:
    void testFinished(bool ok, const QString& message);
    // 每次 fetchModels 恰好发一次；models 为空表示失败（消息在 message 里）。
    void modelsFetched(bool ok, const QStringList& models, const QString& message);

private:
    void finish(bool ok, const QString& message);
    void finishModels(bool ok, const QStringList& models, const QString& message);

    QPointer<IAIService> m_service;
    QTimer m_timeout{this};
    bool m_done{true};

    QNetworkAccessManager* m_net{nullptr};
    QPointer<QNetworkReply> m_modelsReply;
    QTimer m_modelsTimeout{this};
    bool m_modelsBusy{false};

    static ConnectionTester* s_instance;
};

} // namespace NexusAI
