#include "MainWindow.h"

#include <Qsci/qsciscintilla.h>
#include <Qsci/qscilexercpp.h>

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextCodec>
#include <QToolBar>

namespace {
const char kFqbn[] = "arduino:avr:uno";

const char kDefaultSketch[] =
    "void setup() {\n"
    "  // put your setup code here, to run once:\n"
    "}\n"
    "\n"
    "void loop() {\n"
    "  // put your main code here, to run repeatedly:\n"
    "}\n";
}

MainWindow::MainWindow(const QString &sketchPath, QWidget *parent)
    : QMainWindow(parent)
{
    // Toolbar: Verify | (Upload | Board | Port | Serial come later)
    QToolBar *toolbar = addToolBar(tr("Main"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_verifyAction = toolbar->addAction(tr("Verify"), this, &MainWindow::verify);
    m_verifyAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    m_verifyAction->setToolTip(tr("Compile the sketch (Ctrl+R)"));

    // Top: horizontal splitter, editor on the left; the Serial Monitor will be
    // added lazily on the right later.
    m_editor = new QsciScintilla;
    setupEditor();
    m_topSplitter = new QSplitter(Qt::Horizontal);
    m_topSplitter->addWidget(m_editor);

    // Bottom: output/logs pane spanning the full width.
    m_output = new QPlainTextEdit;
    m_output->setReadOnly(true);
    m_output->setMaximumBlockCount(10000);
    m_output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_output->setLineWrapMode(QPlainTextEdit::NoWrap);

    auto *vertical = new QSplitter(Qt::Vertical);
    vertical->addWidget(m_topSplitter);
    vertical->addWidget(m_output);
    vertical->setStretchFactor(0, 3);
    vertical->setStretchFactor(1, 1);
    setCentralWidget(vertical);

    resize(1000, 700);
    resolveSketch(sketchPath);
}

MainWindow::~MainWindow()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    delete m_decoder;
}

void MainWindow::setupEditor()
{
    const QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    auto *lexer = new QsciLexerCPP(m_editor);
    lexer->setDefaultFont(font);
    lexer->setFont(font);
    m_editor->setLexer(lexer);

    m_editor->setUtf8(true);
    m_editor->setMarginType(0, QsciScintilla::NumberMargin);
    m_editor->setMarginLineNumbers(0, true);
    m_editor->setMarginsFont(font);
    m_editor->setMarginWidth(0, QStringLiteral("00000"));
    m_editor->setIndentationsUseTabs(false);
    m_editor->setTabWidth(2);
    m_editor->setAutoIndent(true);
    m_editor->setBraceMatching(QsciScintilla::SloppyBraceMatch);
    m_editor->setCaretLineVisible(true);
}

// Accepts a sketch folder, an .ino file, or nothing (untitled temp sketch).
void MainWindow::resolveSketch(const QString &path)
{
    if (!path.isEmpty()) {
        QFileInfo info(path);
        m_sketchDir = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    } else {
        m_sketchDir = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                          .filePath(QStringLiteral("myide/sketch_untitled"));
    }
    m_sketchFile = QDir(m_sketchDir).filePath(QFileInfo(m_sketchDir).fileName() + QStringLiteral(".ino"));

    QFile file(m_sketchFile);
    if (file.open(QIODevice::ReadOnly)) {
        m_editor->setText(QString::fromUtf8(file.readAll()));
    } else {
        m_editor->setText(QString::fromLatin1(kDefaultSketch));
    }
    m_editor->setModified(false);
    setWindowTitle(QStringLiteral("%1 - myide").arg(QFileInfo(m_sketchFile).fileName()));
}

bool MainWindow::saveSketch()
{
    if (!QDir().mkpath(m_sketchDir)) {
        appendLog(tr("Error: cannot create sketch folder %1").arg(m_sketchDir));
        return false;
    }
    QFile file(m_sketchFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        appendLog(tr("Error: cannot write %1: %2").arg(m_sketchFile, file.errorString()));
        return false;
    }
    file.write(m_editor->text().toUtf8());
    m_editor->setModified(false);
    return true;
}

void MainWindow::verify()
{
    if (m_process && m_process->state() != QProcess::NotRunning)
        return;

    m_output->clear();
    if (!saveSketch())
        return;

    const QString cli = QStandardPaths::findExecutable(QStringLiteral("arduino-cli"));
    if (cli.isEmpty()) {
        appendLog(tr("Error: arduino-cli was not found on PATH."));
        return;
    }

    if (!m_process) {
        m_process = new QProcess(this);
        m_process->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_process, &QProcess::readyRead, this, &MainWindow::onProcessOutput);
        connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &MainWindow::onProcessFinished);
        connect(m_process, &QProcess::errorOccurred, this, &MainWindow::onProcessError);
    }

    delete m_decoder;
    m_decoder = QTextCodec::codecForName("UTF-8")->makeDecoder();
    m_partialLine.clear();

    const QStringList args{QStringLiteral("compile"), QStringLiteral("--fqbn"),
                           QString::fromLatin1(kFqbn), m_sketchDir};
    appendLog(QStringLiteral("> arduino-cli %1").arg(args.join(QLatin1Char(' '))));
    setBusy(true);
    m_process->start(cli, args);
}

void MainWindow::onProcessOutput()
{
    m_partialLine += m_decoder->toUnicode(m_process->readAll());
    int start = 0;
    int nl;
    while ((nl = m_partialLine.indexOf(QLatin1Char('\n'), start)) != -1) {
        QString line = m_partialLine.mid(start, nl - start);
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        appendLog(line);
        start = nl + 1;
    }
    m_partialLine.remove(0, start);
}

void MainWindow::flushPartialLine()
{
    if (!m_partialLine.isEmpty()) {
        appendLog(m_partialLine);
        m_partialLine.clear();
    }
}

void MainWindow::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    onProcessOutput();
    flushPartialLine();
    if (status == QProcess::CrashExit)
        appendLog(tr("arduino-cli crashed."));
    else if (exitCode == 0)
        appendLog(tr("Verify finished: OK."));
    else
        appendLog(tr("Verify failed (exit code %1).").arg(exitCode));
    setBusy(false);
}

void MainWindow::onProcessError(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart) {
        appendLog(tr("Error: could not start arduino-cli: %1").arg(m_process->errorString()));
        setBusy(false);
    }
}

void MainWindow::appendLog(const QString &line)
{
    m_output->appendPlainText(line);
    QScrollBar *bar = m_output->verticalScrollBar();
    bar->setValue(bar->maximum());
}

void MainWindow::setBusy(bool busy)
{
    m_verifyAction->setEnabled(!busy);
    statusBar()->showMessage(busy ? tr("Compiling...") : QString());
}
