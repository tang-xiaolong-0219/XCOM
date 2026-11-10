# 串口核心设计（多会话引擎）

> 目标：把"串口"从界面里彻底剥离成一个可复用、可测试、可被多视图同时消费的引擎。
> 接口契约见 [api-contracts.md](api-contracts.md)，UI 侧如何消费见 [shell-ui-command.md](shell-ui-command.md)。

## 1. 设计目标

1. **一个会话 = 一个对象**，可同时存在多个，生命周期独立。
2. **I/O 不碰 UI 线程**：打开、读写、关闭全部在会话自己的线程里完成。
3. **数据可多播**：一个会话的数据可以同时给控制台视图、协议解析插件、录制器，互不阻塞。
4. **可脱离硬件测试**：传输层抽象出回环、脚本、回放、TCP 桥四种替身。
5. **可诊断**：每个会话都有状态机、计数器、错误码、可选录制文件。

## 2. 领域模型

```
SessionService / SessionManager      (主线程，唯一入口)
   └── SerialSession                 (主线程 facade，持有状态、统计、订阅表)
          ├── SessionWorker          (I/O 线程，拥有 QSerialPort，持有时钟与序列号)
          │      └── ISerialTransport(QSerialPort / Loopback / Replay / TcpBridge)
          ├── RingBuffer             (每会话一段有界历史，供视图晚挂载与回放)
          ├── Recorder               (可选，专属写线程)
          └── Sinks[]                (按注册顺序同步调用，运行在 I/O 线程)
```

| 概念 | 职责 | 不负责 |
|---|---|---|
| `ISerialTransport` | 打开/关闭、写字节、上报收到的字节与错误；不解析、不缓冲 | 会话状态、订阅、UI |
| `SerialSession` | 对外门面：状态、配置、统计、订阅、历史、录制开关 | 直接操作串口句柄 |
| `SessionWorker` | 串口读写、时间戳、顺序号、分发到 sink | 界面、设置、命令 |
| `RingBuffer` | 有界历史（默认 8 MB），丢旧并标记缺口 | 事件通知 |
| `SessionPipeline` | 有序的 sink/解析器链，出错隔离 | 决定谁订阅 |
| `Recorder` | `.xlog` 落盘与轮转 | 展示、解析 |

## 3. 会话状态机

```
            open()
  Created ─────────► Opening ──────► Open
                        │              │  close() / error
                        │ 失败          ▼
                        └────► Failed  Closing ──► Closed
                                         ▲
                              Open ── err┘
```

| 状态 | 含义 | 可接受操作 |
|---|---|---|
| Created | 描述符已建，未占用端口 | open、dispose |
| Opening | 正在打开/应用参数 | cancel、close |
| Open | 正常收发 | send、close、setConfig、setDtr/Rts、subscribe |
| Closing | 正在关闭，等待写入完成 | 只读查询 |
| Closed | 已释放端口，可 reopen | dispose、open |
| Failed | 打开失败或不可恢复错误，携带错误码 | close→Closed、重试 open |

状态变化统一发出 `stateChanged(SessionId, oldState, newState, Error cause)`；UI 只订阅这个信号，不轮询。

## 4. 标识与寻址

| 标识 | 形式 | 用途 |
|---|---|---|
| `SessionId` | `sess-8f3a1c`（启动内唯一） | 所有 API 的精确引用 |
| `name` | 工作区内唯一，默认 `COM3`、冲突时 `COM3-2` | 命令行、脚本、标签页 |
| `portName` | `COM3` | 物理端口，同一端口同时只能有一个会话 |

命令与脚本中的寻址优先级：显式 `--session <id|name>` → 命令参数里的会话 → 当前活动会话 → 报错 `NoActiveSession`。

## 5. 多会话管理

`SessionService` 的职责：

