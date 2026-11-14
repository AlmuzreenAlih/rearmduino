#pragma once

#include <QDialog>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <atomic>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QThread;

// Tools > Create Portable Version: copies this app, arduino-cli, the installed
// AVR core (with its tools), the chosen libraries and USB drivers into one
// folder (and optionally a .zip) that runs on another PC without installing
// anything. See Portable.h for the layout.
class PortableBuilder : public QDialog
{
    Q_OBJECT

public:
    PortableBuilder(const QString &cliPath, const QProcessEnvironment &cliEnv, const QString &sketchDir,
                    QWidget *parent = nullptr);
    ~PortableBuilder() override;

protected:
    void reject() override;

private:
    struct Job;   // everything the worker needs (plain data)

    void loadLibraries();
    void selectUsedBySketch();
    void setAllLibraries(bool checked);
    void updateLibraryCount();
    void build();
    void log(const QString &line, bool isError = false);
    void finished(bool ok, const QString &summary);
    QString cliConfig(const QString &key) const;

    QString m_cli;
    QProcessEnvironment m_env;
    QString m_sketchDir;
    QString m_userDir;   // arduino-cli directories.user (libraries/ inside)

    QLineEdit *m_outDir = nullptr;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_copyName = nullptr;   // who the copy is for (shown in the approval list)
    QListWidget *m_libs = nullptr;
    QLabel *m_libCount = nullptr;
    QLineEdit *m_ch340 = nullptr;
    QCheckBox *m_zip = nullptr;
    QProgressBar *m_progress = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QPushButton *m_buildButton = nullptr;
    QPushButton *m_closeButton = nullptr;

    QThread *m_worker = nullptr;
    std::atomic<bool> m_cancel{false};
};
