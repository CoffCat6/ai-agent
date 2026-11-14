// docs/20 §4.2 二期: InstanceIpc — single-instance raise channel.
// 主实例 startServer 监听；第二实例 notifyRunningInstance 发 "show"，
// 回调在主实例侧触发。协议一问一答，测试进程内即可闭环两端。
#include "core/utils/InstanceIpc.h"

#include <QtTest>

using NexusAI::InstanceIpc;

class TestInstanceIpc : public QObject {
    Q_OBJECT

private slots:
    // 主流程：server 监听 → notify → 回调触发。newConnection 信号经事件
    // 循环派发，断言用 QTRY（内部 processEvents，本地连接毫秒级到位）。
    void notifyRaisesActivateCallback() {
        InstanceIpc server;
        int activated = 0;
        const QString name = QStringLiteral("NexusAI.test.ipc.main");
        QVERIFY(server.startServer(name, [&activated]() { ++activated; }));

        QVERIFY(InstanceIpc::notifyRunningInstance(name));
        QTRY_COMPARE(activated, 1);
        // 可以连续唤起（每次第二实例启动都会发一次）。
        QVERIFY(InstanceIpc::notifyRunningInstance(name));
        QTRY_COMPARE(activated, 2);
    }

    // 无人监听：notify 返回 false，不阻塞也不抛。
    void notifyWithoutServerReturnsFalse() {
        QVERIFY(!InstanceIpc::notifyRunningInstance(
            QStringLiteral("NexusAI.test.ipc.nobody")));
    }

    // 陈旧 socket 文件（进程被 kill 残留）：startServer 先 removeServer
    // 再 listen，重启监听必须成功——对应"应用崩溃后重启"的路径。
    void staleSocketFileIsClearedOnRestart() {
        const QString name = QStringLiteral("NexusAI.test.ipc.stale");
        {
            InstanceIpc first;
            QVERIFY(first.startServer(name, []() {}));
            // first 析构：QLocalServer 关闭监听，socket 文件残留。
        }
        InstanceIpc second;
        QVERIFY(second.startServer(name, []() {}));
        QVERIFY(InstanceIpc::notifyRunningInstance(name));
    }
};

QTEST_GUILESS_MAIN(TestInstanceIpc)
#include "test_instance_ipc.moc"
