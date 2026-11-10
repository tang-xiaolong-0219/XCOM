#include "ui-kit/session_workspace.h"

#include "ui-kit/command_line.h"
#include "ui-kit/console_view.h"
#include "ui-kit/frame_parser_view.h"

#include <QCloseEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QCheckBox>
#include <QSpinBox>
#include <QTimer>
#include <QScrollArea>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace xcom {
namespace {

void setWindowPinned(QWidget *window, bool pinned)
{
    if (!window) return;
#ifdef Q_OS_WIN
    const HWND handle = reinterpret_cast<HWND>(window->winId());
    SetWindowPos(handle, pinned ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#else
    const QRect geometry = window->geometry();
    const bool visible = window->isVisible();
    window->setWindowFlag(Qt::WindowStaysOnTopHint, pinned);
    window->setGeometry(geometry);
    if (visible) {
        window->show();
        window->raise();
    }
#endif
}

// 浮动控制台用的独立窗口：关闭时只收起，不销毁，避免丢掉接收区内容。
class ConsoleFloatWindow : public QWidget
{
public:
    explicit ConsoleFloatWindow(QWidget *parent = nullptr)
        : QWidget(parent, Qt::Window)
    {
        setWindowTitle(QStringLiteral("X-COM 控制台"));
        setAttribute(Qt::WA_DeleteOnClose, false);
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        setLayout(layout);
        m_contentLayout = layout;
        resize(880, 560);
    }

    QHBoxLayout *contentLayout() const
    {
        return m_contentLayout;
    }

    std::function<void()> onClosed;

private:
    QHBoxLayout *m_contentLayout = nullptr;

protected:
    void closeEvent(QCloseEvent *event) override
    {
        QWidget::closeEvent(event);
        if (onClosed) {
            onClosed();
        }
    }
    void keyPressEvent(QKeyEvent *event) override
    {
        // Esc 直接把控制台嵌回去，符合「浮窗随手关掉」的直觉。
        if (event->key() == Qt::Key_Escape) {
            close();
            return;
        }
        QWidget::keyPressEvent(event);
    }
};

} // namespace

SessionWorkspace::SessionWorkspace(ISession *session, IViewHost *host, QWidget *parent)
    : QWidget(parent)
    , m_session(session)
    , m_host(host)
{
    buildUi();
}

SessionWorkspace::~SessionWorkspace()
{
    if (m_floatWindow) {
        auto *window = static_cast<ConsoleFloatWindow *>(m_floatWindow);
        window->onClosed = nullptr;
        m_floatWindow = nullptr;
        delete window;
    }
}

void SessionWorkspace::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_console = new ConsoleView(m_session, m_host, this);

    m_commandLine = new CommandLineView(m_session, m_host, this);
    m_parser = new FrameParserView(m_session, m_host, this);

