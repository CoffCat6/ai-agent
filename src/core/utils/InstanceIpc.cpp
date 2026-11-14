#include "InstanceIpc.h"

#include <QLocalServer>
#include <QLocalSocket>

namespace NexusAI {

namespace {
// 唤起动词（协议 v1，一行文本，\n 结尾）。
constexpr const char* kShowVerb = "show\n";
// 第二实例侧的等待上限：本地 socket 是毫秒级，300ms 已远超正常路径，
// 超时说明主实例假活（锁残留），调用方按"连不上"处理。
constexpr int kConnectTimeoutMs = 300;
} // namespace

InstanceIpc::~InstanceIpc() {
    // 关闭监听并断开所有挂着的连接；socket 文件由 removeServer 在下次
    // startServer 时清理（退出路径不必碰文件系统）。
    delete m_server;
}

bool InstanceIpc::startServer(const QString& name, std::function<void()> onActivate) {
    // 崩溃/被 kill 残留的 socket 文件会让 listen 失败：QLockFile 能识别
    // 陈旧锁，但 socket 文件需要这里显式清（名字是本应用专属，误删风险
    // 只存在于另一个实例正在 listen 的瞬间——那种情况下锁也拿不到，走
    // 不到这条路径）。
    QLocalServer::removeServer(name);
    m_server = new QLocalServer();
    if (!m_server->listen(name)) {
        delete m_server;
        m_server = nullptr;
        return false;
    }
    QObject::connect(m_server, &QLocalServer::newConnection,
                     m_server, [this, onActivate = std::move(onActivate)]() {
        QLocalSocket* conn = m_server->nextPendingConnection();
        if (!conn) return;
        // 读完请求再触发回调：协议一问一答，客户端写完即断，readAll
        // 兜住分片。回调（唤起窗口）同步执行——本地连接回调极轻。
        QObject::connect(conn, &QLocalSocket::disconnected, conn,
            [conn, onActivate = std::move(onActivate)]() {
                if (QString::fromUtf8(conn->readAll()).contains(
                        QStringLiteral("show")))
                    onActivate();
                conn->deleteLater();
            });
    });
    return true;
}

bool InstanceIpc::notifyRunningInstance(const QString& name) {
    QLocalSocket socket;
    socket.connectToServer(name);
    if (!socket.waitForConnected(kConnectTimeoutMs)) return false;
    socket.write(kShowVerb);
    if (!socket.waitForBytesWritten(kConnectTimeoutMs)) return false;
    socket.disconnectFromServer();
    return true;
}

} // namespace NexusAI
