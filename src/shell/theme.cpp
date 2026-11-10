#include "shell/theme.h"

#include <QApplication>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>

namespace xcom {

void applyTheme()
{
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(0x1e, 0x1e, 0x1e));
    palette.setColor(QPalette::WindowText, QColor(0xcc, 0xcc, 0xcc));
    palette.setColor(QPalette::Base, QColor(0x25, 0x25, 0x26));
    palette.setColor(QPalette::AlternateBase, QColor(0x2d, 0x2d, 0x30));
    palette.setColor(QPalette::ToolTipBase, QColor(0x2d, 0x2d, 0x30));
    palette.setColor(QPalette::ToolTipText, QColor(0xdd, 0xdd, 0xdd));
    palette.setColor(QPalette::Text, QColor(0xcc, 0xcc, 0xcc));
    palette.setColor(QPalette::Button, QColor(0x33, 0x33, 0x33));
    palette.setColor(QPalette::ButtonText, QColor(0xcc, 0xcc, 0xcc));
    palette.setColor(QPalette::BrightText, Qt::red);
    palette.setColor(QPalette::Link, QColor(0x37, 0x94, 0xff));
    palette.setColor(QPalette::Highlight, QColor(0x09, 0x47, 0x71));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(0x6b, 0x6b, 0x6b));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x6b, 0x6b, 0x6b));
    QApplication::setPalette(palette);

    qApp->setStyleSheet(QStringLiteral(R"(
QMainWindow { background: #1e1e1e; }
QMenuBar { background: #2d2d30; color: #cccccc; border-bottom: 1px solid #3c3c3c; }
QMenuBar::item { padding: 5px 10px; background: transparent; }
QMenuBar::item:selected { background: #094771; }
QMenu { background: #252526; color: #cccccc; border: 1px solid #3c3c3c; }
QMenu::item { padding: 5px 24px 5px 20px; }
QMenu::item:selected { background: #094771; }
QMenu::separator { height: 1px; background: #3c3c3c; margin: 4px 8px; }
QToolBar { background: #333333; border: none; spacing: 2px; }
QToolBar QToolButton { color: #cccccc; padding: 8px 6px; border-radius: 3px; }
QToolBar QToolButton:hover { background: #3f3f46; }
QToolBar QToolButton:checked { background: #094771; color: #ffffff; }
QDockWidget { color: #bbbbbb; titlebar-close-icon: none; titlebar-normal-icon: none; }
QDockWidget::title { background: #252526; padding: 6px 10px; border-bottom: 1px solid #3c3c3c; }
QTabWidget::pane { border: 1px solid #3c3c3c; background: #1e1e1e; }
QTabBar::tab { background: #2d2d30; color: #9d9d9d; padding: 7px 14px; border-right: 1px solid #252526; }
QTabBar::tab:selected { background: #1e1e1e; color: #ffffff; border-top: 1px solid #007acc; }
QTabBar::tab:hover { background: #3a3a3d; }
QStatusBar { background: #007acc; color: #ffffff; }
QStatusBar QLabel { color: #ffffff; padding: 0 8px; }
QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QComboBox {
    background: #1e1e1e; color: #d4d4d4; border: 1px solid #3c3c3c; border-radius: 2px;
    selection-background-color: #264f78;
}
QLineEdit, QSpinBox, QComboBox { padding: 3px 5px; }
QComboBox QAbstractItemView { background: #252526; color: #cccccc; selection-background-color: #094771; }
QPushButton { background: #3a3d41; color: #e6e6e6; border: 1px solid #4a4d51; border-radius: 3px; padding: 5px 12px; }
QPushButton:hover { background: #45494e; }
QPushButton:disabled { color: #7a7a7a; background: #2d2d30; }
QPushButton#sendButton { background: #0e639c; border-color: #1177bb; color: white; font-weight: 600; }
QPushButton#sendButton:hover { background: #1177bb; }
QPushButton#sendButton:disabled { background: #2d2d30; color: #7a7a7a; }
QCheckBox { color: #cccccc; spacing: 6px; }
QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid #6b6b6b; border-radius: 2px; background: #1e1e1e; }
QCheckBox::indicator:checked { background: #0e639c; border-color: #1177bb; }
QTableWidget { background: #1e1e1e; alternate-background-color: #252526; gridline-color: #3c3c3c; }
QHeaderView::section { background: #2d2d30; color: #cccccc; padding: 5px; border: 1px solid #3c3c3c; }
QScrollBar:vertical { background: #1e1e1e; width: 12px; margin: 0; }
QScrollBar::handle:vertical { background: #424242; min-height: 24px; border-radius: 4px; margin: 2px; }
QScrollBar::handle:vertical:hover { background: #4f4f4f; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar:horizontal { background: #1e1e1e; height: 12px; margin: 0; }
QScrollBar::handle:horizontal { background: #424242; min-width: 24px; border-radius: 4px; margin: 2px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QSplitter::handle { background: #3c3c3c; }
QLabel#sessionState { color: #89d185; font-weight: 600; }
QLabel#sessionStats { color: #9d9d9d; }
QFrame#workspacePanelBar { background: #252526; border-bottom: 1px solid #3c3c3c; }
QFrame#workspacePanelBar QToolButton {
    color: #9d9d9d; padding: 2px 8px; border: 1px solid transparent; border-radius: 3px;
}
QFrame#workspacePanelBar QToolButton:hover { background: #3a3a3d; color: #d4d4d4; }
QFrame#workspacePanelBar QToolButton:checked {
    background: #094771; color: #ffffff; border-color: #1177bb;
}
QFrame#workspacePanelBar QToolButton:disabled { color: #6b6b6b; }
)"));
}

} // namespace xcom
