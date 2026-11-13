#pragma once

#include <QMainWindow>
#include <QProcess>
#include <QString>

class QAction;
class QPlainTextEdit;
class QSplitter;
class QTextDecoder;
class QsciScintilla;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const QString &sketchPath, QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void verify();
    void onProcessOutput();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError error);

private:
    void setupEditor();
    void resolveSketch(const QString &path);
    bool saveSketch();
    void appendLog(const QString &line);
    void flushPartialLine();
    void setBusy(bool busy);

    QsciScintilla *m_editor = nullptr;
    QPlainTextEdit *m_output = nullptr;
    QSplitter *m_topSplitter = nullptr;   // editor | (future) serial monitor
    QAction *m_verifyAction = nullptr;

    QProcess *m_process = nullptr;
    QTextDecoder *m_decoder = nullptr;
    QString m_partialLine;

    QString m_sketchDir;   // folder passed to arduino-cli
    QString m_sketchFile;  // <dir>/<dirname>.ino
};
