# API 契约

> 本文是插件与宿主之间的**正式契约**：数据类型、接口、线程归属、版本与兼容规则。
> 落地时头文件放在 `src/api/include/xcom/api/`，此处只定义语义，不写实现。
> 表中 `-> Result<T>` 表示成功返回 T、失败返回错误码，不抛异常。

## 1. 契约总则

1. **只依赖 `xcom_api`**：插件只能 include `<xcom/api/...>`，不得引用宿主内部头文件或链接内部库。
2. **接口只增不改**：v1 内新增方法用"可选接口 + 能力查询"方式（`queryInterface`），不修改已发布接口。
3. **不抛异常**：跨 DLL 边界禁止异常传播；插件内部异常自行捕获并转为错误返回。
4. **不用 STL 值类型**：接口签名只出现 Qt 值类型（`QString`、`QByteArray`、`QVariantMap`、`QVector`）与 POD；`std::function`、`std::string`、`std::vector` 禁止出现在签名中。
5. **线程显式**：每个方法标注调用线程；跨线程只传值。
6. **所有权显式**：实例由谁创建、由谁销毁，必须在接口注释中写明；宿主创建的视图 widget 由宿主销毁。
7. **nullptr 即"不支持"**：`IPluginContext::ui()` 在无界面模式下返回 `nullptr`，插件必须判空。

## 2. 版本与兼容

| 项 | 规则 |
|---|---|
| 版本宏 | `XCOM_API_VERSION_MAJOR`、`_MINOR`、`_PATCH` |
| 插件声明 | manifest `apiVersion` 使用语义化范围，如 `^1.0` |
| 兼容判定 | 主版本相等且次版本 ≤ 宿主次版本；补丁版本不参与 |
| 插件 IID | `com.xcom.plugin/1.0`（主版本变化即换 IID，旧插件自然不匹配） |
| 构建指纹 | `apiHash` 由公共头文件内容 + 版本 + 构建配置生成，插件启动前比对 |
| Qt 约束 | 插件必须与宿主同 Qt 版本、同编译器族、同架构构建 |

破坏性变更流程：提升主版本 → 新 IID → 旧接口保留至少一个大版本 → 在 `CHANGELOG-API.md` 记录迁移说明。

## 3. 数据类型

