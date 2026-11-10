#include "serial/session_service.h"
#include "services/event_bus.h"
#include "services/subscription.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <functional>

#include <cstdio>

namespace {

int g_failures = 0;

void check(bool condition, const char *what)
{
    if (!condition) {
        std::printf("FAIL  %s\n", what);
        ++g_failures;
    } else {
        std::printf("ok    %s\n", what);
    }
}

void spin(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

bool waitFor(const std::function<bool()> &predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
        spin(20);
    }
    return predicate();
}

class TestSink : public xcom::ISessionDataSink
{
public:
    void onData(const xcom::DataChunk &chunk) override
    {
        if (chunk.direction == xcom::Direction::Rx) {
            rxBytes.append(chunk.bytes);
            ++rxChunks;
        }
    }

    QByteArray rxBytes;
    int rxChunks = 0;
};

class PingHandler : public xcom::IEventHandler
{
public:
    void onEvent(const xcom::Event &event) override { topics.append(event.topic); }
    QStringList topics;
};

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    xcom::EventBus bus;
    xcom::SessionService sessions(&bus);

    PingHandler handler;
    class Counter : public xcom::IEventHandler
    {
    public:
        void onEvent(const xcom::Event &event) override
        {
            if (event.topic == QStringLiteral("session/opened")) {
                ++opened;
            } else if (event.topic == QStringLiteral("session/closed")) {
                ++closed;
            }
        }
        int opened = 0;
        int closed = 0;
    };

    Counter counter;
    xcom::ISubscription *openedSub = bus.subscribe(QStringLiteral("session/opened"), &counter);
    xcom::ISubscription *closedSub = bus.subscribe(QStringLiteral("session/closed"), &counter);

    std::printf("=== X-COM 串口核心集成测试 ===\n");

    // 1. 打开 4 个回环会话（无硬件）
    QStringList ids;
    for (int i = 0; i < 4; ++i) {
        xcom::SessionDescriptor descriptor;
        descriptor.transport = QStringLiteral("loopback");
        descriptor.config.portName = QStringLiteral("LOOPBACK");
        descriptor.config.baudRate = 921600;
        descriptor.name = QStringLiteral("回环-%1").arg(i + 1);
        xcom::Error error;
        QString id;
        const bool ok = sessions.open(descriptor, &id, &error);
        check(ok, qPrintable(QStringLiteral("打开会话 %1").arg(descriptor.name)));
        if (!ok) {
            std::printf("      error: %s\n", qPrintable(error.message));
        }
        ids.append(id);
    }

    check(sessions.list().size() == 4, "会话列表包含 4 个会话");
    check(waitFor([&] { return counter.opened == 4; }, 3000), "4 个会话全部进入 Open 状态");

    // 2. 会话名唯一性与端口独占
    {
        xcom::SessionDescriptor descriptor;
        descriptor.transport = QStringLiteral("loopback");
        descriptor.name = QStringLiteral("回环-1");
        xcom::Error error;
        QString id;
        const bool ok = sessions.open(descriptor, &id, &error);
        check(!ok && error.code == xcom::ErrorCode::AlreadyExists, "同名会话被拒绝");
    }

    // 3. 数据收发
    TestSink sink;
    xcom::ISubscription *sinkSub = sessions.find(ids.first())->subscribe(&sink);
    check(sinkSub != nullptr, "订阅数据 sink");

    const QByteArray payload("AT+HELLO\r\n");
    xcom::Error sendError;
    check(sessions.find(ids.first())->send(payload, &sendError), "发送数据成功");
    check(waitFor([&] { return sink.rxBytes == payload; }, 2000), "回环数据完整返回");
    check(sink.rxChunks >= 1, "至少收到一个数据块");

    // 4. 历史缓冲
    const QVector<xcom::DataChunk> history = sessions.find(ids.first())->history();
    check(!history.isEmpty(), "历史缓冲可读");
    bool hasTx = false;
    bool hasRx = false;
    for (const xcom::DataChunk &chunk : history) {
        if (chunk.direction == xcom::Direction::Tx) {
            hasTx = true;
        }
        if (chunk.direction == xcom::Direction::Rx) {
            hasRx = true;
        }
    }
    check(hasTx && hasRx, "历史同时记录收发两个方向");

    // 5. 活动会话切换
    sessions.setActiveId(ids.at(2));
    check(sessions.activeId() == ids.at(2), "活动会话可切换");
    check(sessions.find(QStringLiteral("active")) == sessions.find(ids.at(2)),
          "active 引用解析为当前会话");
    check(sessions.find(QStringLiteral("回环-3")) != nullptr, "按名称查找会话");

    // 6. 订阅撤销后不再接收
    sinkSub->unsubscribe();
    const int before = sink.rxChunks;
    sessions.find(ids.first())->send(QByteArray("PING"), nullptr);
    spin(200);
    check(sink.rxChunks == before, "撤销订阅后不再收到数据");

    // 7. 关闭会话
    xcom::Error closeError;
    check(sessions.close(ids.first(), &closeError), "关闭会话成功");
    check(sessions.list().size() == 3, "关闭后会话数减为 3");
    check(waitFor([&] { return counter.closed == 1; }, 2000), "收到 session/closed 事件");

    // 8. 关闭全部
    sessions.closeAll();
    check(sessions.list().isEmpty(), "关闭全部会话");
    check(!sessions.find(ids.at(2)), "关闭后无法再查到会话");
    check(waitFor([&] { return counter.closed == 4; }, 3000), "全部关闭事件已发出");

    openedSub->unsubscribe();
    closedSub->unsubscribe();

    // sinkSub 由宿主拥有：unsubscribe() 后由宿主自行回收，调用方不得 delete。

    std::printf("=== %s（失败 %d 项）===\n", g_failures == 0 ? "全部通过" : "存在失败", g_failures);
    return g_failures == 0 ? 0 : 1;
}
