#include "ui-kit/frame_parser_view.h"

#include "xcom/api/commands.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>

namespace xcom {
namespace {

constexpr int kMaxRows = 5000;

const char *const kHexDigits = "0123456789ABCDEF";

QString hexString(const QByteArray &bytes, int limit)
{
    QString out;
    const int count = limit > 0 && bytes.size() > limit ? limit : bytes.size();
    out.reserve(count * 3);
    for (int i = 0; i < count; ++i) {
        const unsigned char value = static_cast<unsigned char>(bytes.at(i));
        out.append(QLatin1Char(kHexDigits[value >> 4]));
        out.append(QLatin1Char(kHexDigits[value & 0x0f]));
        if (i + 1 < count) {
            out.append(QLatin1Char(' '));
        }
    }
    if (count < bytes.size()) {
        out.append(QStringLiteral(" …"));
    }
    return out;
}

QString asciiString(const QByteArray &bytes, int limit)
{
    QString out;
    const int count = limit > 0 && bytes.size() > limit ? limit : bytes.size();
    out.reserve(count);
    for (int i = 0; i < count; ++i) {
        const unsigned char value = static_cast<unsigned char>(bytes.at(i));
        if (value >= 0x20 && value < 0x7f) {
            out.append(QLatin1Char(static_cast<char>(value)));
        } else {
            out.append(QLatin1Char('.'));
        }
    }
    if (count < bytes.size()) {
        out.append(QStringLiteral(" …"));
    }
    return out;
}

} // namespace

FrameParserView::FrameParserView(ISession *session, IViewHost *host, QWidget *parent)
    : QWidget(parent)
    , m_session(session)
    , m_host(host)
{
    buildUi();
    applyModeUi();
    updateSummary();

    if (m_session) {
        m_subscription = m_session->subscribe(this);
    }
}

FrameParserView::~FrameParserView()
{
    if (m_subscription) {
        m_subscription->unsubscribe();
        m_subscription = nullptr;
    }
}

void FrameParserView::onData(const DataChunk &chunk)
{
    if (chunk.direction != Direction::Rx) {
        return;
    }
    feed(chunk.bytes, chunk.wallClockMs);
}

void FrameParserView::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    auto *settingsRow = new QHBoxLayout;
    settingsRow->setSpacing(6);

    m_modeBox = new QComboBox(this);
    m_modeBox->addItem(frameModeLabel(FrameMode::Line), frameModeKey(FrameMode::Line));
    m_modeBox->addItem(frameModeLabel(FrameMode::Delimiter),
                       frameModeKey(FrameMode::Delimiter));
    m_modeBox->addItem(frameModeLabel(FrameMode::FixedLength),
                       frameModeKey(FrameMode::FixedLength));
    m_modeBox->addItem(frameModeLabel(FrameMode::Idle), frameModeKey(FrameMode::Idle));
    m_modeBox->setMinimumWidth(200);

    m_delimiterEdit = new QLineEdit(this);
    m_delimiterEdit->setText(QStringLiteral("\\r\\n"));
    m_delimiterEdit->setToolTip(
        QStringLiteral("支持转义：\\r \\n \\t \\0 \\xHH，例如 \\r\\n 、 ; 或 \\x03"));
    m_delimiterEdit->setMinimumWidth(110);

    m_lengthSpin = new QSpinBox(this);
    m_lengthSpin->setRange(1, 65535);
    m_lengthSpin->setValue(8);
    m_lengthSpin->setSuffix(QStringLiteral(" 字节"));

    m_keepDelimiterCheck = new QCheckBox(QStringLiteral("保留分隔符"), this);
    m_dropEmptyCheck = new QCheckBox(QStringLiteral("丢弃空帧"), this);
    m_dropEmptyCheck->setChecked(true);
    m_trimCheck = new QCheckBox(QStringLiteral("去空白"), this);
    m_trimCheck->setChecked(true);
    m_hexCheck = new QCheckBox(QStringLiteral("HEX"), this);
    m_hexCheck->setChecked(true);
    m_pauseCheck = new QCheckBox(QStringLiteral("暂停"), this);

    m_maxBytesSpin = new QSpinBox(this);
    m_maxBytesSpin->setRange(64, 1024 * 1024);
    m_maxBytesSpin->setValue(4096);
    m_maxBytesSpin->setSuffix(QStringLiteral(" 上限"));

    settingsRow->addWidget(new QLabel(QStringLiteral("解析方式"), this));
    settingsRow->addWidget(m_modeBox);
    settingsRow->addWidget(m_delimiterEdit);
    settingsRow->addWidget(m_lengthSpin);
    settingsRow->addWidget(m_keepDelimiterCheck);
    settingsRow->addWidget(m_dropEmptyCheck);
    settingsRow->addWidget(m_trimCheck);
    settingsRow->addWidget(m_maxBytesSpin);
    settingsRow->addStretch();

