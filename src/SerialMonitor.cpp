#include "SerialMonitor.h"
#include "Theme.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QTextCodec>
#include <QTextCursor>
#include <QTime>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
const int kBaudRates[] = {300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 74880,
                          115200, 230400, 250000, 500000, 1000000, 2000000};
constexpr int kDefaultBaud = 9600;
constexpr int kFlushIntervalMs = 30;
constexpr int kMaxLines = 5000;
}

SerialMonitor::SerialMonitor(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("serialMonitor"));
    QSettings settings;

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_header = new QLabel;
    m_header->setObjectName(QStringLiteral("panelHeader"));
    layout->addWidget(m_header);

    m_view = new QPlainTextEdit;
    m_view->setObjectName(QStringLiteral("serialView"));
    m_view->setReadOnly(true);
    m_view->setMaximumBlockCount(kMaxLines);
    m_view->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setFont(Theme::monoFont(10));
    layout->addWidget(m_view, 1);

    // Input row: text + Send.
    auto *inputRow = new QWidget;
    inputRow->setObjectName(QStringLiteral("serialControls"));
    auto *inputLayout = new QHBoxLayout(inputRow);
    inputLayout->setContentsMargins(8, 8, 8, 4);
    inputLayout->setSpacing(6);
    m_input = new QLineEdit;
    m_input->setFont(Theme::monoFont(10));
    m_input->setPlaceholderText(tr("Message (Enter to send)"));
    m_sendButton = new QPushButton(tr("Send"));
    m_sendButton->setObjectName(QStringLiteral("sendButton"));
    inputLayout->addWidget(m_input, 1);
    inputLayout->addWidget(m_sendButton);
    layout->addWidget(inputRow);

    // Options row: line ending + baud.
    auto *optionsRow = new QWidget;
    optionsRow->setObjectName(QStringLiteral("serialControls"));
    auto *optionsLayout = new QHBoxLayout(optionsRow);
    optionsLayout->setContentsMargins(8, 4, 8, 8);
    optionsLayout->setSpacing(6);
    m_endingCombo = new QComboBox;
    m_endingCombo->addItem(tr("No line ending"), QByteArray());
    m_endingCombo->addItem(tr("Newline"), QByteArray("\n"));
    m_endingCombo->addItem(tr("Carriage return"), QByteArray("\r"));
    m_endingCombo->addItem(tr("Both NL & CR"), QByteArray("\r\n"));
    m_endingCombo->setCurrentIndex(settings.value(QStringLiteral("serial/lineEnding"), 1).toInt());
    m_baudCombo = new QComboBox;
    const int savedBaud = settings.value(QStringLiteral("serial/baud"), kDefaultBaud).toInt();
    for (int baud : kBaudRates) {
        m_baudCombo->addItem(tr("%1 baud").arg(baud), baud);
        if (baud == savedBaud)
            m_baudCombo->setCurrentIndex(m_baudCombo->count() - 1);
    }
    // Timestamp toggle: prefixes each received line with its arrival time.
    m_timestampButton = new QToolButton;
    m_timestampButton->setObjectName(QStringLiteral("timestampButton"));
    m_timestampButton->setCheckable(true);
    m_timestampButton->setChecked(settings.value(QStringLiteral("serial/timestamps"), false).toBool());
    m_timestampButton->setToolTip(tr("Show timestamps (HH:mm:ss.zzz) for received lines"));
    m_timestampButton->setIconSize(QSize(16, 16));
    connect(m_timestampButton, &QToolButton::toggled, this, [](bool on) {
        QSettings().setValue(QStringLiteral("serial/timestamps"), on);
    });

    optionsLayout->addWidget(m_endingCombo, 1);
    optionsLayout->addWidget(m_baudCombo, 1);
    optionsLayout->addWidget(m_timestampButton);
    layout->addWidget(optionsRow);

    m_port = new QSerialPort(this);
    connect(m_port, &QSerialPort::readyRead, this, &SerialMonitor::onReadyRead);
    connect(m_port, &QSerialPort::errorOccurred, this, &SerialMonitor::onError);

    m_flushTimer = new QTimer(this);
    m_flushTimer->setSingleShot(true);
    m_flushTimer->setInterval(kFlushIntervalMs);
    connect(m_flushTimer, &QTimer::timeout, this, &SerialMonitor::flushReceived);

    connect(m_input, &QLineEdit::returnPressed, this, &SerialMonitor::send);
    connect(m_sendButton, &QPushButton::clicked, this, &SerialMonitor::send);
    connect(m_baudCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SerialMonitor::onBaudChanged);
    connect(m_endingCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [](int index) {
        QSettings().setValue(QStringLiteral("serial/lineEnding"), index);
    });

    updateHeader();
}

SerialMonitor::~SerialMonitor()
{
    closePort();
    delete m_decoder;
}

bool SerialMonitor::isOpen() const
{
    return m_port->isOpen();
}

void SerialMonitor::setPortName(const QString &name)
{
    if (name == m_portName)
        return;
    const bool wasOpen = isOpen();
    closePort();
    m_portName = name;
    if (wasOpen)
        openPort();
    updateHeader();
}

