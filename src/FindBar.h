#pragma once

#include "Theme.h"

#include <QVector>
#include <QWidget>

class QLabel;
class QLineEdit;
class QToolButton;
class QsciScintilla;

// Find / Replace bar shown under the editor (Edit > Find, Edit > Replace). Created on first use.
// It searches the editor's text itself instead of using QScintilla's findFirst(), so Next,
// Previous, wrapping, the "3 of 12" count and Replace All all work from one list of matches.
class FindBar : public QWidget
{
    Q_OBJECT

public:
    explicit FindBar(QWidget *parent = nullptr);

    void setEditor(QsciScintilla *editor);   // the current tab's editor
    void setTheme(Theme::Mode mode);

    // Shows the bar (with the replace row or without), takes the editor's one-line selection as
    // the text to find, and focuses the find field.
    void open(bool withReplace);
    void closeBar();   // hides it and returns focus to the editor

    QString findText() const;
    void setFindText(const QString &text);
    void setReplaceText(const QString &text);
    void setOptions(bool matchCase, bool wholeWord);

    // Selects the next/previous match from the selection, wrapping around. False if none.
    bool findNext(bool forward);
    bool replaceOne();   // replaces the selected match (if the selection is one), then finds the next
    int replaceAll();    // one undo step; returns how many were replaced
    QString status() const;   // "3 of 12", "No results", "Replaced 4"

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Match {
        int start;    // offset in the editor's text (UTF-16 units, like QString)
        int length;
    };

    QVector<Match> matches(const QString &text) const;
    void selectMatch(const QString &text, const Match &match);
    void selectionRange(const QString &text, int *start, int *end) const;
    void searchAsTyped();
    void showStatus(const QString &text, bool error = false);
    void showCount(int current, int total);

    QsciScintilla *m_editor = nullptr;
    Theme::Mode m_mode = Theme::Mode::Dark;
    QLineEdit *m_find = nullptr;
    QLineEdit *m_replace = nullptr;
    QWidget *m_replaceRow = nullptr;
    QLabel *m_status = nullptr;
    QToolButton *m_matchCase = nullptr;
    QToolButton *m_wholeWord = nullptr;
};