    m_commandShelf = new QWidget(this);
    auto *shelfLayout = new QVBoxLayout(m_commandShelf);
    shelfLayout->setContentsMargins(6, 6, 6, 6);
    auto *shelfBar = new QHBoxLayout;
    auto *addCommand = new QToolButton(m_commandShelf);
    addCommand->setText(QStringLiteral("＋ 行"));
    auto *titleSpacer = new QWidget(m_commandShelf);
    titleSpacer->setFixedWidth(addCommand->sizeHint().width());
    auto *commandTitle = new QLabel(QStringLiteral("命令行"), m_commandShelf);
    commandTitle->setAlignment(Qt::AlignCenter);
    shelfBar->addWidget(titleSpacer);
    shelfBar->addWidget(commandTitle, 1);
    shelfBar->addWidget(addCommand);
    auto *scroll = new QScrollArea(m_commandShelf);
    scroll->setWidgetResizable(true);
    m_rowsContainer = new QWidget(scroll);
    m_rowsLayout = new QVBoxLayout(m_rowsContainer);
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(3);
    scroll->setWidget(m_rowsContainer);
    auto *runBar = new QHBoxLayout;
    runBar->setSpacing(4);
    auto *selectAllButton = new QCheckBox(QStringLiteral("全选"), m_commandShelf);
    selectAllButton->setToolTip(QStringLiteral("勾选或取消勾选当前所有命令行"));
    auto *parseButton = new QPushButton(QStringLiteral("提取内容"), m_commandShelf);
    auto *clearDataButton = new QPushButton(QStringLiteral("清空数据"), m_commandShelf);
    auto *runButton = new QPushButton(QStringLiteral("运行"), m_commandShelf);
    auto *loopCount = new QSpinBox(m_commandShelf);
    loopCount->setRange(1, 1000000);
    loopCount->setValue(1);
    loopCount->setPrefix(QStringLiteral("循环 "));
    loopCount->setSuffix(QStringLiteral(" 次"));
    auto *stopButton = new QPushButton(QStringLiteral("停止"), m_commandShelf);
    runBar->addWidget(selectAllButton);
    runBar->addWidget(parseButton);
    runBar->addWidget(clearDataButton);
    runBar->addWidget(runButton);
    runBar->addWidget(loopCount);
    runBar->addWidget(stopButton);
    shelfLayout->addLayout(shelfBar); shelfLayout->addLayout(runBar); shelfLayout->addWidget(scroll, 1);
    m_runTimer = new QTimer(this);
    m_runTimer->setSingleShot(true);
    auto syncSelectAll = [this, selectAllButton] {
        bool allSelected = !m_commandRows.isEmpty();
        for (QWidget *row : m_commandRows) {
            auto *enabled = qobject_cast<QCheckBox *>(
                row->property("selected").value<QObject *>());
            if (!enabled || !enabled->isChecked()) {
                allSelected = false;
                break;
            }
        }
        const QSignalBlocker blocker(selectAllButton);
        selectAllButton->setChecked(allSelected);
        selectAllButton->setEnabled(!m_commandRows.isEmpty());
    };
    auto addRow = [this, syncSelectAll](const QString &text) {
        auto *row = new QWidget(m_rowsContainer);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(4);
        auto *enabled = new QCheckBox(row); enabled->setChecked(false); enabled->setToolTip(QStringLiteral("统一运行时发送此行"));
        auto *indexLabel = new QLabel(QString::number(m_commandRows.size() + 1), row);
        auto *edit = new QLineEdit(text, row); edit->setPlaceholderText(QStringLiteral("AT 指令"));
        edit->setMinimumWidth(100);
        auto *hex = new QCheckBox(QStringLiteral("HEX"), row); hex->setToolTip(QStringLiteral("按十六进制发送"));
        auto *delay = new QSpinBox(row); delay->setRange(0, 3600000); delay->setSuffix(QStringLiteral(" ms")); delay->setMinimumWidth(100); delay->setToolTip(QStringLiteral("发送前等待时间，0 表示立即"));
        auto *newline = new QCheckBox(QStringLiteral("换行"), row); newline->setChecked(true);
        auto *send = new QToolButton(row); send->setText(QStringLiteral("发送"));
        auto *remove = new QToolButton(row); remove->setText(QStringLiteral("×")); remove->setToolTip(QStringLiteral("删除此行"));
        layout->addWidget(enabled); layout->addWidget(indexLabel); layout->addWidget(edit, 1); layout->addWidget(send); layout->addWidget(delay); layout->addWidget(hex); layout->addWidget(newline); layout->addWidget(remove);
        m_rowsLayout->insertWidget(m_commandRows.size(), row); m_commandRows.append(row);
        auto sendRow = [this, edit, hex, newline] {
            m_commandLine->setHexMode(hex->isChecked());
            m_commandLine->setNewline(newline->isChecked());
            m_commandLine->sendLine(edit->text());
        };
        connect(send, &QToolButton::clicked, row, [sendRow, delay, row, enabled] {
            enabled->setChecked(true);
            if (delay->value() == 0) sendRow();
            else QTimer::singleShot(delay->value(), row, sendRow);
        });
        connect(remove, &QToolButton::clicked, this, [this, row, syncSelectAll] {
            m_commandRows.removeOne(row); row->deleteLater();
            for (int i = 0; i < m_commandRows.size(); ++i) {
                const auto labels = m_commandRows.at(i)->findChildren<QLabel *>();
                for (QLabel *label : labels) {
                    bool ok = false; label->text().toInt(&ok);
                    if (ok) { label->setText(QString::number(i + 1)); break; }
                }
            }
            syncSelectAll();
        });
        row->setProperty("selected", QVariant::fromValue(static_cast<QObject *>(enabled)));
        row->setProperty("edit", QVariant::fromValue(static_cast<QObject *>(edit)));
        row->setProperty("hex", QVariant::fromValue(static_cast<QObject *>(hex)));
        row->setProperty("delay", QVariant::fromValue(static_cast<QObject *>(delay)));
        row->setProperty("newline", QVariant::fromValue(static_cast<QObject *>(newline)));
        connect(enabled, &QCheckBox::toggled, row, [syncSelectAll] { syncSelectAll(); });
        syncSelectAll();
    };
    connect(addCommand, &QToolButton::clicked, this, [addRow] { addRow(QString()); });
    connect(selectAllButton, &QCheckBox::toggled, this,
            [this, syncSelectAll](bool checked) {
        for (QWidget *row : m_commandRows) {
            auto *enabled = qobject_cast<QCheckBox *>(
                row->property("selected").value<QObject *>());
            if (enabled) enabled->setChecked(checked);
        }
        syncSelectAll();
    });
    connect(m_console, &ConsoleView::importCommandsRequested, this, [this, addRow](const QString &text) {
        QStringList lines;
        for (const QString &line : text.split(QRegularExpression(QStringLiteral("\\r?\\n"))))
            if (!line.trimmed().isEmpty()) lines.append(line.trimmed());
        for (int i = 0; i < lines.size(); ++i) {
            if (i < m_commandRows.size()) {
                if (auto *edit = qobject_cast<QLineEdit *>(m_commandRows.at(i)->property("edit").value<QObject *>())) edit->setText(lines.at(i));
            } else {
                addRow(lines.at(i));
            }
        }
    });
    m_rowsLayout->addStretch(1);
    connect(parseButton, &QPushButton::clicked, m_console, &ConsoleView::importSendText);
    connect(clearDataButton, &QPushButton::clicked, this, [this] {
        for (QWidget *row : m_commandRows)
            if (auto *edit = qobject_cast<QLineEdit *>(row->property("edit").value<QObject *>())) edit->clear();
    });
    connect(runButton, &QPushButton::clicked, this, [this, loopCount] {
        m_runTimer->stop(); m_runIndex = 0; m_loopRemaining = loopCount->value();
        while (m_runIndex < m_commandRows.size() &&
               !m_commandRows.at(m_runIndex)->property("selected").value<QObject *>()->property("checked").toBool()) ++m_runIndex;
        if (m_runIndex < m_commandRows.size()) {
            auto *delay = qobject_cast<QSpinBox *>(m_commandRows.at(m_runIndex)->property("delay").value<QObject *>());
            m_runTimer->start(delay->value());
        }
    });
    connect(stopButton, &QPushButton::clicked, m_runTimer, &QTimer::stop);
    connect(m_runTimer, &QTimer::timeout, this, [this] {
        while (m_runIndex < m_commandRows.size()) {
            QWidget *row = m_commandRows.at(m_runIndex++);
            if (!row || row->property("selected").value<QObject *>()->property("checked").toBool() == false) continue;
            auto *edit = qobject_cast<QLineEdit *>(row->property("edit").value<QObject *>());
            auto *hex = qobject_cast<QCheckBox *>(row->property("hex").value<QObject *>());
            auto *newline = qobject_cast<QCheckBox *>(row->property("newline").value<QObject *>());
            m_commandLine->setHexMode(hex->isChecked()); m_commandLine->setNewline(newline->isChecked());
            m_commandLine->sendLine(edit->text()); break;
        }
        while (m_runIndex < m_commandRows.size() &&
               !m_commandRows.at(m_runIndex)->property("selected").value<QObject *>()->property("checked").toBool()) ++m_runIndex;
        if (m_runIndex < m_commandRows.size()) {
            auto *nextDelay = qobject_cast<QSpinBox *>(m_commandRows.at(m_runIndex)->property("delay").value<QObject *>());
            m_runTimer->start(nextDelay->value());
        } else if (--m_loopRemaining > 0) {
            m_runIndex = 0;
            while (m_runIndex < m_commandRows.size() &&
                   !m_commandRows.at(m_runIndex)->property("selected").value<QObject *>()->property("checked").toBool()) ++m_runIndex;
            if (m_runIndex < m_commandRows.size()) {
                auto *firstDelay = qobject_cast<QSpinBox *>(m_commandRows.at(m_runIndex)->property("delay").value<QObject *>());
                m_runTimer->start(firstDelay->value());
            }
        }
    });
    // 控制台里发送的内容，同步回显到命令行面板，避免两块输入区互不知情。
    connect(m_console, &ConsoleView::dataSent, m_commandLine, [this](const QByteArray &bytes) {
        m_commandLine->appendOutput(
            QStringLiteral("\u203a 控制台发送 %1 字节: %2\n")
                .arg(bytes.size())
                .arg(QString::fromLatin1(bytes.toHex(' ')).toUpper()));
    });