    auto *actionRow = new QHBoxLayout;
    actionRow->setSpacing(6);
    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setObjectName(QStringLiteral("sessionStats"));
    m_exportButton = new QPushButton(QStringLiteral("导出 CSV"), this);
    m_clearButton = new QPushButton(QStringLiteral("清空"), this);
    actionRow->addWidget(m_hexCheck);
    actionRow->addWidget(m_pauseCheck);
    actionRow->addWidget(m_summaryLabel, 1);
    actionRow->addWidget(m_exportButton);
    actionRow->addWidget(m_clearButton);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({QStringLiteral("#"), QStringLiteral("时间"),
                                        QStringLiteral("长度"), QStringLiteral("HEX"),
                                        QStringLiteral("ASCII")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setAlternatingRowColors(true);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);

    root->addLayout(settingsRow);
    root->addLayout(actionRow);
    root->addWidget(m_table, 1);

    connect(m_modeBox, &QComboBox::currentIndexChanged, this, [this] {
        applyModeUi();
        onSettingsChanged();
    });
    connect(m_delimiterEdit, &QLineEdit::editingFinished, this,
            &FrameParserView::onSettingsChanged);
    connect(m_lengthSpin, &QSpinBox::valueChanged, this, &FrameParserView::onSettingsChanged);
    connect(m_maxBytesSpin, &QSpinBox::valueChanged, this, &FrameParserView::onSettingsChanged);
    connect(m_keepDelimiterCheck, &QCheckBox::toggled, this,
            &FrameParserView::onSettingsChanged);
    connect(m_dropEmptyCheck, &QCheckBox::toggled, this, &FrameParserView::onSettingsChanged);
    connect(m_trimCheck, &QCheckBox::toggled, this, &FrameParserView::onSettingsChanged);
    connect(m_clearButton, &QPushButton::clicked, this, &FrameParserView::clearFrames);
    connect(m_exportButton, &QPushButton::clicked, this, &FrameParserView::exportFrames);
    connect(m_pauseCheck, &QCheckBox::toggled, this, [this](bool paused) {
        if (!paused && m_droppedWhilePaused > 0) {
            m_droppedWhilePaused = 0;
            updateSummary();
        }
    });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
        const auto rows = m_table->selectionModel() ? m_table->selectionModel()->selectedRows()
                                                    : QModelIndexList();
        if (rows.isEmpty()) {
            return;
        }
        const int row = rows.first().row();
        QTableWidgetItem *item = m_table->item(row, 3);
        if (!item) {
            return;
        }
        m_lastFrame = QByteArray::fromHex(item->data(Qt::UserRole).toByteArray());
        emit frameSelected(m_lastFrame);
    });
}

void FrameParserView::applyModeUi()
{
    const FrameMode mode = frameModeFromKey(m_modeBox->currentData().toString());
    m_delimiterEdit->setVisible(mode == FrameMode::Delimiter);
    m_lengthSpin->setVisible(mode == FrameMode::FixedLength);
    m_keepDelimiterCheck->setEnabled(mode == FrameMode::Delimiter || mode == FrameMode::Line);
}

void FrameParserView::onSettingsChanged()
{
    FrameSettings settings = m_splitter.settings();
    settings.mode = frameModeFromKey(m_modeBox->currentData().toString());
    settings.delimiter = FrameSplitter::decodeEscapes(m_delimiterEdit->text());
    if (settings.delimiter.isEmpty()) {
        settings.delimiter = QByteArrayLiteral("\r\n");
    }
    settings.keepDelimiter = m_keepDelimiterCheck->isChecked();
    settings.fixedLength = m_lengthSpin->value();
    settings.maxFrameBytes = m_maxBytesSpin->value();
    settings.dropEmpty = m_dropEmptyCheck->isChecked();
    settings.trimWhitespace = m_trimCheck->isChecked();
    m_splitter.setSettings(settings);
    updateSummary();
}

void FrameParserView::setSettings(const FrameSettings &settings)
{
    m_modeBox->setCurrentIndex(qMax(0, m_modeBox->findData(frameModeKey(settings.mode))));
    m_delimiterEdit->setText(FrameSplitter::encodeEscapes(settings.delimiter));
    m_lengthSpin->setValue(settings.fixedLength);
    m_maxBytesSpin->setValue(settings.maxFrameBytes);
    m_keepDelimiterCheck->setChecked(settings.keepDelimiter);
    m_dropEmptyCheck->setChecked(settings.dropEmpty);
    m_trimCheck->setChecked(settings.trimWhitespace);
    applyModeUi();
    onSettingsChanged();
}

void FrameParserView::feed(const QByteArray &bytes, qint64 wallClockMs)
{
    if (isPaused()) {
        ++m_droppedWhilePaused;
        updateSummary();
        return;
    }
    const QVector<ParsedFrame> frames = m_splitter.push(bytes, wallClockMs);
    for (const ParsedFrame &frame : frames) {
        appendFrame(frame);
    }
    updateSummary();
}

