#pragma once

#include "Theme.h"

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
class QToolBar;
class QsciScintilla;
class ArduinoLexer;
class PortComboBox;
class SerialMonitor;

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
    void updateMarginWidth();
    void toggleSerialMonitor(bool show);

private:
    void reopenSerialAfterUpload();
    void themeSplitterHandles();
    void setupEditor();
    void setupToolbar();
    void setupMenus();
    QWidget *createOutputPane();
    void resolveSketch(const QString &path);
    bool saveSketch();

    void applyTheme(Theme::Mode mode);
    void recolorOutput();

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

    void appendLog(const QString &line);                       // kind detected from text
    void appendLog(const QString &line, Theme::LogKind kind);
    void flushPartialLine();
    void setBusy(bool busy);

    Theme::Mode m_theme = Theme::Mode::Dark;

    QsciScintilla *m_editor = nullptr;
    ArduinoLexer *m_lexer = nullptr;
    QPlainTextEdit *m_output = nullptr;
    QSplitter *m_topSplitter = nullptr;   // editor | (future) serial monitor
    QSplitter *m_vSplitter = nullptr;     // top area / output pane
    QToolBar *m_toolbar = nullptr;
    QWidget *m_outputPane = nullptr;
    QAction *m_verifyAction = nullptr;
    QAction *m_uploadAction = nullptr;
    QAction *m_serialAction = nullptr;
    SerialMonitor *m_serial = nullptr;     // created on first open
    bool m_reopenSerialAfterUpload = false;
    QAction *m_darkThemeAction = nullptr;
    QAction *m_lightThemeAction = nullptr;
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
