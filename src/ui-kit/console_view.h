#pragma once

#include "xcom/api/sessions.h"
#include "xcom/api/ui.h"

#include <QWidget>

#include <memory>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QFile;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTimer;
class QStringDecoder;
QT_END_NAMESPACE

namespace xcom {

class ConsoleView : public QWidget, public ISessionDataSink
{
    Q_OBJECT

public:
    ConsoleView(ISession *session, IViewHost *host, QWidget *parent = nullptr);
    ~ConsoleView() override;

    QString sessionId() const;
    QString sessionName() const { return m_session ? m_session->name() : QString(); }
    bool isHexDisplay() const;
    bool isTimestampEnabled() const;

    void toggleHexDisplay();
    void toggleTimestamp();
    void clearDisplay();
    void saveToFile();
    void focusSend();
    void sendNow();
    void sendFile();
    void importSendText();

    // ISessionDataSink（主线程回调）
    void onData(const DataChunk &chunk) override;
    void onStateChanged(SessionState state, const Error &reason) override;
    void onError(const Error &error) override;

signals:
    // 控制台自身发出的发送（供同会话的其它面板回显）。
    void dataSent(const QByteArray &bytes);
    void importCommandsRequested(const QString &text);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildUi();
    void applyTheme();
    void scheduleFlush();
    void flushPending();
    void appendText(const QString &text);
    QString formatChunk(const DataChunk &chunk);
    QString decode(const QByteArray &bytes);
    void updateState();
    void updateStats();
    void updateSendEnabled();
    void updateTitle();
    void sendNextFileChunk();
    void stopFileSend(const QString &message = QString(), bool error = false);
    void showHint(const QString &text, bool error = false);
    CommandResult execute(const QString &commandId, const QVariantMap &args);

    ISession *m_session = nullptr;
    IViewHost *m_host = nullptr;
    ISubscription *m_subscription = nullptr;

    QVector<DataChunk> m_pending;
    std::unique_ptr<QStringDecoder> m_decoder;
    bool m_atLineStart = true;

    QPlainTextEdit *m_receive = nullptr;
    QPlainTextEdit *m_send = nullptr;
    QCheckBox *m_hexDisplayCheck = nullptr;
    QCheckBox *m_timestampCheck = nullptr;
    QCheckBox *m_autoScrollCheck = nullptr;
    QCheckBox *m_pauseCheck = nullptr;
    QCheckBox *m_hexSendCheck = nullptr;
    QCheckBox *m_newlineCheck = nullptr;
    QCheckBox *m_timedCheck = nullptr;
    QCheckBox *m_dtrCheck = nullptr;
    QCheckBox *m_rtsCheck = nullptr;
    QSpinBox *m_intervalSpin = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_sendFileButton = nullptr;
    QLabel *m_stateLabel = nullptr;
    QLabel *m_statsLabel = nullptr;
    QLabel *m_hintLabel = nullptr;

    QTimer *m_flushTimer = nullptr;
    QTimer *m_timedTimer = nullptr;
    QTimer *m_fileSendTimer = nullptr;
    QFile *m_sendFile = nullptr;
    qint64 m_fileBytesSent = 0;
    qint64 m_fileBytesTotal = 0;
};

} // namespace xcom
