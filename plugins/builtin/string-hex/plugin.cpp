#include "xcom/api/plugin.h"
#include "xcom/api/ui.h"

#include <QApplication>
#include <QByteArray>
#include <QClipboard>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>
#include <QtPlugin>

#include <memory>

using namespace xcom;

namespace {

bool parseHex(const QString &text, QByteArray *out, QString *error)
{
    QString digits;
    digits.reserve(text.size());

    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch.isSpace() || ch == QLatin1Char(',') || ch == QLatin1Char(';')
            || ch == QLatin1Char('-') || ch == QLatin1Char('_') || ch == QLatin1Char(':')) {
            continue;
        }
        if (ch == QLatin1Char('0') && i + 1 < text.size()
            && (text.at(i + 1) == QLatin1Char('x') || text.at(i + 1) == QLatin1Char('X'))) {
            ++i;
            continue;
        }

        const ushort value = ch.unicode();
        const bool isHexDigit = (value >= ushort('0') && value <= ushort('9'))
                                || (value >= ushort('a') && value <= ushort('f'))
                                || (value >= ushort('A') && value <= ushort('F'));
        if (!isHexDigit) {
            if (error) {
                *error = QStringLiteral("包含非法字符：%1").arg(ch);
            }
            return false;
        }
        digits.append(ch);
    }

    if (digits.size() % 2 != 0) {
        if (error) {
            *error = QStringLiteral("十六进制数据需要成对输入");
        }
        return false;
    }

    QByteArray bytes;
    bytes.reserve(digits.size() / 2);
    for (int i = 0; i < digits.size(); i += 2) {
        bool ok = false;
        const int value = digits.mid(i, 2).toInt(&ok, 16);
        if (!ok) {
            if (error) {
                *error = QStringLiteral("无法解析十六进制字节：%1").arg(digits.mid(i, 2));
            }
            return false;
        }
        bytes.append(static_cast<char>(value));
    }

    if (out) {
        *out = bytes;
    }
    return true;
}