    m_bottomTabs = new QTabWidget(this);
    m_bottomTabs->setDocumentMode(true);
    m_bottomTabs->addTab(m_commandLine, QStringLiteral("串口命令行"));
    m_bottomTabs->addTab(m_parser, QStringLiteral("命令解析"));
    m_bottomTabs->setTabToolTip(0, QStringLiteral("一行一条命令，回车发送；↑↓ 翻历史，Ctrl+L 清屏"));
    m_bottomTabs->setTabToolTip(1,
                                QStringLiteral("按行/分隔符/固定长度/空闲分帧解析接收数据"));

    // 控制台放在一层容器里，才能整体隐藏、以及浮出到独立窗口后干净地嵌回来。
    m_consoleHost = new QWidget(this);
    auto *consoleLayout = new QVBoxLayout(m_consoleHost);
    consoleLayout->setContentsMargins(0, 0, 0, 0);
    consoleLayout->setSpacing(0);
    m_consoleTabs = new QTabWidget(m_consoleHost);
    m_consoleTabs->setDocumentMode(true);
    m_consoleTabs->setTabBarAutoHide(true);
    m_consoleTabs->addTab(m_console, QStringLiteral("控制台"));
    consoleLayout->addWidget(m_consoleTabs);

    m_splitter = new QSplitter(Qt::Vertical, this);
    m_splitter->setChildrenCollapsible(true);
    m_splitter->addWidget(m_consoleHost);
    m_splitter->addWidget(m_bottomTabs);
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 2);
    m_splitter->setSizes({420, 260});

    m_contentSplitter = new QSplitter(Qt::Horizontal, this);
    m_contentSplitter->setChildrenCollapsible(false);
    m_contentSplitter->setHandleWidth(6);
    m_contentSplitter->setOpaqueResize(true);
    m_contentSplitter->addWidget(m_splitter);
    m_contentSplitter->addWidget(m_commandShelf);
    m_contentSplitter->setCollapsible(0, false);
    m_contentSplitter->setCollapsible(1, false);
    m_splitter->setMinimumWidth(380);
    m_commandShelf->setMinimumWidth(320);
    m_contentSplitter->setStretchFactor(0, 1);
    m_contentSplitter->setStretchFactor(1, 0);
    m_contentSplitter->setSizes({800, 240});
    root->addWidget(createPanelBar());
    root->addWidget(m_contentSplitter, 1);

    syncPanelBar();
}

