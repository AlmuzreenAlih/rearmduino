#include "BoardComboBox.h"

#include <QApplication>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QScreen>
#include <QSortFilterProxyModel>
#include <QVBoxLayout>

// Every whitespace-separated word must appear in the board name or its FQBN
// (item data, Qt::UserRole), case-insensitively. "mega 2560", "esp32 s3", "avr:nano".
// Boards whose *name* matches all words are listed before FQBN-only matches
// ("uno" -> Arduino UNO first, not Nano R4 via "renesas_uno"); otherwise the
// source (alphabetical) order is kept.
class BoardFilterModel : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setWords(const QString &text)
    {
        m_words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        invalidate();
        sort(m_words.isEmpty() ? -1 : 0);   // -1: plain source order
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        if (m_words.isEmpty())
            return true;
        const QModelIndex idx = sourceModel()->index(row, 0, parent);
        const QString name = idx.data(Qt::DisplayRole).toString();
        const QString fqbn = idx.data(Qt::UserRole).toString();
        for (const QString &w : m_words) {
            if (!name.contains(w, Qt::CaseInsensitive) && !fqbn.contains(w, Qt::CaseInsensitive))
                return false;
        }
        return true;
    }

    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override
    {
        const bool l = nameMatches(left), r = nameMatches(right);
        if (l != r)
            return l;
        return left.row() < right.row();
    }

private:
    bool nameMatches(const QModelIndex &idx) const
    {
        const QString name = idx.data(Qt::DisplayRole).toString();
        for (const QString &w : m_words) {
            if (!name.contains(w, Qt::CaseInsensitive))
                return false;
        }
        return true;
    }

    QStringList m_words;
};

namespace {
constexpr int kMinPopupWidth = 380;
constexpr int kVisibleRows = 14;
}

BoardComboBox::BoardComboBox(QWidget *parent)
    : QComboBox(parent)
{
}

void BoardComboBox::createPopup()
{
    // Child of the combo so the toolbar stylesheet cascades into it.
    m_popup = new QFrame(this, Qt::Popup);
    m_popup->setObjectName(QStringLiteral("boardPopup"));
    m_popup->setAttribute(Qt::WA_NoMouseReplay);   // clicking the combo closes, not reopens

    auto *layout = new QVBoxLayout(m_popup);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Search boards (name or FQBN)"));
    m_search->setClearButtonEnabled(true);
    m_search->installEventFilter(this);
    layout->addWidget(m_search);

    m_filter = new BoardFilterModel(this);
    m_filter->setSourceModel(model());

    m_list = new QListView;
    m_list->setObjectName(QStringLiteral("boardList"));
    m_list->setModel(m_filter);
    m_list->setUniformItemSizes(true);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setFocusPolicy(Qt::NoFocus);   // typing always goes to the search field
    layout->addWidget(m_list, 1);

    m_empty = new QLabel(tr("No matching boards"));
    m_empty->setObjectName(QStringLiteral("boardEmpty"));
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->hide();
    layout->addWidget(m_empty, 1);

    connect(m_search, &QLineEdit::textChanged, this, &BoardComboBox::applyFilter);
    connect(m_list, &QListView::clicked, this, &BoardComboBox::accept);
}

void BoardComboBox::showPopup()
{
    if (!m_popup)
        createPopup();
    if (m_filter->sourceModel() != model())
        m_filter->setSourceModel(model());

    m_search->blockSignals(true);
    m_search->clear();
    m_search->blockSignals(false);
    m_filter->setWords(QString());
    m_empty->hide();
    m_list->show();

    // Size: at least as wide as the combo, ~14 rows tall; below the combo, or
    // above it if there's no room.
    const int rowHeight = qMax(m_list->sizeHintForRow(0), fontMetrics().height() + 8);
    const int width = qMax(this->width(), kMinPopupWidth);
    const int height = m_search->sizeHint().height() + rowHeight * kVisibleRows + 2 * m_list->frameWidth() + 20;
    QPoint pos = mapToGlobal(QPoint(0, this->height()));
    if (const QScreen *s = screen()) {
        const QRect avail = s->availableGeometry();
        if (pos.y() + height > avail.bottom())
            pos.setY(mapToGlobal(QPoint(0, 0)).y() - height);
        pos.setX(qBound(avail.left(), pos.x(), avail.right() - width));
    }
    m_popup->setGeometry(QRect(pos, QSize(width, height)));
    m_popup->show();
    m_search->setFocus();

    const QModelIndex current = m_filter->mapFromSource(model()->index(currentIndex(), 0));
    if (current.isValid()) {
        m_list->setCurrentIndex(current);
        m_list->scrollTo(current, QAbstractItemView::PositionAtCenter);
    }
}

void BoardComboBox::hidePopup()
{
    if (m_popup)
        m_popup->hide();
    QComboBox::hidePopup();
}

void BoardComboBox::applyFilter(const QString &text)
{
    m_filter->setWords(text);
    const bool any = m_filter->rowCount() > 0;
    m_list->setVisible(any);
    m_empty->setVisible(!any);
    if (any)
        selectProxyRow(0);
}

void BoardComboBox::selectProxyRow(int row)
{
    const int count = m_filter->rowCount();
    if (count == 0)
        return;
    const QModelIndex idx = m_filter->index(qBound(0, row, count - 1), 0);
    m_list->setCurrentIndex(idx);
    m_list->scrollTo(idx);
}

void BoardComboBox::accept(const QModelIndex &proxyIndex)
{
    if (!proxyIndex.isValid())
        return;
    const int row = m_filter->mapToSource(proxyIndex).row();
    hidePopup();
    setCurrentIndex(row);
    emit activated(row);
}

// Keys typed in the search field: navigation goes to the list.
bool BoardComboBox::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_search && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        const int row = m_list->currentIndex().row();
        const int page = qMax(1, kVisibleRows - 1);
        switch (key->key()) {
        case Qt::Key_Down:     selectProxyRow(row + 1); return true;
        case Qt::Key_Up:       selectProxyRow(row - 1); return true;
        case Qt::Key_PageDown: selectProxyRow(row + page); return true;
        case Qt::Key_PageUp:   selectProxyRow(row - page); return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:    accept(m_list->currentIndex()); return true;
        case Qt::Key_Escape:   hidePopup(); return true;
        default: break;
        }
    }
    return QComboBox::eventFilter(watched, event);
}
