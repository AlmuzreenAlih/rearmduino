#pragma once

#include <QDateTime>
#include <QKeySequence>
#include <QList>
#include <QString>
#include <QWidget>

#include <functional>

class QLabel;
class QToolButton;
class QVBoxLayout;

// "Serial Command Guide" tab of the bottom pane: the sketch's serial commands,
// one row each: [command] (hotkey) - name. Clicking the command button, or
// pressing its hotkey while this tab shows (and no text field has focus), sends it.
// Stored per sketch in <sketch>/serial-commands.json.
class SerialCommandGuide : public QWidget
{
    Q_OBJECT

public:
    // Writes data to the port; returns an error text, or empty on success.
    using Sender = std::function<QString(const QByteArray &data, const QString &label)>;

    explicit SerialCommandGuide(Sender sender, QWidget *parent = nullptr);

    void setSketchDir(const QString &dir);   // loads <dir>/serial-commands.json
    void reloadIfChanged();                  // picks up edits made outside the IDE
    static QString fileName();

protected:
    // Hotkeys: an application-wide key filter while this tab is showing.
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    struct Command {
        QString command;      // the serial text, e.g. "MIXER_HOME"
        QString name;         // what it does, e.g. "Mixer Home"
        QKeySequence hotkey;  // optional single key (combination), e.g. Q
    };

    void updateKeysHint();

    void load();
    bool save();
    void rebuild();
    QString toJson() const;

    void addCommand();
    void editCommand(int index);
    void removeCommand(int index);
    void moveCommand(int index, int delta);
    void copyCommand(int index);
    void sendCommand(int index);
    bool runDialog(Command &cmd, int editingIndex, const QString &title);
    void showMenu(int index, const QPoint &globalPos);
    void setStatus(const QString &text, bool isError);

    Sender m_sender;
    QString m_dir;
    QDateTime m_mtime;      // of the loaded file (null: no file)
    QString m_loadError;    // file exists but can't be used: shown, never overwritten
    QList<Command> m_commands;

    QVBoxLayout *m_rows = nullptr;
    QToolButton *m_add = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_keysHint = nullptr;   // hotkeys on / paused while typing
};
