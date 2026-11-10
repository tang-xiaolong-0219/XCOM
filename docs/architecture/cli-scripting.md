# 命令行、终端与脚本设计

> 目标：GUI、内置终端、无界面 CLI、脚本四种使用方式共用同一套命令与同一份参数定义。
> 命令系统本身见 [shell-ui-command.md](shell-ui-command.md#7-命令系统)。

## 1. 四个入口，一条链路

```
GUI 菜单/快捷键 ─┐
命令面板        ─┤
内置终端        ─┼─► CommandRegistry.execute → ICommandHandler → 结果按来源呈现
无界面 CLI      ─┤
脚本 (QJSEngine)─┘
```

任何新功能只要实现为命令，就自动在四个入口可用；任何入口都不得"绕过命令"直接调用内部对象。

## 2. GUI 启动参数

| 参数 | 说明 |
|---|---|
| `--workspace <dir>` | 打开指定工作区（默认恢复上次） |
| `--port <name> --baud <n> --open` | 启动后立即打开一个会话 |
| `--session <name>` | 启动后聚焦指定会话 |
| `--script <file.js>` | 界面就绪后执行脚本 |
| `--safe-mode` | 只加载内置插件 |
| `--headless` | 转为无界面模式（等同 `xcom-cli`） |
| `--log-level <debug\|info\|warn\|error>` | 日志级别 |
| `--profile <name>` | 独立配置目录，便于测试与多套环境 |

单实例：同一 profile 只允许一个 GUI 实例；第二个实例把参数（打开端口、脚本）通过本地套接字转交给已运行实例后退出。

## 3. 无界面 CLI（`xcom-cli.exe`）

只加载 `headless: true` 的插件，不初始化 QtWidgets 与主窗口。

| 命令 | 作用 | 主要参数 |
|---|---|---|
| `ports` | 列出可用串口 | `--json` |
| `sessions` | 列出当前进程/工作区会话定义 | `--json` |
| `send` | 打开会话、发送、关闭（一次性） | `--session` `--text` `--hex` `--file` `--newline` `--wait-ack` |
| `monitor` | 打开会话，采集一段时间或直到 Ctrl+C | `--session` `--duration` `--out` `--hex` `--json` |
| `run` | 执行脚本 | `<script.js>` `--arg k=v` |
| `plugins` | 列出/启用/禁用插件 | `list\|enable\|disable` `--json` |
| `doctor` | 环境诊断（Qt 版本、端口、插件、权限） | `--json` |

示例（命令用法，不是代码）：

```
xcom-cli ports --json
xcom-cli send --session COM3 --text "AT" --newline --wait-ack 500ms
xcom-cli monitor --session COM3 --duration 10s --hex --out rx.log
xcom-cli run scripts/flash.js --arg firmware=app.bin
xcom-cli plugins list --json
```

设计约束：

- 一次性命令默认"打开→操作→关闭"，不留下后台进程；`monitor` 在时长内保持会话；
- 需要长驻服务的场景（被其他程序远程调用）放到后续 `xcom-cli daemon` + 本地套接字，v1 不做；
- 所有命令都支持 `--json`：每行一个 JSON 对象（JSONL），便于管道与 CI；
- 人类可读输出走 stderr，机器可读输出走 stdout，二者不混。

## 4. 退出码

| 码 | 含义 |
|---|---|
| 0 | 成功 |
| 1 | 通用失败 |
| 2 | 参数/用法错误 |
| 3 | 串口相关错误（端口不存在、被占用、权限） |
| 4 | 脚本错误（语法、运行时、超时） |
| 5 | 插件错误（不兼容、初始化失败、被安全模式禁用） |

`--json` 输出统一带 `ok`、`error.code`、`error.message`、`durationMs`。

## 5. 内置命令终端

底部面板的 `terminal` 视图，不是系统 shell，而是命令 REPL：

```
[设备A · COM3 115200] > serial.session.open COM5 --baud 9600 --name 传感器
已打开 传感器（COM5 · 9600 8N1）
[设备A · COM3 115200] > use 传感器
当前会话：传感器（COM5）
[传感器 · COM5 9600] > send --hex "01 03 00 00 00 0A C5 CD"
[传感器 · COM5 9600] > send --text "AT" --newline
[传感器 · COM5 9600] > monitor --hex
... Ctrl+C 停止
```

| 能力 | 规则 |
|---|---|
| 提示符 | 显示当前活动会话与关键参数；无会话时显示 `[无活动会话] >` |
| 别名 | `open` / `close` / `send` / `use` / `sessions` / `log` / `help` / `plugins` 映射到规范命令 |
| 补全 | `Tab` 补全命令 id、参数名、枚举值、会话名 |
| 历史 | 每工作区保存 500 条，`↑/↓` 浏览，`Ctrl+R` 搜索（v2） |
| 多行 | 行尾 `\` 续行；粘贴多行自动识别为一条 |
| 取消 | `Ctrl+C` 取消正在执行的命令（通过 `ICommandProgress`） |
| 帮助 | `help`、`help <command>`、`help --json` 由参数 schema 自动生成 |
| 输出 | 流式输出，长输出自动折叠，可"在输出面板中打开" |
| JS 模式 | `js` 进入持久 REPL，提示符变为 `js>`，`exit` 返回命令模式 |

终端视图本身是插件（`com.xcom.terminal`），但使用与 CLI 相同的 `CommandExecutor` 与 tokenizer。

## 6. 命令文本语法

```
<command-id> [--arg <value>]... [--flag] [--no-flag] [positional...]
```

- 命令 id 即注册 id，如 `serial.session.open`；也接受唯一前缀简写（`serial.open` 若唯一则解析）；
- 字符串含空格用双引号，内部 `\"` 转义；单引号字符串不解析转义；
- `--hex` 值允许空格、逗号、`0x` 前缀；
- 布尔参数：`--hex` 为真，`--no-hex` 为假；
- 位置参数按命令声明的顺序绑定，便于 `send COM3 "AT"` 这类简写；
- 解析失败给出精确位置与可选值列表，退出码 2。

Tokenizer 与参数绑定由 `CommandRegistry` 统一提供，终端与 CLI 不各自实现。

## 7. 脚本

### 7.1 运行环境

- Qt `QJSEngine`（ECMAScript 7），运行在**主线程**，禁止阻塞调用；
- 全局对象 `xcom`，按命名空间组织；所有可能耗时的操作返回 Promise；
- 脚本文件来自工作区时需"信任工作区"；来源与哈希记录在日志；
- 单脚本默认超时 5 分钟，可通过 `--timeout` 调整；超时触发取消，不 kill 线程。

### 7.2 API 映射

| 脚本 API | 对应能力 | 备注 |
|---|---|---|
| `xcom.sessions.open(options)` | `ISessionService::open` | 返回 `Promise<Session>`，options 同命令参数 |
| `xcom.sessions.list()` / `.active()` / `.find(ref)` | 会话查询 | 返回只读快照 |
| `session.send(data)` | 发送 | `string` 按当前编码，`Uint8Array`/`ArrayBuffer` 按原始字节 |
| `session.sendHex("AA 55")` | HEX 发送 | 便捷方法 |
| `session.on("data"\|"state"\|"error", cb)` | 订阅 | `data` 回调收到 `{ bytes, direction, timestampMs }` |
| `session.close()` | 关闭 | Promise |
| `xcom.commands.execute(id, args)` | 命令 | 返回 `Promise<result>`；脚本因此能调用一切功能 |
| `xcom.commands.list()` | 命令枚举 | 用于自动生成 UI/文档 |
| `xcom.settings.get/set(key)` | 设置 | 受命名空间限制，脚本只能改 `script.*` 与用户级值 |
| `xcom.events.on(topic, cb)` | 事件 | 低频率事件；热路径用 session 订阅 |
| `xcom.tasks.sleep(ms)` | 延时 | 返回 Promise，不阻塞线程 |
| `xcom.log.info/warn/error` | 日志 | 进 `xcom.script` 分类 |
| `xcom.args` | 启动参数 | `--arg k=v` 注入 |

### 7.3 错误与调试

- 语法错误：启动即失败，指出行列号；
- 运行时异常：未捕获异常 → 通知 + 日志 + 脚本终止；
- Promise reject：记录来源会话与调用栈摘要；
- 调试：`--log-level debug` 打印每次命令调用与返回值摘要；后续可接 QML 调试器（不承诺）。

### 7.4 安全边界

- 脚本默认没有文件、网络、进程权限；需要读写文件的场景通过命令（`serial.export`、`session.record`）完成；
- 工作区脚本首次运行弹窗确认并显示哈希；
- 脚本可调用的命令受 `workspaceTrusted` 与插件启用状态约束；
- 不在脚本层做"沙箱"承诺，权限声明与审计日志是主要手段。

## 8. 典型自动化场景

| 场景 | 方式 |
|---|---|
| 产线冒烟：上电→发 AT→校验回包→退出码 | `xcom-cli run smoke.js` |
| 抓包 10 分钟存盘 | `xcom-cli monitor --session COM3 --duration 10m --out rx.xlog` |
| 回归测试：脚本化设备模拟 + 断言 | TcpBridge 虚拟端口 + `monitor --json` + CI 断言 |
| 批量改参数 | 脚本遍历会话，`xcom.commands.execute("serial.session.reconfigure", ...)` |
| 固件升级 | 插件提供 `firmware.flash` 命令（分块发送 + 进度 + 取消），CLI/脚本复用 |

## 9. 插件与 CLI 的协作

- manifest `headless: true` 的插件才会在 CLI 中加载；UI 插件在 CLI 中被跳过且不报错；
- 插件可贡献 CLI 子命令（`ICliRegistry`），例如 `xcom-cli modbus read ...`，实现与 GUI 命令共用 handler；
- `xcom-cli doctor` 汇总：宿主版本、Qt 版本、插件兼容性、串口列表、配置路径、最近错误。

## 10. 实现要点（供落地时参考）

| 点 | 方案 |
|---|---|
| 入口复用 | `app` 与 `cli` 共用 `Bootstrap`，只是注入不同的 `IUiContext`（CLI 为 `nullptr`） |
| 参数解析 | 由 `CommandSpec.args` 生成解析器，不手写 argparse |
| 终端控件 | `ui-kit` 中的 `CommandConsole`，输入行 + 输出区 + 补全模型 |
| 历史存储 | 工作区 `.xcom/terminal-history.json`，最多 500 条，去重 |
| 进程模型 | CLI 用 `QCoreApplication`，绝不创建 `QApplication` |
| 超时与取消 | 统一走 `ICommandProgress`，CLI 收到 SIGINT 时触发取消并优雅退出 |
