#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QWidget>

class QLabel;
class QToolButton;
class QTreeWidget;

// "Serial Command Guide" tab of the bottom pane: a reference list of the
// sketch's serial commands (command text + name). It only lists them; nothing
// is sent from here. Stored per sketch in <sketch>/serial-commands.json.
class SerialCommandGuide : public QWidget
{
    Q_OBJECT

public:
    explicit SerialCommandGuide(QWidget *parent = nullptr);

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
    bool runDialog(Command &cmd, int editingIndex, const QString &title);
    void showMenu(const QPoint &pos);
    void setStatus(const QString &text, bool isError);

    QString m_dir;
    QDateTime m_mtime;      // of the loaded file (null: no file)
    QString m_loadError;    // file exists but can't be used: shown, never overwritten
    QList<Command> m_commands;

    QTreeWidget *m_list = nullptr;
    QToolButton *m_add = nullptr;
    QLabel *m_empty = nullptr;
    QLabel *m_status = nullptr;
};
