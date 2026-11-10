#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSerialPort>

#include <memory>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QSplitter;
class QTimer;
QT_END_NAMESPACE

class QStringDecoder;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void refreshPorts();
    void togglePort();
    void handleReadyRead();
    void handleSerialError(QSerialPort::SerialPortError error);
    void sendData();
    void clearReceive();
    void clearSend();
    void saveReceiveData();

private:
    void buildUi();
    void loadSettings();
    void saveSettings();

    bool openPort();
    void closePort();

    void appendIncoming(const QString &text);
    void appendReceiveText(const QString &text);
    QString decodeText(const QByteArray &data);
    QByteArray encodeText(const QString &text) const;
    std::unique_ptr<QStringDecoder> createDecoder() const;

    void updateUiState();
    void updateCounters();
    void updateStatusLabel();

    QSerialPort *m_serial = nullptr;
    QTimer *m_autoSendTimer = nullptr;
    QTimer *m_portScanTimer = nullptr;
    std::unique_ptr<QStringDecoder> m_decoder;

    qint64 m_rxBytes = 0;
    qint64 m_txBytes = 0;
    bool m_atLineStart = true;
    bool m_closing = false;
    QString m_preferredPort;

    // 串口设置
    QComboBox *m_portBox = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QComboBox *m_baudBox = nullptr;
    QComboBox *m_dataBitsBox = nullptr;
    QComboBox *m_stopBitsBox = nullptr;
    QComboBox *m_parityBox = nullptr;
    QComboBox *m_flowBox = nullptr;
    QComboBox *m_encodingBox = nullptr;
    QCheckBox *m_dtrCheck = nullptr;
    QCheckBox *m_rtsCheck = nullptr;
    QPushButton *m_openButton = nullptr;

    // 接收区
    QCheckBox *m_hexDisplayCheck = nullptr;
    QCheckBox *m_timestampCheck = nullptr;
    QCheckBox *m_autoScrollCheck = nullptr;
    QCheckBox *m_pauseCheck = nullptr;
    QPushButton *m_clearReceiveButton = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPlainTextEdit *m_receiveEdit = nullptr;

    // 发送区
    QCheckBox *m_hexSendCheck = nullptr;
    QCheckBox *m_newlineCheck = nullptr;
    QCheckBox *m_autoSendCheck = nullptr;
    QSpinBox *m_autoSendInterval = nullptr;
    QPushButton *m_clearSendButton = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPlainTextEdit *m_sendEdit = nullptr;

    QSplitter *m_splitter = nullptr;
    QLabel *m_stateLabel = nullptr;
    QLabel *m_counterLabel = nullptr;
};

#endif // MAINWINDOW_H
