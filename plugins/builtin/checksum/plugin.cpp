#include "xcom/api/plugin.h"
#include "xcom/api/ui.h"

#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
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
            || ch == QLatin1Char('-') || ch == QLatin1Char('_')) {
            continue;
        }
        if (ch == QLatin1Char('0') && i + 1 < text.size()
            && (text.at(i + 1) == QLatin1Char('x') || text.at(i + 1) == QLatin1Char('X'))) {
            ++i;
            continue;
        }
        const QChar upper = ch.toUpper();
        if (!ch.isDigit() && !(upper >= QLatin1Char('A') && upper <= QLatin1Char('F'))) {
            if (error) {
                *error = QStringLiteral("包含非法字符 %1").arg(ch);
            }
            return false;
        }
        digits.append(ch);
    }
    if (digits.size() % 2 != 0) {
        if (error) {
            *error = QStringLiteral("十六进制数字个数为奇数");
        }
        return false;
    }
    out->clear();
    for (int i = 0; i < digits.size(); i += 2) {
        bool ok = false;
        const int value = digits.mid(i, 2).toInt(&ok, 16);
        if (!ok) {
            if (error) {
                *error = QStringLiteral("无法解析 %1").arg(digits.mid(i, 2));
            }
            return false;
        }
        out->append(static_cast<char>(value));
    }
    return true;
}

quint16 crc16Modbus(const QByteArray &data)
{
    quint16 crc = 0xFFFF;
    for (char raw : data) {
        crc ^= static_cast<quint8>(raw);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x0001) ? static_cast<quint16>((crc >> 1) ^ 0xA001)
                                 : static_cast<quint16>(crc >> 1);
        }
    }
    return crc;
}

quint16 crc16Ccitt(const QByteArray &data)
{
    quint16 crc = 0xFFFF;
    for (char raw : data) {
        crc ^= static_cast<quint16>(static_cast<quint8>(raw) << 8);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) ? static_cast<quint16>((crc << 1) ^ 0x1021)
                                 : static_cast<quint16>(crc << 1);
        }
    }
    return crc;
}

QString hex4(quint32 value)
{
    return QStringLiteral("%1").arg(value, 4, 16, QLatin1Char('0')).toUpper();
}

class ChecksumView : public QWidget
{
public:
    ChecksumView(IViewHost *host, QWidget *parent)
        : QWidget(parent)
        , m_host(host)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(10, 10, 10, 10);
        layout->setSpacing(6);

        auto *title = new QLabel(QStringLiteral("输入十六进制字节，计算常用校验："), this);
        m_input = new QLineEdit(this);
        m_input->setPlaceholderText(QStringLiteral("例如：01 03 00 00 00 0A"));
        m_input->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

        auto *buttons = new QHBoxLayout;
        auto *computeButton = new QPushButton(QStringLiteral("计算"), this);
        auto *clearButton = new QPushButton(QStringLiteral("清空"), this);
        buttons->addWidget(computeButton);
        buttons->addWidget(clearButton);
        buttons->addStretch();

        m_result = new QLabel(this);
        m_result->setTextFormat(Qt::RichText);
        m_result->setWordWrap(true);
        m_result->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        m_result->setText(QStringLiteral("<span style='color:#9d9d9d'>结果将显示在这里</span>"));

        layout->addWidget(title);
        layout->addWidget(m_input);
        layout->addLayout(buttons);
        layout->addWidget(m_result);
        layout->addStretch();

        connect(computeButton, &QPushButton::clicked, this, [this] { compute(); });
        connect(m_input, &QLineEdit::returnPressed, this, [this] { compute(); });
        connect(clearButton, &QPushButton::clicked, this, [this] {
            m_input->clear();
            m_result->setText(QStringLiteral("<span style='color:#9d9d9d'>结果将显示在这里</span>"));
        });
    }

    void focusInput() { m_input->setFocus(); }

private:
    void compute()
    {
        const QString text = m_input->text().trimmed();
        if (text.isEmpty()) {
            m_result->setText(QStringLiteral("<span style='color:#f48771'>请输入十六进制数据</span>"));
            return;
        }
        const CommandResult result =
            m_host->executeCommand(QStringLiteral("checksum.compute"),
                                   {{QStringLiteral("hex"), text}});
        if (!result.ok) {
            m_result->setText(
                QStringLiteral("<span style='color:#f48771'>%1</span>")
                    .arg(result.message.toHtmlEscaped()));
            return;
        }

        const QVariantMap data = result.data;
        QStringList lines;
        lines << QStringLiteral("字节数      : %1").arg(data.value(QStringLiteral("length")).toInt());
        lines << QStringLiteral("CRC16/Modbus: 0x%1")
                     .arg(hex4(data.value(QStringLiteral("crc16Modbus")).toUInt()));
        lines << QStringLiteral("CRC16/CCITT : 0x%1")
                     .arg(hex4(data.value(QStringLiteral("crc16Ccitt")).toUInt()));
        lines << QStringLiteral("SUM8        : 0x%1")
                     .arg(hex4(data.value(QStringLiteral("sum8")).toUInt()));
        lines << QStringLiteral("XOR8        : 0x%1")
                     .arg(hex4(data.value(QStringLiteral("xor8")).toUInt()));
        m_result->setText(QStringLiteral("<pre style='color:#d4d4d4'>%1</pre>")
                              .arg(lines.join(QLatin1Char('\n')).toHtmlEscaped()));
    }

    IViewHost *m_host = nullptr;
    QLineEdit *m_input = nullptr;
    QLabel *m_result = nullptr;
};

} // namespace

