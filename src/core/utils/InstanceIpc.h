#pragma once

#include <functional>

#include <QString>

class QLocalServer;

namespace NexusAI {

// docs/20 §4.2（D7 单实例）二期：单实例 IPC。
//
// 单实例锁（main.cpp 的 QLockFile）只保证"第二个实例不产生双写"，此前
// 第二个实例是静默退出——用户点击桌面图标/任务栏 expecting 唤起窗口，却
// 什么都没发生。这里补上唤起链：第二个实例连本地 socket 发一行 "show"，
// 首实例收到后前置主窗。协议是行文本，未来可扩展 "open <path>" 等动词。
//
// 两端都是纯进程间设施（QLocalServer/QLocalSocket，core 层零 UI 依赖）：
// 唤起动作由调用方以回调注入，本类不认识窗口。锁本身仍由 main.cpp 的
// QLockFile 负责——锁管"谁是主实例"，这里管"主实例如何被找到"。
class InstanceIpc {
public:
    // 单实例锁与 IPC socket 共用的名字（锁是 <name>.lock，socket 是 name）。
    static constexpr const char* kName = "NexusAI.instance";

    InstanceIpc() = default;
    ~InstanceIpc();

    InstanceIpc(const InstanceIpc&) = delete;
    InstanceIpc& operator=(const InstanceIpc&) = delete;

    // 主实例侧：监听 IPC。返回 false（端口被占且不可复用——通常说明锁与
    // socket 状态不一致）。onActivate 在收到唤起请求的连接时触发（连接
    // 随即关闭，协议一问一答，无状态）。
    bool startServer(const QString& name, std::function<void()> onActivate);

    // 第二实例侧：向主实例发唤起请求。返回 false 表示连不上（主实例
    // 存在但 IPC 未就绪，例如旧版本在跑）——调用方仍应直接退出，不双起。
    static bool notifyRunningInstance(const QString& name);

private:
    QLocalServer* m_server = nullptr;
};

} // namespace NexusAI