| 类型 | 字段 | 说明 |
|---|---|---|
| `SessionId` | `QString value`（`sess-xxxxxx`） | 会话唯一标识，进程内有效 |
| `PluginId` | `QString value`（反向域名） | 插件标识，同时是设置与日志命名空间 |
| `PortConfig` | `portName`、`baudRate`、`dataBits`(5–8)、`stopBits`(1/1.5/2)、`parity`(none/even/odd/mark/space)、`flow`(none/rtscts/xonxoff)、`dtr`、`rts` | 与 `QSerialPort` 解耦的自有枚举，由 serial 模块映射 |
| `SessionDescriptor` | `id`、`name`、`config`、`encoding`(system/utf8/latin1)、`autoReconnect`、`recordPolicy`、`tags` | 创建会话的完整描述 |
| `SessionState` | `Created / Opening / Open / Closing / Closed / Failed` | 状态机见 [serial-core.md](serial-core.md#3-会话状态机) |
| `DataChunk` | `session`、`direction`(rx/tx)、`sequence`、`monotonicNs`、`wallClockMs`、`bytes`、`flags`(gap/replay/truncated) | 数据面最小单位，≤ 64 KB |
| `SessionStats` | `rxBytes`、`txBytes`、`rxChunks`、`txChunks`、`droppedBytes`、`queueDepth`、`ringUsage`、`lastChunkAgoMs` | 快照，非实时引用 |
| `SessionInfo` | `id`、`name`、`portName`、`state`、`config`、`stats` | 列表查询返回的只读快照 |
| `Frame` | `parserId`、`bytes`、`sequence`、`timestampNs`、`meta` | 分帧结果，`meta` 为协议自定义字段 |
| `Error` | `code`（枚举）、`message`、`details`（`QVariantMap`）、`source`（模块或插件 id） | 见第 10 节 |
| `CommandSpec` | `id`、`title`、`category`、`icon`、`args`、`enablement`、`background`、`singleton`、`hidden` | 第 5 节 |
| `CommandArg` | `name`、`type`、`required`、`defaultValue`、`description`、`choices`、`min`、`max` | 参数 schema |
| `CommandInvocation` | `id`、`args`（`QVariantMap`）、`source`(ui/palette/terminal/cli/script/plugin)、`session` | 一次调用 |
| `CommandResult` | `ok`、`message`、`data`（`QVariantMap`） | 返回值 |
| `ViewSpec` | `id`、`title`、`icon`、`location`、`container`、`order`、`singleton`、`canClose`、`retainWhenHidden`、`instanceKey` | 第 8 节 |
| `MenuContribution` | `menu`、`group`、`order`、`command`、`when`、`label` | 菜单项 |
| `StatusItemSpec` | `id`、`alignment`、`priority`、`when`、`command`、`tooltip` | 状态栏项 |
| `SettingSpec` | `key`、`type`、`defaultValue`、`title`、`description`、`scope`、`choices`、`min`、`max` | 设置项声明 |
| `Event` | `topic`、`payload`（`QVariantMap`）、`timestampMs`、`source` | 事件总线消息 |
| `Subscription` | 不透明句柄，`unsubscribe()` | 注册即返回，move-only |

## 4. 插件与上下文

### `IPlugin`（主线程）

| 方法 | 说明 |
|---|---|
| `initialize(IPluginContext&, Error&) -> bool` | 只注册贡献，禁止耗时操作，目标 < 10 ms |
| `activate(Error&) -> bool` | 首次激活，创建资源；失败返回 false 并给出原因 |
| `deactivate()` | 释放资源，保留注册 |
| `shutdown()` | 最终清理，宿主退出或禁用时调用 |

### `IPluginContext`

| 方法 | 返回 | 线程 | 说明 |
|---|---|---|---|
| `commands()` | `ICommandRegistry&` | 主 | 注册/查询命令 |
| `sessions()` | `ISessionService&` | 主 | 会话生命周期 |
| `settings()` | `ISettingsService&` | 主 | 读写设置 |
| `settingsRegistry()` | `ISettingsRegistry&` | 主 | 声明设置项 |
| `events()` | `IEventBus&` | 主 | 订阅/发布事件 |
| `tasks()` | `ITaskService&` | 主 | 后台任务 |
| `notifications()` | `INotificationService&` | 主 | 通知 |
| `contextKeys()` | `IContextKeyService&` | 主 | 自定义上下文键 |
| `keybindings()` | `IKeybindingRegistry&` | 主 | 注册键位 |
| `log()` | `ILogger&` | 任意 | 已带插件命名空间 |
| `ui()` | `IUiContext*` | 主 | 无界面模式返回 `nullptr` |
| `dataDir()` | `QDir` | 任意 | 插件私有可写目录 |
| `pluginId()` | `PluginId` | 任意 | 自身 id |
| `pluginInfo(id)` | `PluginInfo` | 主 | 查询其他插件状态 |

### `IPluginHost`

| 方法 | 说明 |
|---|---|
| `list()` | 全部插件及其状态、版本、错误 |
| `setEnabled(id, bool)` | 启用/禁用；禁用立即 `deactivate` 并撤销贡献 |
| `state(id)` | `Discovered/Resolved/Loaded/Initialized/Activated/Deactivated/Failed/Disabled/Incompatible` |
| `lastError(id)` | 最近一次失败原因 |

## 5. 命令

### `ICommandRegistry`

| 方法 | 说明 |
|---|---|
| `registerCommand(const CommandSpec&, ICommandHandler*) -> Subscription` | id 冲突即失败，插件进入 `Failed` |
| `find(id)` | 返回 spec 与可用性 |
| `execute(const CommandInvocation&) -> CommandResult` | 同步执行；`background` 命令内部转任务 |
| `all()` | 供命令面板与文档生成 |

### `ICommandHandler`

| 方法 | 说明 |
|---|---|
| `execute(const CommandInvocation&, ICommandProgress&) -> CommandResult` | 主线程或任务线程池；不得阻塞 > 100 ms |

### `ICommandProgress`

| 方法 | 说明 |
|---|---|
| `setProgress(int percent, const QString& message)` | 0–100，-1 表示不确定 |
| `isCanceled()` | 轮询式取消 |
| `onCancel(IActionHandler*)` | 预留；v1 用轮询 |

## 6. 会话

### `ISessionService`（主线程）

| 方法 | 返回 | 说明 |
|---|---|---|
| `open(const SessionDescriptor&)` | `Result<SessionId>` | 端口独占检查、线程创建 |
| `close(const SessionId&)` | `Result<void>` | 异步关闭，等待排空 |
| `list()` | `QVector<SessionInfo>` | 快照 |
| `find(const QString& ref)` | `ISession*` | 支持 id / name |
| `active()` / `setActive(...)` | `ISession*` / `void` | 无参命令的目标 |
| `exists(const QString& portName)` | `bool` | 端口占用查询 |

### `ISession`

| 方法 | 返回 | 线程 | 说明 |
|---|---|---|---|
| `id()` / `name()` / `descriptor()` | 值 | 任意 | 只读快照，线程安全 |
| `state()` / `stats()` | 值 | 任意 | 快照 |
| `send(const QByteArray&)` | `Result<void>` | 任意 | 入队即返回，不保证写完 |
| `setDtr(bool)` / `setRts(bool)` | `Result<void>` | 任意 | 排队到 I/O 线程 |
| `reconfigure(const PortConfig&)` | `Result<void>` | 任意 | 部分参数需要重开端口 |
| `subscribe(ISessionDataSink*)` | `Subscription` | 主 | sink 在 I/O 线程回调 |
| `history(quint64 fromSequence, int maxChunks)` | `QVector<DataChunk>` | 主 | 视图晚挂载时补历史 |
| `recorder()` | `IRecorder*` | 主 | 录制控制 |

### `ISessionDataSink`（I/O 线程）

| 方法 | 说明 |
|---|---|
| `onData(const DataChunk&)` | 必须 < 5 ms，不得阻塞、不得创建 QWidget、不得调用服务 |
| `onStateChanged(SessionState, const Error&)` | 同上，轻量 |
| `onError(const Error&)` | 同上 |

### `IRecorder`

| 方法 | 说明 |
|---|---|
| `start(const QUrl& path, const RecordOptions&)` | 启动写线程 |
| `stop()` / `isRecording()` | 停止/状态 |
| `filePath()` | 当前文件 |

### `IFrameParser`（I/O 线程）

| 方法 | 说明 |
|---|---|
| `feed(const DataChunk&, QVector<Frame>& out)` | 纯状态机，可重置 |
| `reset()` | 会话重开时清状态 |

## 7. 设置、事件、任务、通知、日志

| 接口 | 关键方法 | 线程 |
|---|---|---|
| `ISettingsService` | `value(key, default)`、`setValue(key, v)`、`onChanged(key, callback)` | 主 |
| `ISettingsRegistry` | `define(const SettingSpec&) -> Subscription` | 主 |
| `IEventBus` | `subscribe(topic, IEventHandler*) -> Subscription`、`publish(const Event&)` | 主 |
| `IEventHandler` / `IActionHandler` | 单方法回调：`onEvent(const Event&)` / `invoke()`，由宿主在主线程调用 | 主 |
| `ICliRegistry` | `registerCommand(const CliCommandSpec&, ICommandHandler*) -> Subscription`，仅无界面模式可用 | CLI 主线程 |
| `ITaskService` | `run(const TaskSpec&, ITaskBody*) -> ITask*`；`ITask::cancel()`、`isCanceled()` | 主 |
| `INotificationService` | `info/warn/error(title, message, actions)`；`IOutputChannel::append(text)` | 主 |
| `ILogger` | `debug/info/warn/error(fmt)`，自动带插件分类 | 任意（线程安全） |
| `IContextKeyService` | `get/set/observe(key)`，插件键必须以 `<pluginId>.` 开头 | 主 |

事件主题（固定）：

| 主题 | 载荷 |
|---|---|
| `session/opened` | `SessionInfo` |
| `session/closed` | `id`、`reason` |
| `session/stateChanged` | `id`、`old`、`new`、`error` |
| `session/error` | `id`、`error` |
| `settings/changed` | `key`、`value`、`scope` |
| `plugin/activated` / `plugin/deactivated` / `plugin/failed` | `id`、`error` |
| `workspace/trustChanged` | `trusted` |
| `command/executed` | `id`、`source`、`ok`、`durationMs`（默认关闭，调试开启） |

热路径（每个数据块）**不**走事件总线，只用 sink。

## 8. UI 接口（仅头文件声明 QWidget 前向类型）

| 接口 | 关键方法 | 说明 |
|---|---|---|
| `IUiContext` | `views()`、`menus()`、`statusBar()`、`dialogs()`、`showView(id)`、`openSessionConsole(SessionId)` | 无界面模式不存在 |
| `IViewRegistry` | `registerView(ViewSpec, IViewFactory*)`、`show(id)`、`close(id)`、`isVisible(id)` | 主线程 |
| `IViewFactory` | `create(QWidget* parent, IViewHost&) -> QWidget*` | 首次显示时调用 |
| `IViewHost` | `viewState()`、`setViewState()`、`execute(command, args)`、`sessionId()`、`outputChannel()` | 视图与宿主交互的唯一通道 |
| `IMenuRegistry` | `addItem(const MenuContribution&)` | 主线程 |
| `IStatusBarRegistry` | `addItem(const StatusItemSpec&, IStatusItemFactory*)` | 主线程 |
| `IDialogService` | `openFile/saveFile/message/confirm` | 主线程，模态由宿主决定 |
| `IThemeRegistry` | `registerTheme(...)`、`activeTheme()`、`token(name)` | 主线程 |

`api` 库编译时只依赖 QtCore，以上接口中的 `QWidget*` 均为前向声明；插件实际使用界面能力时自行链接 QtWidgets。

## 9. 订阅与生命周期

- 所有 `register*` / `subscribe` 返回 `ISubscription`；`unsubscribe()` 幂等、线程约束为"创建线程"；
- 宿主按插件记录订阅，`deactivate()` 时逆序统一撤销，插件无需自己清理；
- 会话关闭时，该会话上的 sink 订阅自动失效；插件持有的 `ISubscription` 变为 no-op；
- 视图被销毁时通过 `IViewHost` 的销毁通知告知插件，插件不得再访问旧 widget；
- 订阅回调期间宿主保证插件对象存活（宿主先撤销订阅，再销毁插件对象）。

## 10. 错误模型

```
Error { code, message, details, source }
```

| 错误码 | 场景 |
|---|---|
| `Ok` | 成功 |
| `InvalidArgument` | 参数缺失/越界/格式错误 |
| `NotFound` | 会话、命令、插件、文件不存在 |
| `AlreadyExists` | 命令 id、会话名冲突 |
| `InvalidState` | 对已关闭会话发送、重复打开 |
| `NotSupported` | 接口/能力不支持（无界面模式调用 UI） |
| `PermissionDenied` | 工作区未信任、能力未声明 |
| `Timeout` | 命令、打开端口、写入排空超时 |
| `Canceled` | 用户取消 |
| `InvalidConfig` | 串口参数非法（波特率、数据位越界） |
| `PortNotFound` / `PortBusy` / `AccessDenied` / `DeviceRemoved` | 串口相关，见 [serial-core.md](serial-core.md#12-错误模型) |
| `OpenTimeout` / `WriteTimeout` / `NotOpen` / `QueueFull` | 打开超时、发送排空超时、会话未打开、写队列满 |
| `TooManySessions` | 超过会话上限 |
| `StorageError` | 录制、导出写入失败 |
| `PluginFailed` | 插件初始化/激活失败 |
| `Internal` | 不应发生的内部错误，必须带日志 |

`source` 取值：`core:<module>`、`plugin:<id>`、`command:<id>`。

## 11. ID 规范

| 类型 | 规则 | 示例 |
|---|---|---|
| 插件 id | 反向域名，小写 | `com.example.modbus` |
| 命令 id | `<域>.<对象>.<动作>`，小写点分 | `serial.session.open`、`serial.data.send` |
| 核心命令域 | `app`、`serial`、`view`、`settings`、`workspace`、`plugin` | `app.palette.show` |
| 视图 id | `<域>.<名称>` | `serial.console`、`serial.explorer` |
| 设置键 | `core.<段>.<名>` 或 `<插件 id>.<名>` | `core.ui.theme`、`com.example.modbus.pollMs` |
| 事件主题 | `<域>/<实体>/<动作>` | `session/opened`、`settings/changed` |
| 会话名 | 工作区内唯一，默认端口名，冲突加 `-2` | `COM3`、`设备A` |
| 菜单路径 | `menu/<名>` 或 `context/<名>` | `menu/session`、`context/sessionTab` |

## 12. 弃用策略

- 接口方法弃用：先在新次版本标记 `[[deprecated]]` 并在 `api-contracts.md` 标注，保留 2 个次版本后移除，且移除只能发生在主版本升级时；
- 命令 id 弃用：保留 1 个大版本，期间转发到新 id 并记录警告；
- 设置键弃用：读取时自动迁移到新键并写回，日志提示一次；
- manifest 字段弃用：至少保留 2 个次版本，解析时警告。

## 13. 最小插件契约（示例流程）

一个"HEX 校验工具"插件的完整契约路径（只描述流程）：

1. `manifest.json` 声明 `id = com.example.checksum`、`apiVersion = ^1.0`、`activationEvents = ["onCommand:checksum.compute"]`、`contributes.commands`；
2. 插件类实现 `IPlugin`，`initialize` 中调用 `ctx.commands().registerCommand(spec, handler)`，返回的 `Subscription` 交宿主托管；
3. `activate` 中不创建界面（命令按需执行），如需面板则 `ctx.ui()->views().registerView(...)`；
4. handler 校验参数（hex 字符串）、调用纯函数计算、返回 `CommandResult{ data = {"crc16": ..., "sum8": ...} }`；
5. 命令面板、CLI、脚本调用均得到同一结果结构；
6. `deactivate` 时宿主撤销订阅，插件无残留对象。

## 14. 契约测试清单（合并到插件模板）

- [ ] manifest 通过 schema 校验，id 与目录名一致；
- [ ] `initialize` 后注册项可枚举，数量与类型正确；
- [ ] `initialize` 内不发生耗时操作（测试用计时器断言 < 10 ms）；
- [ ] `activate`/`deactivate` 可重复调用 3 次无崩溃、无重复注册；
- [ ] `deactivate` 后视图关闭、命令不可见、sink 不再收到数据；
- [ ] 命令参数 schema 与 handler 行为一致（必填缺失返回 `InvalidArgument`）；
- [ ] 命令执行不阻塞主线程超过 100 ms（或声明 `background`）；
- [ ] 插件日志分类为 `xcom.plugin.<id>`；
- [ ] 在 `--safe-mode` 下不被加载，宿主功能不受影响；
- [ ] 兼容性不匹配（改 `apiHash`）时被正确拒绝且提示原因。