QWidget *SessionWorkspace::createPanelBar()
{
    auto *bar = new QFrame(this);
    bar->setObjectName(QStringLiteral("workspacePanelBar"));
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(6, 3, 6, 3);
    layout->setSpacing(4);

    auto makeToggle = [this, bar](const QString &text, const QString &tip) {
        auto *button = new QToolButton(bar);
        button->setText(text);
        button->setToolTip(tip);
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        return button;
    };

    m_consoleToggle = makeToggle(QStringLiteral("控制台"),
                                 QStringLiteral("显示 / 隐藏上方控制台（接收区与发送区）"));
    m_serialConfigToggle = makeToggle(QStringLiteral("串口配置"),
                                      QStringLiteral("显示 / 隐藏串口配置页"));
    m_bottomToggle = makeToggle(QStringLiteral("串口命令行"),
                                QStringLiteral("显示 / 隐藏串口命令行页，不影响命令解析和串口配置"));
    m_parserToggle = makeToggle(QStringLiteral("命令解析"),
                                QStringLiteral("单独显示 / 隐藏「命令解析」标签页"));
    m_commandsToggle = makeToggle(QStringLiteral("AT 命令"), QStringLiteral("显示 / 隐藏右侧 AT 命令清单"));
    m_pinToggle = makeToggle(QStringLiteral("固定在最前"), QStringLiteral("浮动窗口置顶显示"));
    m_pinToggle->setObjectName(QStringLiteral("sessionFloatPinToggle"));

    layout->addWidget(m_consoleToggle);
    layout->addWidget(m_serialConfigToggle);
    layout->addWidget(m_bottomToggle);
    layout->addWidget(m_parserToggle);
    layout->addWidget(m_commandsToggle);
    layout->addWidget(m_pinToggle);
    layout->addStretch();

    connect(m_consoleToggle, &QToolButton::clicked, this,
            [this](bool checked) { setConsoleVisible(checked); });
    connect(m_serialConfigToggle, &QToolButton::toggled, this,
            &SessionWorkspace::serialConfigurationToggled);
    connect(m_bottomToggle, &QToolButton::clicked, this,
            [this](bool checked) { setBottomVisible(checked); });
    connect(m_parserToggle, &QToolButton::clicked, this,
            [this](bool checked) { setParserVisible(checked); });
    connect(m_commandsToggle, &QToolButton::clicked, m_commandShelf, &QWidget::setVisible);
    connect(m_pinToggle, &QToolButton::toggled, this, [this](bool pinned) {
        m_floatPinned = pinned;
        setProperty("floatPinned", pinned);
        if (property("sessionFloating").toBool()) {
            setWindowPinned(window(), pinned);
            return;
        }
        if (!m_floatWindow) return;
        setWindowPinned(m_floatWindow, pinned);
    });

    return bar;
}

