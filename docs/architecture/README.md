# X-COM 架构设计：插件化串口工作台

> 版本 v0.1（设计稿）　2026-11-08
> 基线：当前单窗口串口助手（`X-COM.pro` + `mainwindow.cpp`）
> 本目录只描述架构，不含代码改动；落地顺序见 [roadmap.md](roadmap.md)。

## 1. 目标

把"一个窗口、一个串口、一份代码"的串口助手，演进为 **内核 + 外壳 + 插件** 的工作台：

- **多会话**：同时打开、操作任意多个串口，每个会话有独立参数、独立视图、独立日志，互不影响。
- **可扩展**：第三方（以及我们自己）通过插件增加命令、面板、协议解析、导出格式，乃至新的会话类型（TCP/UDP/BLE）。
- **统一入口**：所有操作都是命令。菜单、快捷键、命令面板、内置终端、无界面 CLI、脚本，走同一条命令派发链路，不存在"只有界面能做的事"。
- **可失败**：单个插件出错不影响宿主编译期与运行期；插件可禁用、可诊断、可安全模式启动。

### 1.1 一句话结构

- **Shell** 负责窗口、布局、命令、设置、插件生命周期；
- **Serial 引擎** 负责"会话对象"，一个会话 = 一个可被多处观察的数据源；
- **功能全部以插件实现**，包括内置功能，确保插件 API 从第一天就被真实使用。

### 1.2 质量属性与指标

| 属性 | 指标 | 验证方式 |
|---|---|---|
| 启动速度 | 无第三方插件时冷启动 < 300 ms | 计时埋点 + CI 基准 |
| 扩展性 | 新增一个协议解析插件不改宿主代码 | 示例插件验收 |
| 并发 | 4 个会话 × 921600 bps 持续 10 分钟不丢数据（显示层允许丢帧但计数准确） | 虚拟端口压测 |
| 响应性 | 命令面板输入延迟 < 16 ms；UI 刷新 ≤ 30 Hz 批量合并 | 性能探针 |
| 稳定性 | 插件初始化失败、运行异常不影响其他插件与宿主 | 故障注入测试 |
| 可测试性 | 串口逻辑可脱离硬件运行 | 虚拟传输 + 单测 |
| 可维护性 | 模块间无循环依赖，插件只依赖 `xcom_api` | 构建期检查 |

## 2. 非目标（v1 不做，架构留口）

- 插件市场 / 在线安装 / 自动更新；
- 插件的进程隔离与沙箱（v1 为进程内插件，见 ADR-003）；
- 多窗口、远程开发、协同；
- 跨平台打包（先 Windows，但传输层按跨平台设计）；
- 插件双向 ABI 兼容（插件必须与宿主同 Qt 版本重编）。

## 3. 设计原则

1. **内核最小化**：宿主只做"稳定且所有插件都需要"的事，业务功能尽量下沉到插件。
2. **一切皆命令**：任何用户可见操作先定义命令，再挂到菜单 / 快捷键 / 面板 / CLI。
3. **接口即契约**：插件只 include `<xcom/api/...>`，内部实现细节不导出；接口只增不改。
4. **数据面与界面分离**：串口数据在 I/O 线程产生，界面只在主线程消费，中间靠批量缓冲。
5. **主线程不阻塞**：主线程不做文件 I/O、不做串口读写、不 sleep。
6. **懒激活**：插件按 activationEvent 首次使用时才真正加载资源，启动只做元数据扫描。
7. **生命周期显式**：谁创建谁释放，注册即返回可撤销的订阅令牌。
8. **配置分层**：内置默认 → 用户 → 工作区 → 会话，逐层覆盖，可追溯。
9. **失败可隔离、可诊断**：错误有错误码与插件归属，日志按模块分类。
10. **先自用再冻结**：先用 3 个内置插件把 API 用一遍，确认后再冻结 v1。

## 4. 总体分层

