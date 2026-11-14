#include "PortComboBox.h"
#include "Spinner.h"

#include <QApplication>
#include <QListView>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QStylePainter>

namespace {

// Draws text left-aligned in rect, the status word (if any) bold in its color.
void drawPortText(QPainter *p, const QRect &rect, const QString &text, const QFont &font,
                  const QColor &normal, const PortComboBox *combo)
{
    QString token = PortComboBox::pluggedText();
    QColor tokenColor = combo->pluggedColor();
    int at = text.indexOf(token);
    if (at < 0) {
        token = PortComboBox::disconnectedText();
        tokenColor = combo->disconnectedColor();
        at = text.indexOf(token);
    }
    QFont bold(font);
    bold.setBold(true);

    p->save();
    p->setClipRect(rect);
    int x = rect.left();
    auto draw = [&](const QString &part, const QFont &f, const QColor &color) {
        if (part.isEmpty() || x > rect.right())
            return;
        const QFontMetrics fm(f);
        const QString shown = fm.elidedText(part, Qt::ElideRight, rect.right() - x + 1);
        p->setFont(f);
        p->setPen(color.isValid() ? color : normal);
        p->drawText(QRect(x, rect.top(), rect.right() - x + 1, rect.height()),
                    Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, shown);
        x += fm.horizontalAdvance(shown);
    };
    if (at < 0) {
        draw(text, font, normal);
    } else {
        draw(text.left(at), font, normal);
        draw(token, bold, tokenColor);
        draw(text.mid(at + token.size()), font, normal);
    }
    p->restore();
}

// Popup items: the style draws the background (hover, selection); the text is ours.
class PortItemDelegate : public QStyledItemDelegate
{
public:
    explicit PortItemDelegate(PortComboBox *combo) : QStyledItemDelegate(combo), m_combo(combo) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const QString text = opt.text;
        const QWidget *widget = opt.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();
        const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, widget);
        opt.text.clear();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);
        const int margin = style->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, widget) + 1;
        const QColor normal = opt.palette.color((opt.state & QStyle::State_Selected) ? QPalette::HighlightedText
                                                                                     : QPalette::Text);
        drawPortText(painter, textRect.adjusted(margin, 0, -margin, 0), text, opt.font, normal, m_combo);
    }

private:
    PortComboBox *m_combo;
};

}

// The popup's list, with an optional empty band below the items for the spinner.
// A viewport margin, so QComboBox sizes the popup to include it.
class PortListView : public QListView
{
public:
    void setBottomSpace(int pixels) { setViewportMargins(0, 0, 0, pixels); }
};

PortComboBox::PortComboBox(QWidget *parent)
    : QComboBox(parent), m_list(new PortListView)
{
    setView(m_list);
    setItemDelegate(new PortItemDelegate(this));
    m_popupSpinner = new Spinner(m_list);
}

void PortComboBox::setLoading(bool on)
{
    if (on == m_loading || !Spinner::enabled())
        return;
    m_loading = on;
    m_list->setBottomSpace(on ? Spinner::size() + 12 : 0);
    if (on)
        m_popupSpinner->start();
    else
        m_popupSpinner->stop();
    if (isPopupVisible())
        reshowPopup();   // grow/shrink the open list for the band
}

void PortComboBox::setSpinnerColor(const QColor &color)
{
    m_popupSpinner->setColors(color, QColor());   // no fill: the list's background shows
}

QString PortComboBox::pluggedText()
{
    return tr(" (PLUGGED)");
}

QString PortComboBox::disconnectedText()
{
    return tr(" (DISCONNECTED)");
}

void PortComboBox::setStatusColors(const QColor &plugged, const QColor &disconnected)
{
    m_plugged = plugged;
    m_disconnected = disconnected;
    update();
    if (view())
        view()->viewport()->update();
}

// As QComboBox::paintEvent, but the label text is drawn by drawPortText().
void PortComboBox::paintEvent(QPaintEvent *)
{
    QStylePainter painter(this);
    painter.setPen(palette().color(QPalette::Text));
    QStyleOptionComboBox opt;
    initStyleOption(&opt);
    const QString text = opt.currentText;
    painter.drawComplexControl(QStyle::CC_ComboBox, opt);
    opt.currentText.clear();
    painter.drawControl(QStyle::CE_ComboBoxLabel, opt);
    const QRect field = style()->subControlRect(QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxEditField, this);
    drawPortText(&painter, field.adjusted(1, 0, -1, 0), text, font(), palette().color(QPalette::Text), this);
}