void FrameParserView::flush()
{
    const QVector<ParsedFrame> frames = m_splitter.flush(QDateTime::currentMSecsSinceEpoch());
    for (const ParsedFrame &frame : frames) {
        appendFrame(frame);
    }
    updateSummary();
}

void FrameParserView::reset()
{
    m_splitter.reset();
    m_totalFrames = 0;
    m_droppedWhilePaused = 0;
    m_lastFrame.clear();
    updateSummary();
}

void FrameParserView::setPaused(bool paused)
{
    m_pauseCheck->setChecked(paused);
}

bool FrameParserView::isPaused() const
{
    return m_pauseCheck->isChecked();
}

int FrameParserView::frameCount() const
{
    return m_totalFrames;
}

void FrameParserView::appendFrame(const ParsedFrame &frame)
{
    m_table->insertRow(m_table->rowCount());
    const int row = m_table->rowCount() - 1;
    ++m_totalFrames;

    auto *indexItem = new QTableWidgetItem(QString::number(frame.index));
    auto *timeItem = new QTableWidgetItem(
        QDateTime::fromMSecsSinceEpoch(frame.wallClockMs).toString(QStringLiteral("HH:mm:ss.zzz")));
    auto *lengthItem = new QTableWidgetItem(
        frame.truncated ? QStringLiteral("%1 (截断)").arg(frame.bytes.size())
                        : QString::number(frame.bytes.size()));
    const bool hex = m_hexCheck->isChecked();
    auto *hexItem = new QTableWidgetItem(hex ? hexString(frame.bytes, 64)
                                             : QString::fromUtf8(frame.bytes));
    hexItem->setData(Qt::UserRole, QString::fromLatin1(frame.bytes.toHex()));
    auto *asciiItem = new QTableWidgetItem(asciiString(frame.bytes, 64));
    asciiItem->setToolTip(QString::fromUtf8(frame.bytes));

    m_table->setItem(row, 0, indexItem);
    m_table->setItem(row, 1, timeItem);
    m_table->setItem(row, 2, lengthItem);
    m_table->setItem(row, 3, hexItem);
    m_table->setItem(row, 4, asciiItem);

    if (m_table->rowCount() > kMaxRows) {
        m_table->removeRow(0);
    }
    m_table->scrollToBottom();
    m_lastFrame = frame.bytes;
    emit frameSelected(frame.bytes);
}

void FrameParserView::updateSummary()
{
    if (!m_summaryLabel) {
        return;
    }
    QString text = QStringLiteral("共 %1 帧 · 缓冲 %2 字节")
                       .arg(m_totalFrames)
                       .arg(m_splitter.bufferedBytes());
    if (isPaused()) {
        text.append(QStringLiteral(" · 已暂停，忽略 %1 块").arg(m_droppedWhilePaused));
    }
    m_summaryLabel->setText(text);
}

void FrameParserView::clearFrames()
{
    m_table->setRowCount(0);
    m_totalFrames = 0;
    m_droppedWhilePaused = 0;
    m_lastFrame.clear();
    m_splitter.reset();
    updateSummary();
}

void FrameParserView::exportFrames()
{
    if (m_table->rowCount() == 0) {
        m_summaryLabel->setText(QStringLiteral("没有可导出的帧"));
        return;
    }
    const QString documents =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString suggested = QDir(documents).filePath(
        QStringLiteral("%1-frames-%2.csv")
            .arg(m_session ? m_session->name() : QStringLiteral("serial"),
                 QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
    const QString fileName =
        QFileDialog::getSaveFileName(this, QStringLiteral("导出解析结果"), suggested,
                                     QStringLiteral("CSV 文件 (*.csv);;所有文件 (*.*)"));
    if (fileName.isEmpty()) {
        return;
    }

    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        m_summaryLabel->setText(QStringLiteral("无法写入文件"));
        return;
    }
    QString csv = QStringLiteral("#,time,length,hex,ascii\n");
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const QString index = m_table->item(row, 0) ? m_table->item(row, 0)->text() : QString();
        const QString time = m_table->item(row, 1) ? m_table->item(row, 1)->text() : QString();
        const QString length = m_table->item(row, 2) ? m_table->item(row, 2)->text() : QString();
        QString hex = m_table->item(row, 3) ? m_table->item(row, 3)->data(Qt::UserRole).toString()
                                            : QString();
        QString ascii = m_table->item(row, 4) ? m_table->item(row, 4)->text() : QString();
        ascii.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        hex.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        csv.append(QStringLiteral("%1,%2,%3,\"%4\",\"%5\"\n")
                       .arg(index, time, length, hex, ascii));
    }
    const QByteArray bytes = csv.toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        m_summaryLabel->setText(QStringLiteral("写入文件失败"));
        return;
    }
    m_summaryLabel->setText(QStringLiteral("已导出 %1 帧到 %2").arg(m_table->rowCount()).arg(fileName));
}

} // namespace xcom
