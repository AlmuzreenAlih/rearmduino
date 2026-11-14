#pragma once

#include "Activation.h"

#include <QDialog>

class QLabel;
class QPushButton;
class QTimer;

// Shown instead of the main window when this PC has no valid license: the request code to give
// the developer, with Retry and Close. It asks the server in the background (and again every
// minute); once the server sends a license it is saved and exec() returns Accepted.
// Pending, denied and revoked machines all see this same window.
class ActivationWindow : public QDialog
{
    Q_OBJECT

public:
    explicit ActivationWindow(QWidget *parent = nullptr);

private:
    void sendRequest();
    void onResult(const Activation::Result &result);

    QLabel *m_status = nullptr;
    QPushButton *m_retry = nullptr;
    QTimer *m_autoRetry = nullptr;
    bool m_busy = false;
};