```
┌────────────────────────────────────────────────────────────────────┐
│  app (X-COM.exe)   cli (xcom-cli.exe)                              │
│  入口、单实例、崩溃处理、启动参数、无界面模式                        │
├────────────────────────────────────────────────────────────────────┤
│  shell (QtWidgets)                                                 │
│  主窗口 · 停靠布局 · 视图注册表 · 命令面板 · 终端视图 · 主题        │
│    └── ui-kit：可复用控件（会话控制台、HEX 视图、状态项、图标）      │
├────────────────────────────────────────────────────────────────────┤
│  services (QtCore)                                                 │
│  命令 · 键位 · 上下文键 · 设置 · 工作区 · 任务 · 通知 · 事件        │
│  插件宿主（发现/校验/加载/生命周期）                                │
├────────────────────────────────────────────────────────────────────┤
│  serial (QtCore)                                                   │
│  传输层 · 会话 · 会话管理 · 数据管线 · 录制/回放 · 虚拟端口         │
├────────────────────────────────────────────────────────────────────┤
│  api (QtCore)                                                      │
│  插件契约：IPlugin / 命令 / 会话 / 设置 / 事件 / 视图（仅前向声明）  │
├────────────────────────────────────────────────────────────────────┤
│  foundation                                                        │
│  日志 · Result/Error · ID · 路径 · JSON 工具 · Worker 基类          │
└────────────────────────────────────────────────────────────────────┘

plugins/* 只依赖 api（UI 插件额外依赖 QtWidgets 与 ui-kit）
```

关键点：**`api` 只包含纯虚接口与数据类型**，且不 include 任何 QtWidgets 头文件；界面相关接口只前向声明 `QWidget` / `QAction`。这样 `serial`、`services`、`cli` 都保持 QtCore 依赖，而插件想要做界面时自己链接 QtWidgets 即可。

## 5. 模块清单

| 模块 | 产物 | 依赖 | 职责 | 运行线程 |
|---|---|---|---|---|
| foundation | `xcom_foundation`（静态库） | QtCore | 日志、错误码、ID、路径、JSON 读写、通用 Worker 基类 | 任意 |
| api | `xcom_api`（**共享库**） | QtCore | 插件契约、数据类型、各服务接口、UI 接口声明 | 契约 |
| serial | `xcom_serial`（静态库） | foundation, api | 传输层、会话、会话管理、管线、录制、虚拟端口 | I/O 线程 |
| services | `xcom_services`（静态库） | foundation, api, serial | 命令、键位、上下文键、设置、工作区、插件宿主、任务、通知、事件 | 主线程 |
| ui-kit | `xcom_ui_kit`（**共享库**） | api, QtWidgets | 会话控制台、HEX 视图、状态项、主题令牌、图标注册 | UI 线程 |
| shell | `xcom_shell`（静态库） | services, ui-kit, QtWidgets | 主窗口、布局、视图/菜单/状态栏注册表实现、命令面板、终端视图 | UI 线程 |
| app | `X-COM.exe` | shell | 入口、单实例、崩溃处理、解析启动参数 | 主线程 |
| cli | `xcom-cli.exe` | services, serial, api | 无界面模式、一次性命令、脚本执行 | 主线程 |
| plugins | DLL / JS 包 | api（+ ui-kit） | 命令、视图、解析器、导出、主题 | 依插件 |

为什么 `api` 必须是共享库：插件与宿主需要共享同一份接口类型信息（RTTI / vtable 身份）。`foundation / serial / services / shell` 用静态库链进两个可执行文件，减少 DLL 数量；`ui-kit` 共享是因为插件也会链接它。

## 6. 仓库结构