QString formatHex(const QByteArray &bytes)
{
    QStringList parts;
    parts.reserve(bytes.size());
    for (char raw : bytes) {
        const auto value = static_cast<quint8>(raw);
        parts.append(QStringLiteral("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper());
    }
    return parts.join(QLatin1Char(' '));
}

class StringHexView : public QWidget
{
public:
    StringHexView(IViewHost *host, QWidget *parent)
        : QWidget(parent)
        , m_host(host)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(6);

        auto *hint = new QLabel(
            QStringLiteral("字符串按 UTF-8 编码；HEX 可包含空格、逗号或 0x 前缀。"), this);
        hint->setWordWrap(true);

        auto *inputLabel = new QLabel(QStringLiteral("输入内容"), this);
        m_input = new QPlainTextEdit(this);
        m_input->setPlaceholderText(QStringLiteral("输入字符串或十六进制字节，例如：E4 B8 AD"));
        m_input->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

        auto *convertButtons = new QHBoxLayout;
        auto *encodeButton = new QPushButton(QStringLiteral("字符串 → HEX"), this);
        auto *decodeButton = new QPushButton(QStringLiteral("HEX → 字符串"), this);
        convertButtons->addWidget(encodeButton);
        convertButtons->addWidget(decodeButton);

        auto *outputLabel = new QLabel(QStringLiteral("转换结果"), this);
        m_output = new QPlainTextEdit(this);
        m_output->setReadOnly(true);
        m_output->setPlaceholderText(QStringLiteral("转换结果显示在这里"));
        m_output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

        auto *outputButtons = new QHBoxLayout;
        m_copyButton = new QPushButton(QStringLiteral("复制结果"), this);
        auto *clearButton = new QPushButton(QStringLiteral("清空"), this);
        m_copyButton->setEnabled(false);
        outputButtons->addWidget(m_copyButton);
        outputButtons->addWidget(clearButton);
        outputButtons->addStretch();

        m_status = new QLabel(QStringLiteral("就绪"), this);
        m_status->setStyleSheet(QStringLiteral("color:#9d9d9d;"));

        layout->addWidget(hint);
        layout->addWidget(inputLabel);
        layout->addWidget(m_input, 1);
        layout->addLayout(convertButtons);
        layout->addWidget(outputLabel);
        layout->addWidget(m_output, 1);
        layout->addLayout(outputButtons);
        layout->addWidget(m_status);

        connect(encodeButton, &QPushButton::clicked, this,
                [this] { convert(QStringLiteral("stringhex.encode")); });
        connect(decodeButton, &QPushButton::clicked, this,
                [this] { convert(QStringLiteral("stringhex.decode")); });
        connect(m_copyButton, &QPushButton::clicked, this, [this] {
            if (QApplication::clipboard()) {
                QApplication::clipboard()->setText(m_output->toPlainText());
                m_status->setText(QStringLiteral("已复制结果"));
            }
        });
        connect(clearButton, &QPushButton::clicked, this, [this] {
            m_input->clear();
            m_output->clear();
            m_status->setText(QStringLiteral("就绪"));
            m_status->setStyleSheet(QStringLiteral("color:#9d9d9d;"));
        });
        connect(m_output, &QPlainTextEdit::textChanged, this, [this] {
            m_copyButton->setEnabled(!m_output->toPlainText().isEmpty());
        });
    }

    void focusInput() { m_input->setFocus(); }

private:
    void convert(const QString &commandId)
    {
        if (!m_host) {
            m_status->setText(QStringLiteral("视图命令不可用"));
            return;
        }

        const QString value = m_input->toPlainText();
        const QString argument = commandId == QStringLiteral("stringhex.encode")
                                     ? QStringLiteral("text")
                                     : QStringLiteral("hex");
        const CommandResult result =
            m_host->executeCommand(commandId, {{argument, value}});
        if (!result.ok) {
            m_output->clear();
            m_status->setText(result.message);
            m_status->setStyleSheet(QStringLiteral("color:#f48771;"));
            return;
        }

        const QString outputKey = commandId == QStringLiteral("stringhex.encode")
                                      ? QStringLiteral("hex")
                                      : QStringLiteral("text");
        m_output->setPlainText(result.data.value(outputKey).toString());
        const int length = result.data.value(QStringLiteral("byteLength")).toInt();
        const QString type = commandId == QStringLiteral("stringhex.encode")
                                 ? QStringLiteral("UTF-8 字节")
                                 : QStringLiteral("解码字节");
        m_status->setText(QStringLiteral("转换完成：%1 字节（%2）").arg(length).arg(type));
        m_status->setStyleSheet(QStringLiteral("color:#89d185;"));
    }

    IViewHost *m_host = nullptr;
    QPlainTextEdit *m_input = nullptr;
    QPlainTextEdit *m_output = nullptr;
    QPushButton *m_copyButton = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace

class StringHexPlugin : public QObject, public IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID XCOM_PLUGIN_IID FILE "manifest.json")
    Q_INTERFACES(xcom::IPlugin)

public:
    explicit StringHexPlugin(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    bool initialize(IPluginContext &context, Error &error) override
    {
        m_context = &context;

        m_encodeHandler = std::make_unique<Callback>(this, Callback::Operation::Encode);
        m_decodeHandler = std::make_unique<Callback>(this, Callback::Operation::Decode);
        m_showHandler = std::make_unique<Callback>(this, Callback::Operation::Show);

        CommandSpec encode;
        encode.id = QStringLiteral("stringhex.encode");
        encode.title = QStringLiteral("字符串转十六进制");
        encode.category = QStringLiteral("工具");
        encode.args = {CommandArg{QStringLiteral("text"), CommandArgType::String, false, {},
                                  QStringLiteral("待转换的 UTF-8 字符串")}};
        if (!context.commands().registerCommand(encode, m_encodeHandler.get())) {
            error = Error::make(ErrorCode::AlreadyExists,
                                QStringLiteral("命令 stringhex.encode 注册失败"));
            return false;
        }

        CommandSpec decode;
        decode.id = QStringLiteral("stringhex.decode");
        decode.title = QStringLiteral("十六进制转字符串");
        decode.category = QStringLiteral("工具");
        decode.args = {CommandArg{QStringLiteral("hex"), CommandArgType::String, false, {},
                                  QStringLiteral("十六进制字节，可用空格分隔")}};
        if (!context.commands().registerCommand(decode, m_decodeHandler.get())) {
            error = Error::make(ErrorCode::AlreadyExists,
                                QStringLiteral("命令 stringhex.decode 注册失败"));
            return false;
        }

        CommandSpec show;
        show.id = QStringLiteral("stringhex.show");
        show.title = QStringLiteral("打开字符串与十六进制转换");
        show.category = QStringLiteral("工具");
        if (!context.commands().registerCommand(show, m_showHandler.get())) {
            error = Error::make(ErrorCode::AlreadyExists,
                                QStringLiteral("命令 stringhex.show 注册失败"));
            return false;
        }

        IUiContext *ui = context.ui();
        if (!ui) {
            return true;
        }

        class Factory : public IViewFactory
        {
        public:
            explicit Factory(StringHexPlugin *plugin)
                : m_plugin(plugin)
            {
            }

            QWidget *create(QWidget *parent, IViewHost *host) override
            {
                auto *view = new StringHexView(host, parent);
                m_plugin->m_view = view;
                return view;
            }

        private:
            StringHexPlugin *m_plugin = nullptr;
        };

        ViewSpec viewSpec;
        viewSpec.id = QStringLiteral("com.xcom.tools.string-hex.view");
        viewSpec.title = QStringLiteral("字符串转换");
        viewSpec.icon = QStringLiteral("string-hex");
        viewSpec.location = ViewLocation::Sidebar;
        viewSpec.order = 400;

        auto factory = std::make_unique<Factory>(this);
        if (!ui->views().registerView(viewSpec, factory.get())) {
            error = Error::make(ErrorCode::AlreadyExists,
                                QStringLiteral("字符串转换视图注册失败"));
            return false;
        }
        m_factory = std::move(factory);

        MenuContribution menu;
        menu.menu = QStringLiteral("menu/tools");
        menu.group = QStringLiteral("string-hex");
        menu.order = 20;
        menu.command = QStringLiteral("stringhex.show");
        menu.label = QStringLiteral("字符串与十六进制转换");
        if (!ui->menus().addItem(menu)) {
            error = Error::make(ErrorCode::Internal, QStringLiteral("菜单贡献注册失败"));
            return false;
        }
        return true;
    }

    bool activate(Error &error) override
    {
        Q_UNUSED(error)
        return true;
    }

    void deactivate() override { m_view = nullptr; }

    void shutdown() override
    {
        m_view = nullptr;
        m_factory.reset();
        m_encodeHandler.reset();
        m_decodeHandler.reset();
        m_showHandler.reset();
        m_context = nullptr;
    }

    CommandResult showView()
    {
        if (!m_context || !m_context->ui()) {
            return CommandResult::failure(QStringLiteral("当前没有图形界面"));
        }
        m_context->ui()->showView(QStringLiteral("com.xcom.tools.string-hex.view"));
        if (m_view) {
            m_view->focusInput();
        }
        return CommandResult::success();
    }

    CommandResult encode(const QString &text) const
    {
        const QByteArray bytes = text.toUtf8();
        return CommandResult::success(
            QStringLiteral("转换完成"),
            {{QStringLiteral("hex"), formatHex(bytes)},
             {QStringLiteral("byteLength"), bytes.size()}});
    }

    CommandResult decode(const QString &hex) const
    {
        QByteArray bytes;
        QString parseError;
        if (!parseHex(hex, &bytes, &parseError)) {
            return CommandResult::failure(parseError);
        }

        const QString text = QString::fromUtf8(bytes.constData(), bytes.size());
        if (text.toUtf8() != bytes) {
            return CommandResult::failure(QStringLiteral("十六进制数据不是有效的 UTF-8 文本"));
        }
        return CommandResult::success(
            QStringLiteral("转换完成"),
            {{QStringLiteral("text"), text}, {QStringLiteral("byteLength"), bytes.size()}});
    }

private:
    class Callback : public ICommandHandler
    {
    public:
        enum class Operation { Encode, Decode, Show };

        Callback(StringHexPlugin *plugin, Operation operation)
            : m_plugin(plugin)
            , m_operation(operation)
        {
        }

        CommandResult execute(const CommandInvocation &invocation,
                              ICommandProgress *progress) override
        {
            Q_UNUSED(progress)
            if (!m_plugin) {
                return CommandResult::failure(QStringLiteral("插件已卸载"));
            }
            switch (m_operation) {
            case Operation::Encode:
                return m_plugin->encode(invocation.args.value(QStringLiteral("text")).toString());
            case Operation::Decode:
                return m_plugin->decode(invocation.args.value(QStringLiteral("hex")).toString());
            case Operation::Show:
                return m_plugin->showView();
            }
            return CommandResult::failure(QStringLiteral("未知操作"));
        }

    private:
        StringHexPlugin *m_plugin = nullptr;
        Operation m_operation = Operation::Encode;
    };

    IPluginContext *m_context = nullptr;
    std::unique_ptr<IViewFactory> m_factory;
    QPointer<StringHexView> m_view;
    std::unique_ptr<Callback> m_encodeHandler;
    std::unique_ptr<Callback> m_decodeHandler;
    std::unique_ptr<Callback> m_showHandler;
};

#include "plugin.moc"
