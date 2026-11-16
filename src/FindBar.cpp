#include "FindBar.h"

#include <Qsci/qsciscintilla.h>

#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QStyle>
#include <QToolButton>

#include <algorithm>

namespace {

QToolButton *makeButton(const QString &text, const QString &toolTip, const char *objectName)
{
    auto *button = new QToolButton;
    button->setText(text);
    button->setToolTip(toolTip);
    button->setObjectName(QLatin1String(objectName));   // styled like the Serial Monitor's small buttons
    button->setFocusPolicy(Qt::NoFocus);                // typing stays in the fields
    return button;
}

bool isWordChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

// Offset in `text` of the start of each line (lines end with '\n', as QScintilla counts them).
QVector<int> lineStarts(const QString &text)
{
    QVector<int> starts{0};
    for (int i = text.indexOf(QLatin1Char('\n')); i != -1; i = text.indexOf(QLatin1Char('\n'), i + 1))
        starts.append(i + 1);
    return starts;
}

// QScintilla's line "index" counts characters (code points); QString counts UTF-16 units.
int toIndex(const QString &text, int lineStart, int offset)
{
    return text.mid(lineStart, offset - lineStart).toUcs4().size();
}

int toOffset(const QString &text, int lineStart, int index)
{
    int offset = lineStart;
    for (int i = 0; i < index && offset < text.size(); ++i)
        offset += text.at(offset).isHighSurrogate() ? 2 : 1;
    return qMin(offset, text.size());
}

} // namespace

FindBar::FindBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("findBar"));
    setAttribute(Qt::WA_StyledBackground);
    auto *layout = new QGridLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setHorizontalSpacing(6);
    layout->setVerticalSpacing(4);

    m_find = new QLineEdit;
    m_find->setPlaceholderText(tr("Find"));
    m_find->setClearButtonEnabled(true);
    m_find->installEventFilter(this);
    m_status = new QLabel;
    m_status->setObjectName(QStringLiteral("manualStatus"));
    m_status->setMinimumWidth(m_status->fontMetrics().horizontalAdvance(tr("999 of 999")) + 8);
    auto *previous = makeButton(tr("Previous"), tr("Previous match (Shift+Enter, Shift+F3)"), "clearButton");
    auto *next = makeButton(tr("Next"), tr("Next match (Enter, F3)"), "clearButton");
    m_matchCase = makeButton(QStringLiteral("Aa"), tr("Match case"), "timestampButton");
    m_matchCase->setCheckable(true);
    m_wholeWord = makeButton(tr("Word"), tr("Whole words only"), "timestampButton");
    m_wholeWord->setCheckable(true);
    auto *close = makeButton(QStringLiteral("✕"), tr("Close (Esc)"), "clearButton");

    auto *findButtons = new QHBoxLayout;
    findButtons->setSpacing(6);
    findButtons->addWidget(m_status);
    findButtons->addWidget(previous);
    findButtons->addWidget(next);
    findButtons->addWidget(m_matchCase);
    findButtons->addWidget(m_wholeWord);
    findButtons->addStretch(1);
    findButtons->addWidget(close);
    layout->addWidget(m_find, 0, 0);
    layout->addLayout(findButtons, 0, 1);

    // Replace row: its own widget so it can be hidden as a whole.
    m_replace = new QLineEdit;
    m_replace->setPlaceholderText(tr("Replace with"));
    m_replace->installEventFilter(this);
    auto *replaceOneButton = makeButton(tr("Replace"), tr("Replace this match and go to the next (Enter)"), "clearButton");
    auto *replaceAllButton = makeButton(tr("Replace All"), tr("Replace every match in this file"), "clearButton");
    m_replaceRow = new QWidget;
    auto *replaceButtons = new QHBoxLayout(m_replaceRow);
    replaceButtons->setContentsMargins(0, 0, 0, 0);
    replaceButtons->setSpacing(6);
    replaceButtons->addWidget(replaceOneButton);
    replaceButtons->addWidget(replaceAllButton);
    replaceButtons->addStretch(1);
    layout->addWidget(m_replace, 1, 0);
    layout->addWidget(m_replaceRow, 1, 1);
    layout->setColumnStretch(0, 2);
    layout->setColumnStretch(1, 3);

    connect(m_find, &QLineEdit::textChanged, this, &FindBar::searchAsTyped);
    connect(m_matchCase, &QToolButton::toggled, this, &FindBar::searchAsTyped);
    connect(m_wholeWord, &QToolButton::toggled, this, &FindBar::searchAsTyped);
    connect(previous, &QToolButton::clicked, this, [this] { findNext(false); });
    connect(next, &QToolButton::clicked, this, [this] { findNext(true); });
    connect(replaceOneButton, &QToolButton::clicked, this, &FindBar::replaceOne);
    connect(replaceAllButton, &QToolButton::clicked, this, &FindBar::replaceAll);
    connect(close, &QToolButton::clicked, this, &FindBar::closeBar);
    hide();
}