void SessionWorkspace::syncPanelBar()
{
    if (!m_consoleToggle) {
        return;
    }
    const QSignalBlocker b1(m_consoleToggle);
    const QSignalBlocker b2(m_serialConfigToggle);
    const QSignalBlocker b3(m_bottomToggle);
    const QSignalBlocker b4(m_parserToggle);
    const QSignalBlocker b5(m_pinToggle);

    const bool floating = isConsoleFloating();
    m_consoleToggle->setChecked(isConsoleVisible());
    m_serialConfigToggle->setChecked(
        m_serialConfigPage && m_bottomTabs && m_bottomTabs->indexOf(m_serialConfigPage) >= 0);
    m_bottomToggle->setChecked(isBottomVisible());
    m_parserToggle->setChecked(isParserVisible());
    if (m_floatToggle) m_floatToggle->setChecked(floating);
    if (m_commandsToggle) m_commandsToggle->setChecked(m_commandShelf->isVisible());
    if (m_pinToggle) m_pinToggle->setChecked(m_floatPinned);
}

QString SessionWorkspace::sessionId() const
{
    return m_session ? m_session->id() : QString();
}

void SessionWorkspace::showConsole()
{
    setConsoleVisible(true);
    if (m_floatWindow) {
        m_floatWindow->show();
        m_floatWindow->raise();
        m_floatWindow->activateWindow();
        return;
    }
    if (m_console) {
        m_console->setFocus();
    }
}