bool SerialMonitor::openPort()
{
    ++m_reopenGeneration;   // cancels a pending reopenWithRetry()
    if (isOpen())
        return true;
    if (m_portName.isEmpty()) {
        emit message(tr("Serial Monitor: no port selected."), true);
        updateHeader();
        return false;
    }
    m_port->setPortName(m_portName);
    m_port->setBaudRate(m_baudCombo->currentData().toInt());
    m_port->setDataBits(QSerialPort::Data8);
    m_port->setParity(QSerialPort::NoParity);
    m_port->setStopBits(QSerialPort::OneStop);
    m_port->setFlowControl(QSerialPort::NoFlowControl);
    if (!m_port->open(QIODevice::ReadWrite)) {
        emit message(tr("Serial Monitor: cannot open %1: %2").arg(m_portName, m_port->errorString()), true);
        updateHeader();
        return false;
    }
    // Like the Arduino IDE: assert DTR and RTS (native-USB boards only send with
    // DTR set; both asserted is the "run" state of ESP32 auto-reset circuits).
    m_port->setDataTerminalReady(true);
    m_port->setRequestToSend(true);

    delete m_decoder;
    m_decoder = QTextCodec::codecForName("UTF-8")->makeDecoder();
    m_lastWasCR = false;
    updateHeader();
    return true;
}

void SerialMonitor::closePort()
{
    ++m_reopenGeneration;
    if (m_port && m_port->isOpen()) {
        onReadyRead();
        flushReceived();
        m_port->close();
    }
    if (m_header)
        updateHeader();
}

void SerialMonitor::reopenWithRetry(int attemptsLeft)
{
    const int generation = ++m_reopenGeneration;
    QTimer::singleShot(500, this, [this, generation, attemptsLeft] {
        if (generation != m_reopenGeneration || isOpen())
            return;   // superseded (closed/opened/port changed meanwhile)
        if (m_portName.isEmpty())
            return;
        m_port->setPortName(m_portName);
        m_port->setBaudRate(m_baudCombo->currentData().toInt());
        if (m_port->open(QIODevice::ReadWrite)) {
            m_port->setDataTerminalReady(true);
            m_port->setRequestToSend(true);
            delete m_decoder;
            m_decoder = QTextCodec::codecForName("UTF-8")->makeDecoder();
            m_lastWasCR = false;
            m_reopenGeneration = generation + 1;
            updateHeader();
            emit message(tr("Serial Monitor: reopened %1.").arg(m_portName), false);
        } else if (attemptsLeft > 1) {
            reopenWithRetry(attemptsLeft - 1);
        } else {
            emit message(tr("Serial Monitor: could not reopen %1: %2").arg(m_portName, m_port->errorString()), true);
            updateHeader();
        }
    });
}

void SerialMonitor::send()
{
    const QString text = m_input->text();
    if (!isOpen()) {
        emit message(tr("Serial Monitor: port is not open, nothing sent."), true);
        return;
    }
    QByteArray data = text.toUtf8();
    data += m_endingCombo->currentData().toByteArray();
    if (m_port->write(data) != data.size()) {
        emit message(tr("Serial Monitor: write failed: %1").arg(m_port->errorString()), true);
        return;
    }
    m_input->clear();
    emit sent(text);
}

void SerialMonitor::onReadyRead()
{
    if (!m_port->isOpen() || !m_decoder)
        return;
    const QByteArray data = m_port->readAll();
    if (data.isEmpty())
        return;
    // Normalize line endings (\r\n and lone \r become \n) and, if enabled, put
    // the arrival time in front of each new line: "12:34:56.789 -> text".
    const QString text = m_decoder->toUnicode(data);
    const bool stamps = m_timestampButton->isChecked();
    const QString stamp = stamps ? QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz -> ")) : QString();
    m_pending.reserve(m_pending.size() + text.size());
    for (QChar ch : text) {
        if (ch == QLatin1Char('\n') && m_lastWasCR) {
            m_lastWasCR = false;
            continue;   // second half of \r\n
        }
        m_lastWasCR = (ch == QLatin1Char('\r'));
        if (m_lastWasCR)
            ch = QLatin1Char('\n');
        if (m_atLineStart && stamps)
            m_pending += stamp;
        m_pending += ch;
        m_atLineStart = (ch == QLatin1Char('\n'));
    }
    if (!m_flushTimer->isActive())
        m_flushTimer->start();
}

void SerialMonitor::flushReceived()
{
    if (m_pending.isEmpty())
        return;
    QScrollBar *bar = m_view->verticalScrollBar();
    const bool atBottom = bar->value() >= bar->maximum() - 2;
    QTextCursor cursor(m_view->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(m_pending);
    m_pending.clear();
    if (atBottom)   // don't yank the view while the user scrolls back
        bar->setValue(bar->maximum());
}

void SerialMonitor::onError(QSerialPort::SerialPortError error)
{
    // ResourceError: device unplugged / reset away. Opening errors are reported by openPort().
    if (error == QSerialPort::ResourceError && m_port->isOpen()) {
        emit message(tr("Serial Monitor: %1 disconnected (%2).").arg(m_portName, m_port->errorString()), true);
        m_port->close();
        updateHeader();
    }
}

void SerialMonitor::onBaudChanged(int index)
{
    const int baud = m_baudCombo->itemData(index).toInt();
    QSettings().setValue(QStringLiteral("serial/baud"), baud);
    if (isOpen())
        m_port->setBaudRate(baud);
    updateHeader();
}

void SerialMonitor::setTheme(Theme::Mode mode)
{
    m_timestampButton->setIcon(Theme::svgIcon(QStringLiteral(":/icons/clock.svg"), Theme::colors(mode).text, 16));
}

void SerialMonitor::updateHeader()
{
    QString state;
    if (m_portName.isEmpty())
        state = tr("no port");
    else if (isOpen())
        state = tr("%1 · %2 baud").arg(m_portName).arg(m_baudCombo->currentData().toInt());
    else
        state = tr("%1 · closed").arg(m_portName);
    m_header->setText(tr("Serial Monitor  —  %1").arg(state));
}
