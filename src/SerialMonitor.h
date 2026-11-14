#pragma once

#include "Theme.h"

#include <QByteArray>
#include <QSerialPort>
#include <QString>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTextDecoder;
class QTimer;
class QToolButton;

// Serial Monitor panel (right side of the editor splitter). Created lazily on
// first open. The port follows the toolbar's Port selection; it is opened while
// the panel is visible and closed when hidden.
class SerialMonitor : public QWidget
{
    Q_OBJECT

public:
    explicit SerialMonitor(QWidget *parent = nullptr);
    ~SerialMonitor() override;

    void setPortName(const QString &name);   // reopens if currently open
    QString portName() const { return m_portName; }
    bool isOpen() const;
    bool openPort();
    void closePort();
    // Reopen after an upload: the board resets/re-enumerates, so retry briefly.
    void reopenWithRetry(int attemptsLeft = 10);
    void setTheme(Theme::Mode mode);   // icon colors (the stylesheet is set by MainWindow)
    QString writeRaw(const QByteArray &data);   // error text, empty on success
    // Puts "Sent: <text>" in the received view, on its own line (Sent color).
    void showSent(const QString &text);
    // Reset button text: "Reset Arduino" for Arduino boards, else "Reset Board".
    void setBoardIsArduino(bool arduino);

signals:
    void sent(const QString &text);          // text as typed, without line ending
    void message(const QString &text, bool isError);
    // The port could not be opened (other than "not found"), or the board stopped
    // taking data: its USB chip may be stuck (motor/relay noise). MainWindow offers
    // to restart the USB device.
    void portStuck(const QString &portName, const QString &error);

private slots:
    void send();
    void onReadyRead();
    void flushReceived();
    void onError(QSerialPort::SerialPortError error);
    void onBaudChanged(int index);
    void resetBoard();

private:
    void updateHeader();
    void checkWriteStuck();
    void insertLine(const QString &text, Theme::LogKind kind, int state);   // colored line in the view

    QSerialPort *m_port = nullptr;
    QString m_portName;

    QLabel *m_header = nullptr;
    QPlainTextEdit *m_view = nullptr;
    QLineEdit *m_input = nullptr;
    QPushButton *m_sendButton = nullptr;
    QComboBox *m_baudCombo = nullptr;
    QComboBox *m_endingCombo = nullptr;
    QToolButton *m_timestampButton = nullptr;
    QToolButton *m_resetButton = nullptr;
    QTimer *m_writeCheck = nullptr;   // data not leaving after a write: board stuck?
    bool m_stuckReported = false;     // once per open
    bool m_boardIsArduino = true;
    bool m_atLineStart = true;   // next received char starts a line (timestamp goes here)
    Theme::Mode m_mode = Theme::Mode::Dark;

    // Incoming data is batched (fast streams would otherwise flood the UI).
    QTextDecoder *m_decoder = nullptr;
    QString m_pending;
    QTimer *m_flushTimer = nullptr;
    bool m_lastWasCR = false;
    int m_reopenGeneration = 0;
};
