#include "Spinner.h"

#include <QAbstractScrollArea>
#include <QComboBox>
#include <QEvent>
#include <QPainter>
#include <QTimer>

namespace {
constexpr int kSize = 16;        // spinner square
constexpr int kArrowWidth = 24;  // QComboBox::drop-down width in the stylesheet
}

Spinner::Spinner(QComboBox *combo)
    : QWidget(combo), m_combo(combo)
{
    init(combo);
}

Spinner::Spinner(QAbstractScrollArea *view)
    : QWidget(view), m_view(view)
{
    init(view);
}

int Spinner::size()
{
    return kSize;
}

void Spinner::init(QWidget *host)
{
    m_timer = new QTimer(this);
    setAttribute(Qt::WA_TransparentForMouseEvents);   // clicks still reach the combo / list
    m_timer->setInterval(40);
    connect(m_timer, &QTimer::timeout, this, [this] {
        m_angle = (m_angle + 24) % 360;
        update();
    });
    host->installEventFilter(this);
    hide();
}

void Spinner::setColors(const QColor &arc, const QColor &background)
{
    m_arc = arc;
    m_background = background;
    update();
}

void Spinner::start()
{
    place();
    show();
    raise();
    m_timer->start();
}

void Spinner::stop()
{
    m_timer->stop();
    hide();
}

void Spinner::place()
{
    if (m_combo) {
        // A bit wider than the arc so it covers the end of the text behind it.
        const int w = kSize + 8;
        setGeometry(m_combo->width() - kArrowWidth - w, (m_combo->height() - kSize) / 2 - 1, w, kSize + 2);
        return;
    }
    // Centred in the band between the items (viewport) and the view's bottom frame.
    const QRect vp = m_view->viewport()->geometry();
    const int bandTop = vp.bottom() + 1;
    const int bandHeight = m_view->height() - m_view->frameWidth() - bandTop;
    setGeometry((m_view->width() - kSize) / 2, bandTop + (bandHeight - kSize - 2) / 2, kSize, kSize + 2);
}

bool Spinner::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == m_combo || watched == m_view) && (event->type() == QEvent::Resize || event->type() == QEvent::Show))
        place();
    return QWidget::eventFilter(watched, event);
}

void Spinner::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (m_background.isValid())
        p.fillRect(rect(), m_background);
    QPen pen(m_arc.isValid() ? m_arc : palette().color(QPalette::Highlight), 2.2);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    const QRectF r(width() - kSize + 1.5, 2.5, kSize - 3, kSize - 3);
    p.drawArc(r, -m_angle * 16, 270 * 16);   // 3/4 circle, turning clockwise
}
