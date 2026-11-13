#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QWidget>

#include <functional>

class QKeyEvent;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QToolButton;
class QVBoxLayout;

// "Manual Control" tab of the bottom pane: user-defined buttons that write a
// string to the serial port, for testing a sketch by hand.
//  - Manual Relay Control: Toggle (sends on release) or Hold (sends on press
//    and again on release).
//  - Perform Functions: sends once per click (e.g. "!MIXER_HOME@").
// Each button can have a hotkey, active while this panel has keyboard focus.
// Stored per sketch in <sketch>/manual-control.json (shown read-only on the right).
class ManualControl : public QWidget
{
    Q_OBJECT

public:
    // Writes data to the port; returns an error text, or empty on success.
    using Sender = std::function<QString(const QByteArray &data, const QString &label)>;

    explicit ManualControl(Sender sender, QWidget *parent = nullptr);

    void setSketchDir(const QString &dir);   // loads <dir>/manual-control.json
    void reloadIfChanged();                  // picks up edits made outside the IDE
    static QString fileName();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum Group { Relays = 0, Functions = 1 };
    struct Item {
        QString name;
        QString hotkey;   // QKeySequence, portable text ("Q", "F1", "Shift+Q"); may be empty
        QString send;
        bool hold = false;     // relays: hold vs toggle
        bool latched = false;  // toggle relays: runtime ON state (not saved)
        bool down = false;     // hold relays: press sent, release pending
    };

    QVBoxLayout *addColumn(QLayout *row, const QString &title, Group group);
    void load();
    bool save();
    void rebuild();
    QString toJson() const;
    QString buttonText(const Item &item) const;
    QPushButton *createButton(Group group, int index);

    void addItem(Group group);
    void editItem(Group group, int index);
    void removeItem(Group group, int index);
    void moveItem(Group group, int index, int delta);
    bool runDialog(Group group, Item &item, int editingIndex, const QString &title);
    void showItemMenu(Group group, int index, const QPoint &globalPos);

    bool send(const Item &item, const QString &what);
    void holdPress(int index);
    void holdRelease(int index);
    void onToggleClicked(int index, bool checked);

    int findHotkey(Group *group, const QKeyEvent *event) const;
    bool handleKey(QKeyEvent *event, bool press);
    void releaseKeys();   // focus left the panel: end key holds
    void updateFocusHint();
    void setStatus(const QString &text, bool isError);

    Sender m_sender;
    QString m_dir;
    QDateTime m_mtime;      // of the loaded file (null: no file)
    QString m_loadError;    // file exists but can't be used: shown, never overwritten
    QString m_rawText;

    QList<Item> m_items[2];
    QList<QPushButton *> m_buttons[2];
    QVBoxLayout *m_lists[2] = {nullptr, nullptr};
    QToolButton *m_addButtons[2] = {nullptr, nullptr};
    QPlainTextEdit *m_json = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_focusHint = nullptr;

    struct KeyHold { Group group; int index; };
    QHash<int, KeyHold> m_keysDown;   // Qt key code -> item pressed with it
};