void FindBar::setEditor(QsciScintilla *editor)
{
    m_editor = editor;
    if (isVisible())
        showCount(-1, matches(m_editor ? m_editor->text() : QString()).size());
}

void FindBar::setTheme(Theme::Mode mode)
{
    m_mode = mode;
    const Theme::Colors c = Theme::colors(mode);
    // The app's stylesheet plus this bar's own background and top border.
    setStyleSheet(Theme::styleSheet(mode)
                  + QStringLiteral("QWidget#findBar { background: %1; border-top: 1px solid %2; }")
                        .arg(c.window.name(), c.border.name()));
}

void FindBar::open(bool withReplace)
{
    m_replace->setVisible(withReplace);
    m_replaceRow->setVisible(withReplace);
    if (m_editor && m_editor->hasSelectedText()) {
        const QString selected = m_editor->selectedText();
        if (!selected.contains(QLatin1Char('\n')) && !selected.contains(QChar(0x2029)))
            m_find->setText(selected);
    }
    show();
    m_find->setFocus();
    m_find->selectAll();
    searchAsTyped();
}

void FindBar::closeBar()
{
    hide();
    if (m_editor)
        m_editor->setFocus();
}

QString FindBar::findText() const
{
    return m_find->text();
}

void FindBar::setFindText(const QString &text)
{
    m_find->setText(text);
}

void FindBar::setReplaceText(const QString &text)
{
    m_replace->setText(text);
}

void FindBar::setOptions(bool matchCase, bool wholeWord)
{
    m_matchCase->setChecked(matchCase);
    m_wholeWord->setChecked(wholeWord);
}

QString FindBar::status() const
{
    return m_status->text();
}

// Every match in `text`, in order, not overlapping.
QVector<FindBar::Match> FindBar::matches(const QString &text) const
{
    QVector<Match> found;
    const QString needle = m_find->text();
    if (needle.isEmpty())
        return found;
    const Qt::CaseSensitivity cs = m_matchCase->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    int from = 0;
    while (true) {
        const int at = text.indexOf(needle, from, cs);
        if (at == -1)
            break;
        const int end = at + needle.size();
        const bool wordOk = !m_wholeWord->isChecked()
                            || ((at == 0 || !isWordChar(text.at(at - 1))) && (end >= text.size() || !isWordChar(text.at(end))));
        if (wordOk) {
            found.append({at, needle.size()});
            from = end;
        } else {
            from = at + 1;
        }
    }
    return found;
}

// The editor's selection (or the cursor) as offsets in `text`.
void FindBar::selectionRange(const QString &text, int *start, int *end) const
{
    int lineFrom, indexFrom, lineTo, indexTo;
    if (m_editor->hasSelectedText()) {
        m_editor->getSelection(&lineFrom, &indexFrom, &lineTo, &indexTo);
    } else {
        m_editor->getCursorPosition(&lineFrom, &indexFrom);
        lineTo = lineFrom;
        indexTo = indexFrom;
    }
    const QVector<int> starts = lineStarts(text);
    *start = toOffset(text, starts.value(lineFrom, text.size()), indexFrom);
    *end = toOffset(text, starts.value(lineTo, text.size()), indexTo);
}

