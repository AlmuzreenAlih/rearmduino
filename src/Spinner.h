#pragma once

#include <QColor>
#include <QWidget>

class QComboBox;
class QTimer;

// Small spinning arc drawn inside a combo box, just left of its arrow, while its
// list is loading (boards) or being scanned (ports). Hidden otherwise.
class Spinner : public QWidget
{
public:
    explicit Spinner(QComboBox *combo);

    void setColors(const QColor &arc, const QColor &background);
    void start();
    void stop();

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void place();

    QComboBox *m_combo;
    QTimer *m_timer;
    int m_angle = 0;
    QColor m_arc;
    QColor m_background;
};