void SessionWorkspace::showCommandLine()
{
    if (!m_bottomTabs || !m_commandLine) {
        return;
    }
    setBottomVisible(true);
    m_bottomTabs->setCurrentWidget(m_commandLine);
    m_commandLine->focusInput();
}

void SessionWorkspace::showParser()
{
    if (!m_bottomTabs || !m_parser) {
        return;
    }
    setParserVisible(true);
    m_bottomTabs->setCurrentWidget(m_parser);
}

void SessionWorkspace::showConsolePage(QWidget *page, const QString &title)
{
    if (!m_consoleTabs || !page) return;
    int index = m_consoleTabs->indexOf(page);
    if (index < 0) index = m_consoleTabs->addTab(page, title);
    setConsoleVisible(true);
    m_consoleTabs->setCurrentIndex(index);
}

void SessionWorkspace::showBottomPage(QWidget *page, const QString &title)
{
    if (!m_bottomTabs || !page) return;
    int index = m_bottomTabs->indexOf(page);
    if (index < 0) {
        int insertAt = 0;
        const int parserIndex = m_parser ? m_bottomTabs->indexOf(m_parser) : -1;
        const int commandIndex = m_commandLine ? m_bottomTabs->indexOf(m_commandLine) : -1;
        if (parserIndex >= 0) insertAt = parserIndex + 1;
        else if (commandIndex >= 0) insertAt = commandIndex + 1;
        index = m_bottomTabs->insertTab(insertAt, page, title);
    }
    if (title == QStringLiteral("串口配置")) m_serialConfigPage = page;
    m_bottomTabs->setCurrentIndex(index);
    m_bottomTabs->setVisible(m_bottomTabs->count() > 0);
    syncPanelBar();
}

void SessionWorkspace::hideBottomPage(QWidget *page)
{
    if (!m_bottomTabs || !page) return;
    const int index = m_bottomTabs->indexOf(page);
    if (index >= 0) m_bottomTabs->removeTab(index);
    m_bottomTabs->setVisible(m_bottomTabs->count() > 0);
    syncPanelBar();
}

void SessionWorkspace::focusSend()
{
    if (m_console) {
        if (m_floatWindow) {
            m_floatWindow->raise();
        }
        m_console->focusSend();
    }
}

bool SessionWorkspace::isConsoleVisible() const
{
    return m_consoleVisible;
}

void SessionWorkspace::setConsoleVisible(bool visible)
{
    m_consoleVisible = visible;
    if (m_consoleHost) {
        m_consoleHost->setVisible(visible);
    }
    if (m_floatWindow) {
        m_floatWindow->setVisible(visible);
    }
    if (!visible && m_floatToggle && m_floatToggle->isChecked()) {
        // 控制台被隐藏时不该继续占着一个浮窗。
        setConsoleFloating(false);
    }
    syncPanelBar();
    emit panelVisibilityChanged();
}

bool SessionWorkspace::isBottomVisible() const
{
    return m_bottomTabs && m_commandLine && m_bottomTabs->indexOf(m_commandLine) >= 0;
}

void SessionWorkspace::setBottomVisible(bool visible)
{
    if (!m_bottomTabs || !m_commandLine) return;
    const int index = m_bottomTabs->indexOf(m_commandLine);
    if (visible && index < 0) {
        const int insertAt = 0;
        const int added = m_bottomTabs->insertTab(insertAt, m_commandLine,
                                                   QStringLiteral("串口命令行"));
        m_bottomTabs->setTabToolTip(
            added, QStringLiteral("一行一条命令，回车发送；↑↓ 翻历史，Ctrl+L 清屏"));
        m_bottomTabs->setCurrentWidget(m_commandLine);
    } else if (!visible && index >= 0) {
        m_bottomTabs->removeTab(index);
    }
    m_bottomTabs->setVisible(m_bottomTabs->count() > 0);
    syncPanelBar();
    emit panelVisibilityChanged();
}