```
X-COM/
├─ CMakeLists.txt                 # 顶层：project(xcom VERSION 0.4.0)
├─ cmake/
│  ├─ XComApi.cmake               # api 版本、IID、buildStamp 生成
│  └─ Packaging.cmake             # windeployqt、插件目录布局
├─ src/
│  ├─ foundation/
│  │  ├─ log/                     # 分类日志 + 文件 sink + 轮转
│  │  ├─ result/                  # Result<T> / Error{code,message,details}
│  │  ├─ ids/                     # SessionId / PluginId / CommandId 解析
│  │  ├─ json/                    # JSON 读写、schema 校验
│  │  └─ concurrent/              # Worker 基类、队列、节流器
│  ├─ api/                        # 只放头文件 + 少量导出符号
│  │  └─ include/xcom/api/
│  │     ├─ version.h             # XCOM_API_VERSION、IID、兼容规则
│  │     ├─ types.h               # SessionId、DataChunk、结果类型
│  │     ├─ plugin.h              # IPlugin、IPluginContext、IPluginHost
│  │     ├─ commands.h            # CommandSpec、ICommandHandler、ICommandRegistry
│  │     ├─ sessions.h            # ISession、ISessionService、ISessionDataSink
│  │     ├─ settings.h            # ISettingsService、SettingSpec
│  │     ├─ events.h              # IEventBus、Subscription
│  │     ├─ ui.h                  # IUiContext、IViewRegistry、IMenuRegistry（前向声明 QWidget）
│  │     └─ services.h            # ITaskService、INotificationService、ILogger
│  ├─ serial/
│  │  ├─ transport/               # ISerialTransport、QSerialPort 实现、枚举
│  │  ├─ session/                 # SerialSession、SessionWorker、状态机
│  │  ├─ pipeline/                # 分帧、编解码、sink 分发
│  │  ├─ recorder/                # .xlog 写入/轮转/回放
│  │  └─ virtual/                 # Loopback / Scripted / Replay / TcpBridge
│  ├─ services/
│  │  ├─ commands/                # 注册表、参数、enablement、执行器
│  │  ├─ keybindings/             # 默认键位、用户覆盖、冲突检测
│  │  ├─ contextkeys/             # 上下文键与表达式求值
│  │  ├─ settings/                # 分层配置、schema、变更通知
│  │  ├─ plugins/                 # 扫描、manifest、加载、生命周期、依赖
│  │  ├─ workspace/               # xcom.json、布局、会话恢复
│  │  ├─ tasks/                   # 后台任务、进度、取消
│  │  ├─ events/                  # 事件总线
│  │  └─ notifications/           # 通知抽象（由 shell 实现展示）
│  ├─ ui-kit/                     # ConsoleView、HexView、StatusItem、Theme
│  ├─ shell/
│  │  ├─ mainwindow/
│  │  ├─ views/                   # 会话资源管理器、会话控制台、插件管理器
│  │  ├─ palette/                 # 命令面板
│  │  ├─ terminal/                # 内置命令终端视图
│  │  └─ theme/                   # 主题加载、图标、字体
│  ├─ app/
│  └─ cli/
├─ plugins/
│  ├─ builtin/                    # 会话资源管理器、控制台、终端（作为内置插件）
│  └─ examples/hello-world/       # 插件模板 + 契约测试
├─ tests/
│  ├─ unit/                       # 解析器、状态机、设置合并、enablement
│  ├─ integration/                # 虚拟端口 + 多会话 + 命令链路
│  └─ plugin-contract/            # 插件契约测试工具
├─ resources/{icons,themes,i18n}
└─ docs/architecture/
```

## 7. 依赖规则

**允许**
- 上层依赖下层：`app → shell → services → serial → api → foundation`；
- 插件依赖 `xcom_api`（+ 可选 `xcom_ui_kit`）；
- 任何模块依赖 `foundation` 的日志与错误类型。

**禁止**
- `api`、`serial`、`services` 出现任何 `#include <QWidget>`、`<QMainWindow>`、`<QAction>`；
- `api` 依赖 `foundation` / `serial` / `services`（契约必须自包含）；
- `serial` 调用 `services`（引擎不感知命令、设置、插件）；
- 插件链接或 include `src/` 下任何内部头文件；
- 模块间循环依赖；同层横向调用（`serial` 内部子模块除外）。

**头文件规则**：公共头只放在 `src/api/include/xcom/api/`；其余头文件视为内部实现，不进安装包。

## 8. 关键流程

### 8.1 启动

```
main()
 ├─ 单实例检查（QLocalServer），已有实例则唤醒它并退出
 ├─ 日志 / 崩溃处理器 / 命令行参数解析
 ├─ foundation + SettingsService（默认值 ← 用户 settings.json）
 ├─ EventBus → SerialService（端口枚举）→ SessionService
 ├─ CommandRegistry / ContextKeys / Keybindings / TaskService
 ├─ PluginHost：扫描 manifest（不加载 DLL）→ 依赖排序 → 生成激活计划
 ├─ Shell：MainWindow、主题、布局、注册核心视图与命令
 ├─ 激活 onStartup 插件（内部插件通常只有 2～3 个）
 ├─ 恢复工作区会话（可选，失败不阻塞启动）
 └─ app.exec()
```

### 8.2 打开一个会话

```
命令 serial.session.open（参数：port / baud / ...）
  → CommandRegistry 校验参数与 enablement
  → SessionService.open(descriptor)
      → 端口独占检查（同名端口只允许一个会话）
      → 创建 SerialSession + SessionWorker，启动 I/O 线程
      → worker: QSerialPort.open + applyConfig
  → state = Open，发布事件 session/opened
  → Shell：新建 SessionConsoleView 标签页并设为活动会话
  → 插件：按需订阅该会话的 sink，挂接自己的解析器
```

