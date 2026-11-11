#pragma once

#include "xcom/api/sessions.h"
#include "xcom/api/ui.h"

#include <QWidget>
#include <QList>
#include <QPointer>
#include <QVBoxLayout>

QT_BEGIN_NAMESPACE
class QSplitter;
class QTabWidget;
class QToolButton;
class QListWidget;
class QTimer;
QT_END_NAMESPACE

namespace xcom {

class CommandLineView;
class ConsoleView;
class FrameParserView;

// 一个会话 = 一个工作区：上方控制台，下方各功能按独立标签页显隐。
// 会话标签页承载它，切换标签页不会重建视图，历史与解析状态得以保留。
//
// 工作区顶部有一条面板栏，三个面板各自独立开关：
//   [控制台] [底部面板] [命令解析] [浮出窗口]
// 任何面板被隐藏后都能从这条栏（或「视图」菜单）再次打开，避免出现「点了没反应」的死角。
class SessionWorkspace : public QWidget
{
    Q_OBJECT

public:
    SessionWorkspace(ISession *session, IViewHost *host, QWidget *parent = nullptr);
    ~SessionWorkspace() override;

    QString sessionId() const;

    ConsoleView *console() const { return m_console; }
    CommandLineView *commandLine() const { return m_commandLine; }
    FrameParserView *parser() const { return m_parser; }

    void showConsole();
    void showSerialConfiguration();
    void showCommandLine();
    void showParser();
    void showConsolePage(QWidget *page, const QString &title);
    void showBottomPage(QWidget *page, const QString &title);
    void hideBottomPage(QWidget *page);
    void focusSend();

    // 控制台区（接收区 + 发送区）：隐藏后只留下方标签页。
    bool isConsoleVisible() const;
    void setConsoleVisible(bool visible);

    // 串口命令行标签页；不会联动命令解析和串口配置页。
    bool isBottomVisible() const;
    void setBottomVisible(bool visible);

    // 只控制「命令解析」这一页：关闭时把标签页移除，重新打开时插回原位置。
    bool isParserVisible() const;
    void setParserVisible(bool visible);

    // 浮动：把控制台移到独立窗口；关闭该窗口或再次切换会嵌回原位。
    bool isConsoleFloating() const;
    void setConsoleFloating(bool floating);
    void raiseFloatingConsoleWithoutActivation();

signals:
    void consoleRequested(const QString &sessionId);
    void serialConfigurationToggled(bool visible);
    // 面板栏上的开关发生变化时发出，供主窗口把布局写进设置。
    void panelVisibilityChanged();

private:
    void buildUi();
    QWidget *createPanelBar();
    void syncPanelBar();
    void applyConsoleParent();

    ISession *m_session = nullptr;
    IViewHost *m_host = nullptr;

    ConsoleView *m_console = nullptr;
    CommandLineView *m_commandLine = nullptr;
    FrameParserView *m_parser = nullptr;

    QSplitter *m_splitter = nullptr;
    QSplitter *m_contentSplitter = nullptr;
    QTabWidget *m_consoleTabs = nullptr;
    QWidget *m_commandShelf = nullptr;
    QWidget *m_rowsContainer = nullptr;
    QVBoxLayout *m_rowsLayout = nullptr;
    QList<QWidget *> m_commandRows;
    QTimer *m_runTimer = nullptr;
    int m_runIndex = 0;
    int m_loopRemaining = 1;
    QWidget *m_consoleHost = nullptr;
    QTabWidget *m_bottomTabs = nullptr;

    QToolButton *m_consoleToggle = nullptr;
    QToolButton *m_serialConfigToggle = nullptr;
    QToolButton *m_bottomToggle = nullptr;
    QToolButton *m_parserToggle = nullptr;
    QToolButton *m_floatToggle = nullptr;
    QToolButton *m_commandsToggle = nullptr;
    QToolButton *m_pinToggle = nullptr;
    QPointer<QWidget> m_serialConfigPage;

    QWidget *m_floatWindow = nullptr;

    bool m_consoleVisible = true;
    bool m_commandsVisible = true;
    bool m_floatPinned = false;
};

} // namespace xcom
