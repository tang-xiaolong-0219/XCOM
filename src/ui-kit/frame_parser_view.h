#pragma once

#include "serial/frame_splitter.h"
#include "xcom/api/sessions.h"
#include "xcom/api/ui.h"

#include <QWidget>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
QT_END_NAMESPACE

namespace xcom {

// 命令解析窗口：把接收字节流按策略切成一条条「命令」并列表展示。
// 分帧算法在 xcom_serial::FrameSplitter，可脱离界面单测。
class FrameParserView : public QWidget, public ISessionDataSink
{
    Q_OBJECT

public:
    FrameParserView(ISession *session, IViewHost *host, QWidget *parent = nullptr);
    ~FrameParserView() override;

    void feed(const QByteArray &bytes, qint64 wallClockMs);
    void flush();
    void reset();

    void clearFrames();
    void setPaused(bool paused);
    bool isPaused() const;
    int frameCount() const;

    FrameSettings settings() const { return m_splitter.settings(); }
    void setSettings(const FrameSettings &settings);

    // 最近一帧的原始字节（供命令面板/命令行复用）。
    QByteArray lastFrame() const { return m_lastFrame; }

    // ISessionDataSink（主线程回调）：与 ConsoleView 并行消费同一份数据。
    void onData(const DataChunk &chunk) override;

signals:
    void frameSelected(const QByteArray &bytes);

public slots:
    void exportFrames();

private:
    void buildUi();
    void applyModeUi();
    void onSettingsChanged();
    void appendFrame(const ParsedFrame &frame);
    void updateSummary();

    ISession *m_session = nullptr;
    IViewHost *m_host = nullptr;
    ISubscription *m_subscription = nullptr;
    FrameSplitter m_splitter;
    QByteArray m_lastFrame;
    int m_totalFrames = 0;
    int m_droppedWhilePaused = 0;

    QComboBox *m_modeBox = nullptr;
    QLineEdit *m_delimiterEdit = nullptr;
    QSpinBox *m_lengthSpin = nullptr;
    QCheckBox *m_keepDelimiterCheck = nullptr;
    QCheckBox *m_dropEmptyCheck = nullptr;
    QCheckBox *m_trimCheck = nullptr;
    QCheckBox *m_hexCheck = nullptr;
    QCheckBox *m_pauseCheck = nullptr;
    QSpinBox *m_maxBytesSpin = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_exportButton = nullptr;
    QPushButton *m_clearButton = nullptr;
};

} // namespace xcom
