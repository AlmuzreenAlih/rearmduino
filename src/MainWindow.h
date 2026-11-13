#pragma once

#include <QMainWindow>
#include <QProcess>
#include <QString>

#include <functional>

class QAction;
class QComboBox;
class QJsonObject;
class QPlainTextEdit;
class QSplitter;
class QTextDecoder;
class QsciScintilla;
class PortComboBox;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const QString &sketchPath, QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void verify();
    void upload();
    void loadBoards();
    void scanPorts();
    void onPortActivated(int index);
    void onProcessOutput();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError error);

private:
    void setupEditor();
    void setupToolbar();
    void resolveSketch(const QString &path);
    bool saveSketch();

    QString cliPath();
    void startTask(const QString &name, const QStringList &args);
    QProcess *startCliJson(const QStringList &args,
                           std::function<void(const QJsonObject &)> onSuccess,
                           std::function<void()> onDone);
    void onBoardsLoaded(const QJsonObject &json);
    void onPortsScanned(const QJsonObject &json);
    void startUpload();

    void selectBoard(const QString &fqbn, const QString &name);
    QString currentFqbn() const;
    QString currentPort() const;

    void appendLog(const QString &line);
    void flushPartialLine();
    void setBusy(bool busy);

    QsciScintilla *m_editor = nullptr;
    QPlainTextEdit *m_output = nullptr;
    QSplitter *m_topSplitter = nullptr;   // editor | (future) serial monitor
    QAction *m_verifyAction = nullptr;
    QAction *m_uploadAction = nullptr;
    QComboBox *m_boardCombo = nullptr;
    PortComboBox *m_portCombo = nullptr;

    // Verify/Upload process (output streamed to the log pane).
    QProcess *m_process = nullptr;
    QTextDecoder *m_decoder = nullptr;
    QString m_partialLine;
    QString m_taskName;

    // Background JSON queries.
    QProcess *m_boardsProcess = nullptr;
    QProcess *m_portsProcess = nullptr;
    bool m_uploadAfterScan = false;

    QString m_cliPath;
    QString m_sketchDir;   // folder passed to arduino-cli
    QString m_sketchFile;  // <dir>/<dirname>.ino
};