### 8.3 命令派发（多入口合一）

```
菜单 / 快捷键 / 命令面板 / 内置终端 / CLI / 脚本 / 插件 API
                     │
                     ▼
        CommandRegistry.execute(invocation)
          ├─ 参数 schema 校验 + 默认值填充
          ├─ enablement（上下文键）判定
          ├─ 调用 ICommandHandler::execute（可带进度与取消）
          └─ CommandResult → 按来源呈现：
                UI     → 状态栏 / 通知
                Palette→ 提示条
                CLI    → stdout(JSON) / stderr + 退出码
                Script → resolve / reject
```

### 8.4 插件激活

```
发现(manifest) → 兼容性校验 → 依赖排序 → QPluginLoader.load
   → initialize(ctx)：只注册贡献（命令/视图/设置/解析器），必须 < 10 ms
   → 首个 activationEvent 命中 → activate()：创建资源
   → deactivate()：释放资源，保留注册
   → 退出：shutdown()，逆序释放；DLL 不卸载（Qt 限制）
```

## 9. 横切规则

### 9.1 线程与所有权

| 线程 | 拥有者 | 允许做的事 | 禁止做的事 |
|---|---|---|---|
| 主 / UI 线程 | `QApplication` | 全部服务、插件 `initialize/activate`、视图、命令、设置读写 | 阻塞 I/O、`sleep`、大文件同步读写 |
| 会话 I/O 线程（每会话一条） | `SerialSession` | `QSerialPort` 读写、时间戳、调用 sink | 创建 QWidget、访问设置/命令注册表、发通知 |
| 录制线程 | Recorder | 顺序写文件、轮转 | 回调插件代码 |
| 任务线程池 | TaskService | 长任务、批量解析、导出 | 创建 QWidget、直接改 UI |

所有权：`QWidget` 与 `QObject` 一律由宿主父对象管理；插件创建的视图在 deactivate 时被宿主销毁；跨线程只传值（`QByteArray` 隐式共享，读时不再修改）。

### 9.2 错误模型