| 能力 | 说明 |
|---|---|
| `open(descriptor)` | 校验参数、端口独占、创建对象、启动 I/O 线程；返回 `Result<SessionId>` |
| `close(id)` / `closeAll()` | 异步关闭，等待写入排空（默认 1 s 超时） |
| `list()` / `find(id\|name)` | 返回快照，供资源管理器与 CLI 使用 |
| `active()` / `setActive(id)` | 活动会话，供无参命令使用；由 UI 焦点驱动 |
| 端口占用表 | `portName → SessionId`，重复打开直接返回 `PortBusy` 并指出占用者 |
| 上限保护 | 默认最多 32 个会话，可配置；超限返回 `TooManySessions` |
| 自动重连 | 会话级开关；设备掉线后按退避重试（1 s/2 s/5 s/10 s，最多 5 次） |
| 恢复 | 从工作区读取会话列表，启动时按 `restore` 标记重建 |
| 事件 | `sessionOpened` / `sessionClosed` / `stateChanged` / `error` |

同一物理端口不允许两个会话（Windows 独占），因此"打开多个串口对象"意味着**多个不同端口**，或同一端口的历史回放会话（虚拟类型，不占用端口）。

### 5.1 操作多个会话的方式

| 模式 | 目标如何确定 | 典型用法 |
|---|---|---|
| 活动会话 | UI 焦点 / `setActive` | 控制台按钮、`Ctrl+Enter` 发送 |
| 显式指定 | `--session <id\|name>` 或命令参数 | 脚本、CLI、插件调用 |
| 广播 | 多选会话（资源管理器 Ctrl/Shift 多选） | 向多个设备发同一条指令（`serial.data.broadcast`） |
| 会话组 | 工作区中命名的会话集合 | 一键"全部打开/全部关闭/全部发送" |
| 遍历 | 插件或脚本对 `list()` 结果逐个执行 | 批量改参数、批量导出 |

广播与批量关闭属于**不可逆操作**，命令必须二次确认（CLI 需显式 `--yes`），结果按会话聚合展示（成功 N 个、失败 M 个及原因）。

## 6. 线程模型

```
主线程                          I/O 线程（每会话一条）
──────                          ─────────────────────
SerialSession (facade)  请求队列   SessionWorker
  ├ open()      ───────────────►   QSerialPort.open / applyConfig
  ├ send()      ───────────────►   write() + bytesWritten 排空
  ├ setDtr()    ───────────────►   setDataTerminalReady
  ├ close()     ───────────────►   flush + close + quit
  └ subscribe() ──────────────►   sinks 同步调用
                           ◄──────  chunk / stateChanged / error / bytesWritten
```

规则：

- 请求一律 `QMetaObject::invokeMethod(worker, ..., Qt::QueuedConnection)`，带 `RequestId`，异步完成通过信号回传；调用方不等待。
- `chunk` 与 `bytesWritten` 通过队列连接回到 `SerialSession` 所在线程（主线程），再分发给 UI 适配器与事件总线。
- sink 在 I/O 线程同步执行，契约要求"不阻塞、不分配大对象、不调用服务、不碰 UI"；单次超 5 ms 记录一次慢 sink 警告（按插件 id 聚合，每分钟最多一条）。
- 关闭顺序：停止接收 → 等待写队列排空（超时强制）→ `close()` → `thread.quit()` → `wait(2000)`；超时只记录，不 `terminate()`（除非进程退出）。
- 每个会话一个线程的代价约 1 MB 栈 + 句柄，32 个会话仍在预算内；不为每个会话创建事件循环以外的线程。

## 7. 数据通路

```
            ┌─────────────── SessionWorker (I/O 线程) ───────────────┐
 QSerialPort│ readyRead → readAll（单次 ≤ 64 KB）                     │
   │        │   → 打时间戳（steady + 墙钟偏移）                      │
   │        │   → 递增 sequence（每方向独立）                        │
   │        │   → Recorder.enqueue（若有）                           │
   │        │   → 逐个 sink.onData(chunk)                            │
   └────────┤        ├─ 插件解析器（帧缓存、CRC、协议状态机）         │
            │        └─ UI 适配器（队列投递到主线程，批量合并）        │
            └────────────────────────────────────────────────────────┘
                                   │ queued
                                   ▼
                      SessionController（主线程）
                        ├ 追加到视图缓冲（HEX/文本/时间戳格式化）
                        ├ 30 Hz 合并刷新到 ConsoleView
                        └ 统计、暂停、丢帧提示
```

设计取舍：**插件解析器在 I/O 线程跑纯函数**，避免主线程被协议解析拖慢；**格式化与渲染在主线程批量做**，避免跨线程操作 QWidget。

