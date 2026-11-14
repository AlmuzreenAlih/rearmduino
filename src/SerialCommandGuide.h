#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QWidget>

#include <functional>

class QLabel;
class QToolButton;
class QVBoxLayout;

// "Serial Command Guide" tab of the bottom pane: the sketch's serial commands,
// one row each: [command] - name. Clicking the command button sends it.
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

private:
    struct Command {
        QString command;   // the serial text, e.g. "MIXER_HOME"
        QString name;      // what it does, e.g. "Mixer Home"
    };

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
};
