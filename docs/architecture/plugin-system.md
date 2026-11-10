# 插件系统设计

> 本文定义 X-COM 的扩展模型：插件长什么样、怎么被发现、如何加载、能扩展什么、出错怎么办。
> 接口签名见 [api-contracts.md](api-contracts.md)，宿主启动流程见 [README.md](README.md#8-关键流程)。

## 1. 插件形态

| 形态 | 技术 | 能力 | 典型用途 |
|---|---|---|---|
| native（主力） | `QPluginLoader` + C++ 共享库 | 全部 API：命令、视图、解析器、设置、导出、主题 | 协议支持、复杂面板、性能敏感处理 |
| script（v2） | `QJSEngine` + `manifest.json` | 命令、设置、输出面板、解析器回调；不能直接构造任意 QWidget | 用户现场写的小工具、批量脚本 |
| builtin | 与 native 相同，随安装包发布 | 全部 | 内置功能，可被用户禁用，用于验证 API |

强制约定：内置功能也走插件 API（"自举"），否则 API 会在真实场景中腐化。

## 2. 插件包结构

```
plugins/com.example.modbus/
├─ manifest.json          # 元数据、兼容性、激活事件、权限
├─ xcom-modbus.dll        # native 入口（script 插件无此文件）
├─ main.js                # script 入口（仅 script 插件）
└─ resources/
   ├─ icon.svg
   └─ translations/
```

安装位置（按优先级从低到高）：

| 位置 | 路径 | 默认 |
|---|---|---|
| 内置 | `<install>/plugins` | 启用 |
| 用户 | `%APPDATA%/X-COM/plugins` | 启用 |
| 工作区 | `<workspace>/.xcom/plugins` | **禁用**，需信任工作区后才可选启用 |

同 id 同时出现在多处时，高优先级覆盖低优先级；同优先级同 id 视为冲突，保留版本号更高者并记录警告。

## 3. manifest 字段

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `id` | string | 是 | 反向域名，小写：`com.example.modbus`；全局唯一，也是设置键与日志分类的前缀 |
| `name` | string | 是 | 显示名（可本地化：`name.zh_CN`） |
| `version` | semver | 是 | 插件版本 |
| `apiVersion` | semver range | 是 | 依赖的宿主 API 版本，如 `^1.0` |
| `kind` | enum | 是 | `native` / `script` |
| `entry` | string | native 必填 | DLL 文件名（相对插件目录） |
| `description` | string | 否 | 一句话说明，用于插件管理器 |
| `author` / `license` / `homepage` | string | 否 | 展示与合规 |
| `icon` | string | 否 | 相对路径，SVG 优先 |
| `engines.host` | semver range | 是 | 宿主最低版本，如 `>=0.4.0` |
| `build` | object | native 必填 | 构建指纹：`qt`、`compiler`、`arch`、`apiHash` |
| `dependencies` | array | 否 | `{ id, version, optional }`，用于排序与失败级联 |
| `activationEvents` | array | 是 | 见第 7 节；`onStartup` 之外尽量惰性 |
| `capabilities` | array | 否 | 权限声明：`session.read` / `session.write` / `network` / `filesystem.user` / `filesystem.plugin-data` |
| `contributes` | object | 否 | **仅元数据**：命令标题、视图标题、设置大纲，供插件管理器与命令面板在未激活时展示 |
| `headless` | bool | 否 | 是否允许在 `xcom-cli` 中加载（默认 false） |

### 3.1 示例

```json
{
  "id": "com.example.modbus",
  "name": "Modbus RTU 工具",
  "name.zh_CN": "Modbus RTU 工具",
  "version": "1.2.0",
  "apiVersion": "^1.0",
  "kind": "native",
  "entry": "xcom-modbus.dll",
  "description": "Modbus RTU 帧解析、CRC 校验与读写命令",
  "author": "Example",
  "license": "MIT",
  "icon": "resources/icon.svg",
  "engines": { "host": ">=0.4.0" },
  "build": {
    "qt": "6.7.3",
    "compiler": "msvc2022",
    "arch": "x86_64",
    "apiHash": "b41f9c2e"
  },
  "dependencies": [],
  "activationEvents": ["onCommand:modbus.read", "onView:modbus.panel"],
  "capabilities": ["session.read", "session.write", "filesystem.plugin-data"],
  "contributes": {
    "commands": [
      { "id": "modbus.read", "title": "Modbus: 读取保持寄存器", "category": "Modbus" }
    ],
    "views": [
      { "id": "modbus.panel", "title": "Modbus 寄存器", "location": "panel" }
    ]
  }
}
```

`contributes` 只是"未激活时的说明书"：真正注册发生在 `initialize()`，运行时注册是唯一事实来源；二者不一致时以运行时为准并记录警告。

## 4. 发现与加载流程

```
启动
 ├─ 扫描三个目录，读取 manifest.json，校验 JSON schema
 ├─ 读取 plugin-cache.json（路径 + mtime + size + manifest hash）
 │    命中且未变化 → 跳过解析
 ├─ 合并同 id（优先级规则），检测版本冲突
 ├─ 兼容性校验（不加载 DLL，只看 manifest + 宿主 buildStamp）
 ├─ 依赖解析：拓扑排序、循环检测、可选依赖缺失降级
 └─ 生成激活计划，写入插件管理器数据模型
```

加载时机由激活事件触发，加载步骤：

1. `QPluginLoader::instance()` 装载 DLL，失败记录 `errorString()`，状态 `Failed`；
2. `qobject_cast<IPlugin*>` 校验，失败状态 `Incompatible`；
3. 创建 `PluginContext`（注入服务、插件数据目录、日志分类）；
4. `initialize(ctx)` 注册贡献；失败则回滚已注册项，状态 `Failed`；
5. 首个激活事件命中后 `activate()`；
6. 退出时按逆序 `shutdown()`。

## 5. 兼容性校验

| 维度 | 规则 | 不通过时 |
|---|---|---|
| API 版本 | 主版本必须相等；次版本 ≤ 宿主次版本 | `Incompatible: 需要 API 1.x` |
| 宿主版本 | 满足 `engines.host` | `Incompatible: 需要宿主 >= x.y` |
| Qt 版本 | `build.qt` 与宿主完全一致（6.7.3 ≠ 6.7.2） | `Incompatible: Qt 版本不一致，请重编` |
| 编译器 / 架构 | `build.compiler`、`build.arch` 一致（MSVC 与 MinGW 不可混用） | `Incompatible: 编译器不一致` |
| 接口指纹 | `build.apiHash` 等于宿主编译期生成的哈希 | `Incompatible: API 已变更` |

Qt 补丁版本升级也要求重编插件，这是官方 ABI 策略决定的，不尝试兼容；发布时把 Qt 运行时连同插件一起分发。

## 6. 依赖与激活顺序

- `dependencies` 只允许依赖其他插件 id（宿主内置服务不写成依赖，直接使用）；
- 拓扑排序决定 `initialize` 顺序（被依赖者先），`shutdown` 反向；
- 循环依赖：全部标记 `Failed: 循环依赖`，不加载任何一方；
- 必需依赖失败或不兼容：本插件跳过，状态 `Disabled (dependency)`；
- 可选依赖缺失：正常加载，插件可通过 `IPluginContext::pluginInfo(id)` 查询后降级。

## 7. 生命周期

```
Discovered ─► Resolved ─► Loaded ─► Initialized ─► Activated
     │            │          │            │             │
     └────────────┴──────────┴────────────┴─────────────┴──► Failed / Disabled
                                             Activated ─► Deactivated ─► Shutdown
```

| 状态 | 含义 | 允许的操作 |
|---|---|---|
| Discovered | manifest 已读到 | 读取元数据 |
| Resolved | 兼容性与依赖通过 | 展示为"可启用" |
| Loaded | DLL 已映射，接口转换成功 | 调用 `initialize` |
| Initialized | 贡献已注册 | 等待激活事件；可注册更多贡献 |
| Activated | 资源已创建 | 正常提供服务 |
| Deactivated | 资源已释放，注册保留 | 可再次激活 |
| Failed | 任一阶段失败 | 只能禁用/卸载；错误进插件管理器 |
| Disabled | 用户或策略禁用 | 不加载 |

`initialize()` 必须轻量：只注册命令、视图工厂、设置项、解析器，目标 < 10 ms；耗时操作（读文件、建索引、连设备）放到 `activate()`。

`deactivate()` 释放运行资源但保留注册，便于"禁用插件"时即时生效；`shutdown()` 释放全部，仅在退出或显式卸载时调用。

DLL 卸载：Qt 对已加载插件不支持安全卸载，**升级插件必须重启宿主**；插件管理器提供"下次启动生效"。

## 8. 激活事件

| 事件 | 触发时机 | 建议 |
|---|---|---|
| `onStartup` | 宿主启动完成 | 仅核心内置插件使用 |
| `onCommand:<id>` | 命令首次出现在菜单/面板/被调用前 | 绝大多数插件的首选 |
| `onView:<id>` | 视图首次需要显示 | 带面板的插件 |
| `onSessionOpen` | 任意会话打开 | 解析类插件 |
| `onSessionType:<type>` | 特定类型会话打开（serial/tcp…） | 协议插件 |
| `onWorkspaceContains:<glob>` | 工作区含匹配文件 | 项目类插件 |
| `*` | 任意激活请求 | 禁止，除调试 |

未激活插件的命令仍可在命令面板被搜索到（来自 `contributes` 元数据），执行时先激活插件。

## 9. 贡献点

| 贡献点 | 注册接口 | 生命周期 | 线程 |
|---|---|---|---|
| 命令 | `ICommandRegistry::registerCommand` | 随订阅撤销 | 主线程执行（可声明后台执行） |
| 菜单 / 工具栏项 | `IMenuRegistry::addItem` | 同上 | 主线程 |
| 快捷键 | `IKeybindingRegistry::addBinding` | 同上 | 主线程 |
| 视图（侧边栏/底部/编辑器标签） | `IViewRegistry::registerView` | 同上；视图实例在首次显示时创建 | 主线程 |
| 状态栏项 | `IStatusBarRegistry::addItem` | 同上 | 主线程 |
| 设置项 | `ISettingsRegistry::define` | 同上 | 任意（只读快照） |
| 数据观察者（sink） | `ISession::subscribe` | 随会话关闭或订阅撤销 | **I/O 线程**，禁止阻塞 |
| 分帧 / 编解码器 | `IParserRegistry::register` | 随订阅撤销 | I/O 线程（纯函数） |
| 导出器 | `IExporterRegistry::register` | 同上 | 任务线程池 |
| 主题 | `IThemeRegistry::register` | 同上 | 主线程 |
| 会话类型（v2） | `ISessionTypeRegistry::register` | 同上 | 主线程 |
| CLI 子命令（headless） | `ICliRegistry::register` | 同上 | CLI 主线程 |
| 诊断提供者 | `IDiagnosticsRegistry::register` | 同上 | 主线程（收集时） |

## 10. 注册、订阅与冲突

- 所有注册函数返回 `ISubscription`（move-only）。插件可主动 `unsubscribe()`；宿主在 `deactivate()` 时统一撤销该插件全部订阅（按注册逆序），防止泄漏。
- 撤销必须幂等，且不得抛异常。
- 冲突规则：
  - **命令 id** 全局唯一。冲突 → 后注册者 `initialize` 失败，插件进入 `Failed`，错误信息指出冲突 id；
  - **视图 id**、**设置键** 同理；
  - **快捷键** 允许冲突，按"用户覆盖 > 插件 > 内置"顺序生效，冲突在设置的键位页列出；
  - **菜单项** 允许重复，按 `menu + group + order` 排序，完全相同时按注册时间。
- 设置键强制带插件命名空间：`com.example.modbus.pollInterval`，注册时校验前缀。

## 11. 线程与所有权

| 回调 | 所在线程 | 约定 |
|---|---|---|
| `IPlugin::initialize / activate / deactivate / shutdown` | 主线程 | 不得阻塞 > 50 ms（activate 可拆后台任务） |
| 命令 handler | 主线程（默认）/ 任务线程池（`background`） | 不得直接改 UI 之外的共享状态 |
| `IViewFactory::create` | 主线程 | 返回的 widget 由宿主接管父子关系 |
| `ISessionDataSink::onData` | 会话 I/O 线程 | 只读数据、不分配大对象、不调用服务、不碰 UI |
| 解析器 `parse` | I/O 线程 | 纯函数，输入输出都在参数里 |
| 导出器 | 任务线程池 | 通过进度接口回报 |

所有权规则：

- 宿主持有插件对象；插件对象由 `QPluginLoader` 创建，宿主不 `delete`；
- 视图 widget 由宿主销毁，destroyed 时通知插件（`IViewHost::onViewDestroyed`）；
- 插件不得持有宿主对象的裸指针超过回调范围，长期引用一律存 `Subscription` 或 id；
- 跨线程传参使用值类型（`QByteArray` 隐式共享、`QString`），不传指向栈的指针。

## 12. 错误隔离、安全与信任

| 措施 | 说明 |
|---|---|
| 初始化事务 | `initialize` 中的注册全部记录，失败时回滚，不留半成品贡献 |
| 异常兜底 | 宿主在所有插件回调外层捕获 C++ 异常（Qt 不保证跨 DLL 异常安全），转 `Failed` 并禁用 |
| 心跳与超时 | 命令执行超时（默认 30 s，可配置）触发取消，不强行杀线程 |
| 安全模式 | `xcom --safe-mode`：只加载内置插件，用于排查第三方插件问题 |
| 工作区信任 | 工作区插件默认不加载，首次启用弹窗确认 |
| 签名（可选） | 发布版对 DLL 做 Authenticode 签名，插件管理器展示签名者；未签名允许但标注 |
| 能力审计 | `capabilities` 在插件详情页展示；不构成强制隔离，文档明确说明 |

插件管理器需要展示：id、版本、作者、状态（含失败原因）、依赖、能力、加载耗时、贡献数量、日志入口、启用/禁用、打开所在目录。

## 13. 内置插件规划

| 插件 id | 形态 | 作用 | 验证的 API |
|---|---|---|---|
| `com.xcom.sessions` | builtin | 会话资源管理器（树、状态、右键菜单） | 视图、命令、上下文键 |
| `com.xcom.console` | builtin | 串口控制台视图（收发、HEX、日志） | 编辑器视图、设置、sink |
| `com.xcom.terminal` | builtin | 内置命令终端 | 命令、输出通道 |
| `com.xcom.tools.checksum` | builtin | CRC/校验计算工具 | 底部面板、命令参数 |
| `com.xcom.protocol.modbus` | example | Modbus RTU 帧解析与读写 | sink、解析器、通知 |
| `com.xcom.exporter.csv` | example | 会话数据导出 CSV | 导出器、任务进度 |

内置插件放 `plugins/builtin/`，与第三方使用相同的 manifest 与加载路径；区别仅在于签名与默认启用。

## 14. 插件模板与契约测试

`plugins/examples/hello-world/` 作为模板，包含：

```
hello-world/
├─ manifest.json
├─ CMakeLists.txt
├─ src/plugin.cpp            # IPlugin 实现
├─ src/hello-command.cpp     # 一个命令
├─ src/hello-view.cpp        # 一个侧边栏视图
└─ tests/contract.cpp        # 契约测试
```

契约测试（`xcom_plugin_testkit`）自动验证：

1. manifest 可解析、字段合法、id 与目录一致；
2. `initialize` 后注册项数量与类型符合预期；
3. `deactivate` 后订阅全部撤销，视图关闭，无悬挂对象（`QPointer` 检查）；
4. 重复 `activate/deactivate` 不崩溃、不重复注册；
5. 命令参数 schema 与 handler 行为一致；
6. 卸载（进程退出）无内存泄漏（ASAN/Debug 堆检查）。

## 15. 反模式（代码评审必查）

- 插件直接 `#include "../../services/..."` 或链接内部库；
- 在 `initialize` 里建窗口、连设备、读大文件；
- 在 I/O 线程回调里发通知、读写设置、创建 QWidget；
- 插件之间直接互相调用（应通过命令、事件或公开 API）；
- 用全局单例替代 `IPluginContext`；
- 命令 handler 阻塞超过 100 ms 且不声明 `background`；
- 在 `deactivate` 后仍持有视图指针或信号连接；
- 依赖 `QString` 以外的 STL 类型跨越 API 边界（`std::string`、`std::vector` 禁止出现在接口签名）。
