#pragma once

#include <QAbstractItemView>
#include <QColor>
#include <QComboBox>

// QComboBox that announces when its popup is about to open, so the port list
// can be rescanned on demand (arduino-cli is not polled). It also paints the
// " (PLUGGED)" / " (DISCONNECTED)" part of an item bold, in its status color.
class PortComboBox : public QComboBox
{
    Q_OBJECT

public:
    explicit PortComboBox(QWidget *parent = nullptr);

    // Status words put into item texts by MainWindow (with a leading space).
    static QString pluggedText();
    static QString disconnectedText();
    void setStatusColors(const QColor &plugged, const QColor &disconnected);
    QColor pluggedColor() const { return m_plugged; }
    QColor disconnectedColor() const { return m_disconnected; }

    void showPopup() override
    {
        if (!m_quiet)
            emit popupAboutToShow();
        QComboBox::showPopup();
    }

    bool isPopupVisible() const { return view() && view()->isVisible(); }

    // While scanning: a spinner centred at the bottom of the open list (the
    // toolbar's own spinner is hidden behind the popup).
    void setLoading(bool on);
    void setSpinnerColor(const QColor &color);

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

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    bool m_quiet = false;
    bool m_loading = false;
    class PortListView *m_list = nullptr;
    class Spinner *m_popupSpinner = nullptr;
    QColor m_plugged;
    QColor m_disconnected;
};
