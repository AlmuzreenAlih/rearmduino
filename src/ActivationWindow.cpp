#include "ActivationWindow.h"
#include "Theme.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

ActivationWindow::ActivationWindow(QWidget *parent)
    : QDialog(parent)
{
    // The main window normally sets the theme; it doesn't exist yet.
    const Theme::Mode mode = QSettings().value(QStringLiteral("view/theme")).toString() == QLatin1String("light")
                                 ? Theme::Mode::Light : Theme::Mode::Dark;
    qApp->setPalette(Theme::palette(mode));
    setStyleSheet(Theme::styleSheet(mode));
    const Theme::Colors colors = Theme::colors(mode);

    setWindowTitle(QStringLiteral("REARMduino"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->setSpacing(12);

    auto *title = new QLabel(tr("This copy is waiting for approval."));
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 3);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto *intro = new QLabel(tr("REARMduino has to be approved once for each PC. "
                                "Give this request code to REARM Automation:"));
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *codeRow = new QHBoxLayout;
    auto *code = new QLabel(Activation::requestCode());
    code->setFont(Theme::monoFont(20));
    code->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *copy = new QPushButton(tr("Copy"));
    connect(copy, &QPushButton::clicked, this, [] { QApplication::clipboard()->setText(Activation::requestCode()); });
    codeRow->addWidget(code);
    codeRow->addSpacing(12);
    codeRow->addWidget(copy);
    codeRow->addStretch(1);
    layout->addLayout(codeRow);

    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_status->setMinimumHeight(m_status->fontMetrics().lineSpacing() * 2);
    m_status->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_status->setStyleSheet(QStringLiteral("color: %1;").arg(colors.textMuted.name()));
    layout->addWidget(m_status);

    auto *buttons = new QDialogButtonBox;
    m_retry = buttons->addButton(tr("Retry"), QDialogButtonBox::ActionRole);
    QPushButton *close = buttons->addButton(QDialogButtonBox::Close);
    connect(m_retry, &QPushButton::clicked, this, &ActivationWindow::sendRequest);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    layout->addWidget(buttons);

    m_autoRetry = new QTimer(this);
    m_autoRetry->setSingleShot(true);
    m_autoRetry->setInterval(60 * 1000);
    connect(m_autoRetry, &QTimer::timeout, this, &ActivationWindow::sendRequest);

    setFixedWidth(460);
    // After the window is shown, so it appears at once even if the network is slow.
    QTimer::singleShot(0, this, &ActivationWindow::sendRequest);
}

void ActivationWindow::sendRequest()
{
    if (m_busy)
        return;
    m_busy = true;
    m_autoRetry->stop();
    m_retry->setEnabled(false);
    m_status->setText(tr("Contacting the server..."));
    Activation::check(this, [this](const Activation::Result &result) { onResult(result); });
}

void ActivationWindow::onResult(const Activation::Result &result)
{
    m_busy = false;
    m_retry->setEnabled(true);
    switch (result.status) {
    case Activation::Status::Approved:
        if (Activation::saveLicense(result.license)) {
            accept();
            return;
        }
        m_status->setText(tr("The server approved this PC, but its license could not be verified here. "
                             "Tell REARM Automation."));
        break;
    case Activation::Status::NotApproved:
        m_status->setText(tr("Not approved yet. This window checks again every minute, "
                             "and opens REARMduino as soon as it is approved."));
        break;
    case Activation::Status::Unreachable:
        m_status->setText(tr("Can't reach the server. Connect this PC to the internet, then click Retry."));
        break;
    case Activation::Status::ServerError:
        m_status->setText(tr("The server can't answer right now. Try again in a few minutes."));
        break;
    }
    m_autoRetry->start();
}