class ChecksumPlugin : public QObject, public IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID XCOM_PLUGIN_IID FILE "manifest.json")
    Q_INTERFACES(xcom::IPlugin)

public:
    explicit ChecksumPlugin(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    bool initialize(IPluginContext &context, Error &error) override
    {
        m_context = &context;

        m_computeHandler = std::make_unique<Callback>();
        m_computeHandler->plugin = this;
        m_computeHandler->show = false;

        m_showHandler = std::make_unique<Callback>();
        m_showHandler->plugin = this;
        m_showHandler->show = true;

        CommandSpec compute;
        compute.id = QStringLiteral("checksum.compute");
        compute.title = QStringLiteral("计算校验值");
        compute.category = QStringLiteral("工具");
        compute.args = {CommandArg{QStringLiteral("hex"), CommandArgType::Hex, true, {},
                                   QStringLiteral("十六进制字节串，例如 AA 55")}};
        if (!context.commands().registerCommand(compute, m_computeHandler.get())) {
            error = Error::make(ErrorCode::AlreadyExists,
                                QStringLiteral("命令 checksum.compute 注册失败"));
            return false;
        }

        CommandSpec show;
        show.id = QStringLiteral("checksum.show");
        show.title = QStringLiteral("打开校验计算器");
        show.category = QStringLiteral("工具");
        if (!context.commands().registerCommand(show, m_showHandler.get())) {
            error = Error::make(ErrorCode::AlreadyExists,
                                QStringLiteral("命令 checksum.show 注册失败"));
            return false;
        }

        IUiContext *ui = context.ui();
        if (!ui) {
            // 无界面模式：只提供命令，供 CLI/脚本使用；不注册视图与菜单。
            return true;
        }

        class Factory : public IViewFactory
        {
        public:
            explicit Factory(ChecksumPlugin *plugin)
                : m_plugin(plugin)
            {
            }

            QWidget *create(QWidget *parent, IViewHost *host) override
            {
                auto *view = new ChecksumView(host, parent);
                m_plugin->m_view = view;
                return view;
            }

        private:
            ChecksumPlugin *m_plugin = nullptr;
        };

        ViewSpec spec;
        spec.id = QStringLiteral("com.xcom.tools.checksum.view");
        spec.title = QStringLiteral("校验计算器");
        spec.location = ViewLocation::Sidebar;
        spec.order = 300;

        auto factory = std::make_unique<Factory>(this);
        if (!ui->views().registerView(spec, factory.get())) {
            error = Error::make(ErrorCode::AlreadyExists,
                                QStringLiteral("视图注册失败，可能已存在"));
            return false;
        }
        m_factory = std::move(factory);

        MenuContribution menu;
        menu.menu = QStringLiteral("menu/tools");
        menu.group = QStringLiteral("checksum");
        menu.order = 10;
        menu.command = QStringLiteral("checksum.show");
        menu.label = QStringLiteral("校验计算器");
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

    void deactivate() override
    {
        m_view = nullptr;
    }

    void shutdown() override
    {
        m_view = nullptr;
        m_factory.reset();
        m_computeHandler.reset();
        m_showHandler.reset();
        m_context = nullptr;
    }

    CommandResult showView()
    {
        if (!m_context || !m_context->ui()) {
            return CommandResult::failure(QStringLiteral("当前没有图形界面"));
        }        m_context->ui()->showView(QStringLiteral("com.xcom.tools.checksum.view"));
        if (m_view) {
            m_view->focusInput();
        }
        return CommandResult::success();
    }

    CommandResult compute(const QString &hexText)
    {
        QByteArray data;
        QString parseError;
        if (!parseHex(hexText, &data, &parseError)) {
            return CommandResult::failure(parseError);
        }
        if (data.isEmpty()) {
            return CommandResult::failure(QStringLiteral("数据为空"));
        }

        quint8 sum = 0;
        quint8 xorValue = 0;
        for (char raw : data) {
            const quint8 value = static_cast<quint8>(raw);
            sum = static_cast<quint8>(sum + value);
            xorValue = static_cast<quint8>(xorValue ^ value);
        }

        QVariantMap result;
        result.insert(QStringLiteral("length"), data.size());
        result.insert(QStringLiteral("crc16Modbus"), crc16Modbus(data));
        result.insert(QStringLiteral("crc16Ccitt"), crc16Ccitt(data));
        result.insert(QStringLiteral("sum8"), sum);
        result.insert(QStringLiteral("xor8"), xorValue);
        return CommandResult::success(QStringLiteral("计算完成"), result);
    }

private:
    class Callback : public ICommandHandler
    {
    public:
        CommandResult execute(const CommandInvocation &invocation,
                              ICommandProgress *progress) override
        {
            Q_UNUSED(progress)
            if (!plugin) {
                return CommandResult::failure(QStringLiteral("插件已卸载"));
            }
            if (show) {
                return plugin->showView();
            }
            const QString hex = invocation.args.value(QStringLiteral("hex")).toString();
            if (hex.isEmpty()) {
                return CommandResult::failure(QStringLiteral("缺少 hex 参数"));
            }
            return plugin->compute(hex);
        }

        ChecksumPlugin *plugin = nullptr;
        bool show = false;
    };

    IPluginContext *m_context = nullptr;
    std::unique_ptr<IViewFactory> m_factory;
    QPointer<ChecksumView> m_view;
    std::unique_ptr<Callback> m_computeHandler;
    std::unique_ptr<Callback> m_showHandler;
};

#include "plugin.moc"
