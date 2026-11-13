#pragma once

#include <QComboBox>

class QFrame;
class QLabel;
class QLineEdit;
class QListView;
class BoardFilterModel;

// QComboBox whose dropdown is a searchable list: a search field on top filters
// the boards (case-insensitive; every word must match the name or the FQBN).
class BoardComboBox : public QComboBox
{
    Q_OBJECT

public:
    explicit BoardComboBox(QWidget *parent = nullptr);

    void showPopup() override;
    void hidePopup() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void createPopup();
    void applyFilter(const QString &text);
    void selectProxyRow(int row);
    void accept(const QModelIndex &proxyIndex);

    QFrame *m_popup = nullptr;
    QLineEdit *m_search = nullptr;
    QListView *m_list = nullptr;
    QLabel *m_empty = nullptr;
    BoardFilterModel *m_filter = nullptr;
};