- 统一 `Result<T>` 与 `Error{ code, message, details }`，错误码见 [serial-core.md](serial-core.md#12-错误模型)；
- **接口内不抛异常**，插件内部的异常必须自行捕获并转成错误返回；
- 每个错误带归属：模块名（`serial.session`）或插件 id（`plugin:com.example.modbus`）；
- 错误呈现三档：状态栏（瞬时）、通知（需要用户知道）、日志（诊断）。

### 9.3 日志与诊断

- `QLoggingCategory`：`xcom.serial`、`xcom.plugin.<id>`、`xcom.command`、`xcom.ui`；
- 文件日志：`%LOCALAPPDATA%/X-COM/logs/xcom-YYYYMMDD.log`，10 MB 轮转，保留 5 份；
- 会话数据日志与程序日志分离（前者是用户数据，后者是诊断信息）；
- `帮助 → 诊断信息`：版本、Qt、插件列表与状态、活动会话、最近错误，一键复制。

### 9.4 性能预算

| 场景 | 预算 |
|---|---|
| 冷启动（无第三方插件） | < 300 ms |
| 插件扫描（含缓存） | < 10 ms/插件 |
| 命令面板过滤 | < 16 ms/次输入 |
| UI 数据刷新 | 30 Hz 合并批量，单次追加 ≤ 256 KB |
| 每会话缓冲上限 | 8 MB（可配置），超出丢最旧并计数 |
| 内存基线 | < 150 MB（含 Qt） |

### 9.5 安全与信任

- 原生插件等价于宿主进程内的任意代码，**没有沙箱**，安装时必须提示；
- 工作区自带插件默认不加载，需"信任此工作区"；
- `--safe-mode` 只加载内置插件，用于排障；
- 插件声明 `capabilities`（读写会话、网络、文件），仅用于展示与审计，不构成强制隔离；
- 后续版本可将"分析类插件"移出进程（见 ADR-003），但 UI 插件必须留在进程内。

## 10. 架构决策记录（ADR）

| 编号 | 决策 | 理由 | 替代方案与代价 |
|---|---|---|---|
| ADR-001 | 构建系统迁移到 CMake | Qt6 官方推荐；插件目标、安装布局、CI 都更清晰 | 保留 qmake：插件与多目标构建难维护 |
| ADR-002 | 原生 C++ 插件为主，JS 脚本插件为辅 | 串口/协议场景需要性能与完整 UI 能力 | 纯脚本插件：能力弱；纯进程外：无法共享控件 |
| ADR-003 | v1 插件进程内加载 | 简单、可直接贡献 QWidget；插件生态初期以自研为主 | 进程外插件：崩溃隔离好，但 UI 无法嵌入 |
| ADR-004 | `api` 为独立共享库，接口纯虚 + Qt 类型 | 跨 DLL 的 vtable/RTTI 必须唯一；Qt 类型天然稳定 | 头文件-only 接口：仍共享 ABI 但无法保证类型身份 |
| ADR-005 | 每个会话一条 I/O 线程 | 串口读写阻塞不拖累 UI；多端口互不影响 | 单 I/O 线程：一个端口卡死拖累全部 |
| ADR-006 | 命令注册表是唯一操作入口 | 菜单/面板/CLI/脚本天然一致，易测试、易录制回放 | 各入口各自实现：行为漂移、测试困难 |
| ADR-007 | 懒激活（activationEvents） | 启动快；插件多了也不拖慢 | 全部启动即激活：启动时间随插件线性增长 |
| ADR-008 | 停靠用 `QDockWidget` + 中央标签页，不引入第三方停靠库 | 无额外许可证与维护负担；视图抽象隔离，将来可替换 | 引入 ADS：浮动/分屏更强，但 LGPL 约束与升级成本 |
| ADR-009 | 配置分三层：内置默认 / 用户 / 工作区 | 团队共享工作区配置，个人偏好不被覆盖 | 单一 QSettings：无法区分作用域 |
| ADR-010 | 先用 3 个内置插件验证再冻结 API v1 | 避免把错误抽象固化进生态 | 直接开放：返工成本高 |

## 11. 里程碑

| 阶段 | 主题 | 结果 |
|---|---|---|
| M0 | 拆核 | 串口逻辑脱离 MainWindow，可用虚拟端口测试 |
| M1 | 多会话外壳 | 多标签会话、会话资源管理器、布局持久化 |
| M2 | 命令化 | 全部操作变命令、命令面板、键位 |
| M3 | 插件 v1 | 插件宿主 + API v1 + 示例插件 + 插件管理器 |
| M4 | 配置与 CLI | settings/workspace、无界面 CLI、内置终端 |
| M5 | 脚本与硬化 | JS 脚本、诊断、安全模式、性能与稳定性 |

详见 [roadmap.md](roadmap.md)。

## 12. 文档索引

| 文档 | 内容 |
|---|---|
| [plugin-system.md](plugin-system.md) | 插件形态、清单、发现、生命周期、贡献点、兼容与安全 |
| [serial-core.md](serial-core.md) | 多会话模型、线程、数据管线、缓冲背压、录制回放 |
| [shell-ui-command.md](shell-ui-command.md) | 窗口布局、视图系统、命令/键位/面板、设置与工作区 |
| [cli-scripting.md](cli-scripting.md) | 启动参数、无界面 CLI、内置终端、脚本 API |
| [api-contracts.md](api-contracts.md) | 接口契约、数据类型、ID 规范、版本兼容策略 |
| [roadmap.md](roadmap.md) | 分阶段实施计划、验收标准、风险、待确认问题 |

## 13. 术语表

| 术语 | 含义 |
|---|---|
| 宿主 Host / Shell | X-COM 可执行程序本身，负责窗口、服务、插件生命周期 |
| 会话 Session | 一次串口连接的运行时对象，拥有独立 I/O 线程与数据缓冲 |
| 命令 Command | 带 id、参数 schema、可被任意入口调用的原子操作 |
| 贡献点 Contribution | 插件向宿主注册的扩展点，如命令、视图、设置项、解析器 |
| 激活事件 ActivationEvent | 决定插件何时真正加载资源的条件 |
| 上下文键 ContextKey | 描述当前 UI/会话状态的布尔或字符串值，用于命令 enablement |
| 订阅 Subscription | 注册时返回的撤销令牌，deactivate 时统一回收 |
| 工作区 Workspace | 一个目录，包含会话列表、布局、插件启用状态与工作区设置 |
| 传输 Transport | 字节流来源抽象：真实串口、回环、回放、TCP 桥、脚本模拟 |