bool SessionWorkspace::isParserVisible() const
{
    return m_bottomTabs && m_parser && m_bottomTabs->indexOf(m_parser) >= 0;
}

void SessionWorkspace::setParserVisible(bool visible)
{
    if (!m_bottomTabs || !m_parser) {
        return;
    }
    const int index = m_bottomTabs->indexOf(m_parser);
    if (visible) {
        if (index < 0) {
            // 插回「串口命令行」之后，保持固定的标签顺序。
            const int commandIndex = m_commandLine ? m_bottomTabs->indexOf(m_commandLine) : -1;
            const int configIndex = m_serialConfigPage
                                        ? m_bottomTabs->indexOf(m_serialConfigPage) : -1;
            const int insertAt = commandIndex >= 0 ? commandIndex + 1
                                  : configIndex >= 0 ? configIndex : 0;
            m_bottomTabs->insertTab(insertAt, m_parser, QStringLiteral("命令解析"));
            m_bottomTabs->setTabToolTip(
                insertAt, QStringLiteral("按行/分隔符/固定长度/空闲分帧解析接收数据"));
        }
        m_bottomTabs->setCurrentWidget(m_parser);
    } else {
        if (index >= 0) {
            m_bottomTabs->removeTab(index);
        }
    }
    m_bottomTabs->setVisible(m_bottomTabs->count() > 0);
    syncPanelBar();
    emit panelVisibilityChanged();
}

bool SessionWorkspace::isConsoleFloating() const
{
    return m_floatWindow != nullptr;
}

void SessionWorkspace::setConsoleFloating(bool floating)
{
    if (!m_console || !m_consoleHost) {
        return;
    }
    if (floating == isConsoleFloating()) {
        syncPanelBar();
        return;
    }

    if (floating) {
        if (!m_consoleVisible) {
            setConsoleVisible(true);
        }
        // 不设置主窗口为 owner，避免主窗口最小化时联动最小化浮窗。
        auto *window = new ConsoleFloatWindow;
        window->setWindowFlag(Qt::WindowStaysOnTopHint, m_floatPinned);
        window->onClosed = [this] { setConsoleFloating(false); };
        m_floatWindow = window;

        if (auto *hostLayout = qobject_cast<QVBoxLayout *>(m_consoleHost->layout())) {
            hostLayout->removeWidget(m_consoleTabs);
        }
        QWidget *placeholder = new QWidget(m_contentSplitter);
        m_contentSplitter->replaceWidget(1, placeholder);
        m_commandShelf->setParent(window);
        m_consoleTabs->setParent(window);
        if (auto *windowLayout = window->contentLayout()) {
            windowLayout->addWidget(m_consoleTabs);
            windowLayout->addWidget(m_commandShelf, 1);
        }
        m_consoleTabs->show();
        window->show();
        window->raise();
        window->activateWindow();
    } else {
        auto *window = static_cast<ConsoleFloatWindow *>(m_floatWindow);
        m_floatWindow = nullptr;
        window->onClosed = nullptr;

        if (auto *windowLayout = window->contentLayout()) {
            windowLayout->removeWidget(m_consoleTabs);
            windowLayout->removeWidget(m_commandShelf);
        }
        m_commandShelf->setParent(this);
        QWidget *placeholder = m_contentSplitter->replaceWidget(1, m_commandShelf);
        if (placeholder) placeholder->deleteLater();
        m_consoleTabs->setParent(m_consoleHost);
        if (auto *hostLayout = qobject_cast<QVBoxLayout *>(m_consoleHost->layout())) {
            hostLayout->addWidget(m_consoleTabs);
        }
        m_consoleTabs->show();
        window->hide();
        window->deleteLater();
        if (m_consoleVisible) {
            m_consoleHost->show();
        }
    }
    syncPanelBar();
}

} // namespace xcom