void FindBar::selectMatch(const QString &text, const Match &match)
{
    const QVector<int> starts = lineStarts(text);
    auto lineOf = [&starts](int offset) {
        return int(std::upper_bound(starts.begin(), starts.end(), offset) - starts.begin()) - 1;
    };
    const int lineFrom = lineOf(match.start);
    const int lineTo = lineOf(match.start + match.length);
    m_editor->setSelection(lineFrom, toIndex(text, starts.at(lineFrom), match.start),
                           lineTo, toIndex(text, starts.at(lineTo), match.start + match.length));
    m_editor->ensureLineVisible(lineFrom);
    m_editor->ensureCursorVisible();
}

bool FindBar::findNext(bool forward)
{
    if (!m_editor)
        return false;
    const QString text = m_editor->text();
    const QVector<Match> found = matches(text);
    if (found.isEmpty()) {
        showCount(-1, 0);
        return false;
    }
    int selStart, selEnd;
    selectionRange(text, &selStart, &selEnd);
    int pick = -1;
    if (forward) {
        // The first match at or after the end of the selection; else wrap to the first.
        for (int i = 0; i < found.size() && pick == -1; ++i) {
            if (found.at(i).start >= selEnd)
                pick = i;
        }
        if (pick == -1)
            pick = 0;
    } else {
        // The last match that ends at or before the start of the selection; else wrap to the last.
        for (int i = found.size() - 1; i >= 0 && pick == -1; --i) {
            if (found.at(i).start + found.at(i).length <= selStart)
                pick = i;
        }
        if (pick == -1)
            pick = found.size() - 1;
    }
    selectMatch(text, found.at(pick));
    showCount(pick, found.size());
    return true;
}

// While typing: the first match at or after where the selection starts, so the match under
// the cursor grows with the typed text instead of jumping ahead.
void FindBar::searchAsTyped()
{
    if (!m_editor || !isVisible())
        return;
    const QString text = m_editor->text();
    const QVector<Match> found = matches(text);
    if (found.isEmpty()) {
        showCount(-1, 0);
        return;
    }
    int selStart, selEnd;
    selectionRange(text, &selStart, &selEnd);
    int pick = 0;
    for (int i = 0; i < found.size(); ++i) {
        if (found.at(i).start >= selStart) {
            pick = i;
            break;
        }
    }
    selectMatch(text, found.at(pick));
    showCount(pick, found.size());
}

bool FindBar::replaceOne()
{
    if (!m_editor || m_editor->isReadOnly())
        return false;
    const QString text = m_editor->text();
    int selStart, selEnd;
    selectionRange(text, &selStart, &selEnd);
    bool replaced = false;
    for (const Match &match : matches(text)) {
        if (match.start == selStart && match.start + match.length == selEnd) {
            m_editor->replaceSelectedText(m_replace->text());   // leaves the cursor after the new text
            replaced = true;
            break;
        }
    }
    findNext(true);
    return replaced;
}

int FindBar::replaceAll()
{
    if (!m_editor || m_editor->isReadOnly())
        return 0;
    const QString text = m_editor->text();
    const QVector<Match> found = matches(text);
    if (found.isEmpty()) {
        showCount(-1, 0);
        return 0;
    }
    // Last to first, so the offsets of the matches still to do stay valid. One undo step.
    m_editor->beginUndoAction();
    for (int i = found.size() - 1; i >= 0; --i) {
        selectMatch(text, found.at(i));
        m_editor->replaceSelectedText(m_replace->text());
    }
    m_editor->endUndoAction();
    showStatus(tr("Replaced %1").arg(found.size()));
    return found.size();
}

void FindBar::showCount(int current, int total)
{
    if (m_find->text().isEmpty())
        showStatus(QString());
    else if (total == 0)
        showStatus(tr("No results"), true);
    else if (current >= 0)
        showStatus(tr("%1 of %2").arg(current + 1).arg(total));
    else
        showStatus(total == 1 ? tr("1 match") : tr("%1 matches").arg(total));
}

void FindBar::showStatus(const QString &text, bool error)
{
    m_status->setText(text);
    if (m_status->property("error").toBool() != error) {
        m_status->setProperty("error", error);   // QLabel#manualStatus[error="true"] in the stylesheet
        m_status->style()->unpolish(m_status);
        m_status->style()->polish(m_status);
    }
}

bool FindBar::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) {
            closeBar();
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            if (watched == m_replace)
                replaceOne();
            else
                findNext(!(key->modifiers() & Qt::ShiftModifier));
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