## 8. 数据块与时间戳

`DataChunk` 字段（详见 [api-contracts.md](api-contracts.md#3-数据类型)）：

| 字段 | 说明 |
|---|---|
| `session` | 来源会话 |
| `direction` | Rx / Tx（Tx 块用于回显与录制对齐） |
| `sequence` | 每方向单调递增，用于去重、缺口检测、历史查询 |
| `monotonicNs` | `steady_clock`，用于计算间隔、回放时序 |
| `wallClockMs` | 墙钟毫秒，用于显示与日志对齐 |
| `bytes` | 负载，≤ 64 KB |
| `flags` | `Gap`（历史被丢弃）、`Replay`（回放数据）、`Truncated` |

时间戳在**读取返回的第一时间**打，不等到 UI 显示；墙钟偏移在会话打开时计算一次，之后只用 steady 时钟换算，避免系统时间调整导致回退。

## 9. 读取、缓冲与背压

| 环节 | 策略 |
|---|---|
| 读取 | `readyRead` → `readAll()`，单次上限 64 KB，循环直到无数据；不在 I/O 线程做格式化 |
| 历史缓冲 | 每会话 8 MB 环形缓冲（可配 1–64 MB）；超出丢最旧 chunk，置 `Gap` 并计数 |
| UI 刷新 | 适配器按 16–33 ms 合并（默认 30 Hz）；单次追加 ≤ 256 KB；超过阈值拆批 |
| 视图显示 | 控制台默认最多 10 万行/块，超出滚动删除；丢弃行数显示在状态栏 |
| 暂停显示 | 只停 UI 消费，引擎继续接收、计数、录制 |
| 录制 | 专用队列 64 MB，满时轮转或停止并通知，**不允许静默丢数据** |
| sink 过慢 | 记录慢 sink；可选"异步 sink 包装"（有界队列，溢出丢弃并计数），默认同步 |

整体原则：**计数与录制不丢，显示可丢**。任何丢弃都必须在 UI 上可见。

## 10. 插件数据接入

两种方式：

1. **`ISessionDataSink`**（推荐）：`session->subscribe(sink)`，I/O 线程回调，适合协议解析、统计、转发。返回 `ISubscription`，会话关闭时自动解绑。
2. **事件总线**（不推荐用于热路径）：`session/data` 主题，主线程、有额外拷贝，只适合低频率通知与调试。

分帧器（`IFrameParser`）作为 sink 的一种特化：输入字节流，输出完整帧（`Frame { bytes, sequence, timestamp }`），供协议插件与视图消费。分帧器必须是纯函数 + 内部状态，不得跨线程共享实例。

生命周期：插件 `deactivate` 时宿主统一撤销其全部 sink 订阅；会话关闭时广播 `onSessionClosed` 并清理订阅表。

## 11. 发送路径

```
命令 serial.data.send / 控制台按钮
  → SerialSession::send(bytes)  （主线程，入队即返回）
  → Worker 写队列（上限 1 MB）
  → QSerialPort::write，处理部分写
  → bytesWritten / writeCompleted
  → 统计 txBytes、可选 Tx 回显块
```

规则：

- `send` 只保证"进入队列"，不保证"写完"；v1 以 `bytesWritten` 统计与命令层分块进度作为确认手段，不提供逐次确认句柄；
- 队列超限返回 `QueueFull`，由调用方决定重试或提示；
- 大文件发送不放在引擎里，由插件/命令用"分块 + 进度 + 可取消"实现，块大小 4–16 KB，块间等待 `bytesWritten`；
- 定时发送属于控制台视图/命令层能力（一个 `QTimer` + `serial.data.send`），引擎不内置定时器。

## 12. 错误模型

| 错误码 | 触发场景 | UI 处理建议 |
|---|---|---|
| `InvalidConfig` | 波特率非法、参数越界 | 参数表单内联报错 |
| `PortNotFound` | 端口被拔掉/名称不存在 | 通知 + 会话置 Failed |
| `PortBusy` | 其他程序或本程序另一会话占用 | 通知，指出占用者 |
| `AccessDenied` | 权限不足、驱动拒绝 | 通知 + 诊断链接 |
| `OpenTimeout` | 驱动无响应 | 通知，允许重试 |
| `NotOpen` | 对已关闭会话发送 | 状态栏提示 |
| `QueueFull` | 写队列超过 1 MB | 提示"设备未就绪/流控阻塞" |
| `WriteTimeout` | 5 s 内未排空 | 警告，可选择强制清空 |
| `DeviceRemoved` | 运行中设备消失 | 自动关闭 + 重连提示 |
| `ReadError` | 驱动读错误 | 关闭会话 + 日志 |
| `StorageError` | 录制写入失败（磁盘满/权限） | 停止录制 + 通知 |
| `TooManySessions` | 超过会话上限 | 提示关闭旧会话 |

错误统一带 `sessionId` 与 `pluginId`（若来自插件 sink），进入会话错误历史（最近 50 条），供诊断面板读取。

## 13. 录制与回放

**格式 `.xlog`**（二进制，便于无损耗回放）：

```
magic "XLOG" | version u16 | headerLen u32 | header JSON
records*: type u8 | flags u8 | reserved u16 | timestampNs u64 | sequence u64 | len u32 | payload
```

- header JSON 保存会话名、端口、参数、编码、开始时间、宿主版本；
- record type：`1=Rx`、`2=Tx`、`3=marker`（用户插入的注释/时间标记）、`4=metadata`（参数变更）；
- 写入独立线程，顺序追加，64 MB 或 24 h 轮转，文件名 `<name>-YYYYMMDD-HHmmss.xlog`；
- 文本/CSV 导出由导出器插件实现，引擎只提供读取 API；
- 回放：`ReplayTransport` 读取 `.xlog`，按原始时序或倍速投递，创建一个"虚拟会话"（类型 `replay`，不占端口），因此控制台、插件、导出器可以完全复用。

## 14. 虚拟传输与测试

| 传输 | 行为 | 用途 |
|---|---|---|
| `LoopbackTransport` | 写入即作为接收回显，可配延迟与字节上限 | 演示、UI 冒烟、基准 |
| `ScriptedTransport` | 预设时间线 `[(delayMs, bytes), ...]` | 确定性 UI 测试、录屏 |
| `ReplayTransport` | 回放 `.xlog` | 故障复盘、协议调试 |
| `TcpBridgeTransport` | 本地 TCP 端口 ↔ 虚拟串口双向转发 | 外部脚本/CI 模拟设备、跨机联调 |
| `NullTransport` | 吞掉写入、不产生数据 | 连接到"不存在的设备"做 UI 测试 |

测试策略：

- 单元：状态机迁移、参数应用顺序、写队列、环形缓冲丢帧标记、错误映射；
- 集成：`ScriptedTransport` + 真实 `SessionService` + 主线程事件循环，验证 4 会话并发、关闭顺序、sink 撤销；
- 压测：`LoopbackTransport` 以 921600 bps 等速灌数据 10 分钟，检查计数一致、内存稳定、UI 可交互。

## 15. 性能与可观测性

每个会话持续统计：`rxBytes`、`txBytes`、`rxChunks`、`txChunks`、`droppedBytes`、`queueDepth`、`ringUsage`、`lastChunkAgoMs`、`sinkCostMs`（按 sink 聚合）。

- 状态栏展示：会话名、速率、RX/TX、丢弃量（异常时高亮）；
- `命令 session.stats --json` 输出完整指标，供 CLI 与诊断使用；
- 超过阈值（如 sink 单次 > 5 ms、队列 > 75%）写入 `xcom.serial` 警告日志，并在会话工具提示中显示。

## 16. 与 UI 的边界

引擎不 include 任何 UI 头文件，也不认识"视图"。UI 侧每个可见会话对应一个 `SessionController`：

- 订阅 `chunk/stateChanged/stats`，在主线程维护格式化后的显示缓冲；
- 按视图设置（HEX/时间戳/编码/自动滚动）格式化，30 Hz 批量提交给 `ConsoleView`；
- 视图关闭时只销毁 controller，不影响会话（后台继续接收/录制）；
- 同一会话可以开多个 controller（分屏、多视图），各自独立暂停与滚动位置。
