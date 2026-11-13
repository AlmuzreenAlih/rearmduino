#pragma once

#include <QAbstractItemView>
#include <QComboBox>

// QComboBox that announces when its popup is about to open, so the port list
// can be rescanned on demand instead of at startup or by polling.
class PortComboBox : public QComboBox
{
    Q_OBJECT

public:
    using QComboBox::QComboBox;

    void showPopup() override
    {
        if (!m_quiet)
            emit popupAboutToShow();
        QComboBox::showPopup();
    }

    bool isPopupVisible() const { return view() && view()->isVisible(); }

    // Re-open the popup (e.g. after its items changed) without triggering a new scan.
    void reshowPopup()
    {
        m_quiet = true;
        hidePopup();
        showPopup();
        m_quiet = false;
    }

signals:
    void popupAboutToShow();

private:
    bool m_quiet = false;
};
