#pragma once

#include <QColor>
#include <QWidget>

class QAbstractScrollArea;
class QComboBox;
class QTimer;

// Small spinning arc shown while a list is loading (boards) or being scanned
// (ports). Hidden otherwise. Two placements:
//  - in a combo box, just left of its arrow;
//  - in a list popup, centred in the empty band below the items (the view keeps
//    that band free with a bottom viewport margin).
class Spinner : public QWidget
{
public:
    explicit Spinner(QComboBox *combo);
    explicit Spinner(QAbstractScrollArea *view);

    static int size();      // the arc's square, in pixels
    static bool enabled();  // false: start() does nothing (spinners hidden for now)

    void setColors(const QColor &arc, const QColor &background);
    void start();
    void stop();

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void init(QWidget *host);
    void place();

    QComboBox *m_combo = nullptr;
    QAbstractScrollArea *m_view = nullptr;
    QTimer *m_timer = nullptr;
    int m_angle = 0;
    QColor m_arc;
    QColor m_background;
};
