#pragma once

#include <QAtomicInt>

namespace NexusAI {

// 进程级“嵌入请求退出中断”标志（任务 #22，退出卡住修复 A）。
//
// 背景：三个后台嵌入 worker（启动知识库恢复、provider/model 切换后的
// 全库重嵌入、文档导入）跑在被丢弃的 QtConcurrent future 上，进程退出
// 时没有任何代码取消它们；worker 在全局线程池线程上用 QEventLoop 同步
// 等待 HTTP 响应（外部服务批量请求的传输超时长达 300s），main() 返回后
// 的静态析构阶段线程池 waitForDone() 无限等待 → 窗口消失而进程挂在后台。
//
// 方案：GUI 线程在 aboutToQuit 里置位本标志（单向闸门——进程已在退
// 出，生产路径不复位）；两个嵌入后端的 QEventLoop 等待循环用同线程
// QTimer 以 200ms 轮询本标志，置位则 abort 在途请求、立即返回空，重试
// 与后续批也不再发起；main.cpp 的两个后台 for 循环同样轮询收手，避免
// 退出后继续白算 pseudo 降级向量。
//
// 线程安全：QAtomicInt 的 release/acquire 保证置位（GUI 线程）与查询
// （worker 线程）跨线程可见；标志为平凡类型，静态析构期访问无副作用。
// inline 函数 + 函数内 static 跨编译单元共享同一实例（ODR），并规避
// 静态初始化顺序问题（magic static 首次调用才构造，线程安全）。

namespace embedShutdownDetail {

// 实现细节：唯一存储。inline 函数的函数内 static 在所有编译单元中
// 是同一个对象——本标志需要跨编译单元共享，这正是不能放匿名命名
// 空间（每个编译单元一份副本）的原因。
inline QAtomicInt& flag() {
    static QAtomicInt instance{0};
    return instance;
}

} // namespace embedShutdownDetail

// 置位退出标志：aboutToQuit 时调用一次（release 语义）。
inline void requestEmbeddingShutdown() {
    embedShutdownDetail::flag().storeRelease(1);
}

// 查询退出标志：嵌入后端轮询与后台循环检查用（acquire 语义）。
inline bool embeddingShutdownRequested() {
    return embedShutdownDetail::flag().loadAcquire() != 0;
}

// 仅供测试复位（test_embedding 的取消用例在同一进程内共用该标志，
// 不复位会短路后续用例的入口检查）。生产代码严禁调用——退出是不可
// 逆语义。
inline void resetEmbeddingShutdownForTesting() {
    embedShutdownDetail::flag().storeRelease(0);
}

} // namespace NexusAI