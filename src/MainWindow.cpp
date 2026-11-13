#include "MainWindow.h"
#include "ArduinoLexer.h"
#include "BoardComboBox.h"
#include "PortComboBox.h"
#include "SerialMonitor.h"

#include <Qsci/qsciscintilla.h>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QMenu>
#include <QMenuBar>
#include <QRegularExpression>
#include <QScreen>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextCodec>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QVector>

#include <algorithm>

namespace {
const char kDefaultFqbn[] = "arduino:avr:uno";
const char kDefaultBoardName[] = "Arduino UNO";

// Item data roles.
constexpr int FqbnRole = Qt::UserRole;            // board combo + port combo (matched board)
constexpr int PortAddressRole = Qt::UserRole + 1; // port combo
constexpr int BoardNameRole = Qt::UserRole + 2;   // port combo (matched board)

constexpr qreal kOutputPointSize = 10;

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
    // Palette before any child widget exists. The stylesheet is applied per
    // widget in applyTheme() (not app-wide: that costs ~75 ms of startup on the Pi).
    m_theme = QSettings().value(QStringLiteral("view/theme")).toString() == QLatin1String("light")
                  ? Theme::Mode::Light : Theme::Mode::Dark;
    qApp->setPalette(Theme::palette(m_theme));

    loadPreferences();
    setupToolbar();

    // Top: horizontal splitter, editor tabs on the left; the Serial Monitor is
    // added lazily on the right. Editors are created by resolveSketch().
    m_tabs = new QTabWidget;
    m_tabs->setDocumentMode(true);   // flat: no frame around the editor
    m_tabs->tabBar()->setObjectName(QStringLiteral("sketchTabs"));
    m_tabs->tabBar()->setExpanding(false);
    m_tabs->tabBar()->setDrawBase(false);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (auto *editor = qobject_cast<QsciScintilla *>(m_tabs->widget(index)))
            m_editor = editor;
    });
    setupMenus();       // after the toolbar: menus reuse its actions
    applyShortcuts();   // defaults + Advanced Settings overrides
    m_topSplitter = new QSplitter(Qt::Horizontal);
    m_topSplitter->setChildrenCollapsible(false);
    m_topSplitter->addWidget(m_tabs);

    // Bottom: output/logs pane spanning the full width, 40% of the height at
    // startup (stretch 3:2 keeps the ratio when the window is resized).
    m_vSplitter = new QSplitter(Qt::Vertical);
    m_vSplitter->setChildrenCollapsible(false);
    m_vSplitter->setHandleWidth(3);
    m_vSplitter->addWidget(m_topSplitter);
    m_outputPane = createOutputPane();
    m_vSplitter->addWidget(m_outputPane);
    m_vSplitter->setStretchFactor(0, 3);
    m_vSplitter->setStretchFactor(1, 2);
    m_vSplitter->setSizes({600, 400});   // proportional; rescaled to the real height
    setCentralWidget(m_vSplitter);

    applyTheme(m_theme);
    // Default size, but never larger than 90% of the screen (the Pi is 1280x720).
    // Centered, so the window manager can't push the bottom off a small screen.
    QSize size(1100, 760);
    if (const QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect avail = screen->availableGeometry();
        size = size.boundedTo(avail.size() * 0.9);
        resize(size);
        move(avail.center() - QPoint(size.width() / 2, size.height() / 2));
    } else {
        resize(size);
    }
    m_sketchIsTemp = sketchPath.isEmpty();
    resolveSketch(sketchPath);
    if (!m_sketchIsTemp)
        addRecent(m_sketchDir);
    updateTitle();
    // Focus the editor once shown; otherwise activation focuses the first combo box
    // (amber focus ring at startup).
    QTimer::singleShot(0, this, [this] { if (m_editor) m_editor->setFocus(); });

    // Fill the board list once the event loop runs (after the window is shown).
    // Ports are only scanned on demand: `board list` can hit the network.
    QTimer::singleShot(0, this, &MainWindow::loadBoards);
}

MainWindow::~MainWindow()
{
    for (QProcess *p : {m_process, m_boardsProcess, m_portsProcess}) {
        if (p && p->state() != QProcess::NotRunning) {
            p->disconnect(this);
            p->kill();
            p->waitForFinished(1000);
        }
    }
    delete m_decoder;
}

void MainWindow::setupToolbar()
{
    QToolBar *toolbar = m_toolbar = addToolBar(tr("Main"));
    toolbar->setObjectName(QStringLiteral("mainToolbar"));
    toolbar->setMovable(false);
    toolbar->setContextMenuPolicy(Qt::PreventContextMenu);
    toolbar->setIconSize(QSize(18, 18));
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    // Shortcuts are set by applyShortcuts() (tooltips get the key appended there).
    m_verifyAction = toolbar->addAction(tr("Verify"), this, &MainWindow::verify);
    m_verifyAction->setToolTip(tr("Verify: compile the sketch"));
    toolbar->widgetForAction(m_verifyAction)->setObjectName(QStringLiteral("verifyButton"));

    m_uploadAction = toolbar->addAction(tr("Upload"), this, &MainWindow::upload);
    m_uploadAction->setToolTip(tr("Upload: compile and upload to the selected port"));
    toolbar->widgetForAction(m_uploadAction)->setObjectName(QStringLiteral("uploadButton"));
    // No focus on click; keyboard users have shortcuts. (Accessibility
    // "press" still focuses them: see setBusy().)
    for (QAction *a : {m_verifyAction, m_uploadAction})
        toolbar->widgetForAction(a)->setFocusPolicy(Qt::NoFocus);

    QSettings settings;

    // Board: starts with just the remembered board; the full list loads later.
    auto *boardLabel = new QLabel(tr("Board"));
    boardLabel->setContentsMargins(12, 0, 0, 0);
    toolbar->addWidget(boardLabel);
    m_boardCombo = new BoardComboBox;   // dropdown with a search field
    m_boardCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    // Narrow enough that the whole toolbar (incl. Serial) fits ~850 px, as on the
    // Pi; the popup still widens to fit long board names.
    m_boardCombo->setMinimumContentsLength(16);
    m_boardCombo->addItem(settings.value(QStringLiteral("board/name"), QString::fromLatin1(kDefaultBoardName)).toString(),
                          settings.value(QStringLiteral("board/fqbn"), QString::fromLatin1(kDefaultFqbn)).toString());
    m_boardCombo->setToolTip(currentFqbn());
    connect(m_boardCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index < 0)
            return;
        m_boardCombo->setToolTip(currentFqbn());
        QSettings s;
        s.setValue(QStringLiteral("board/fqbn"), currentFqbn());
        s.setValue(QStringLiteral("board/name"), m_boardCombo->currentText());
    });
    toolbar->addWidget(m_boardCombo);

    // Port: remembered port only; rescanned each time the dropdown opens.
    toolbar->addWidget(new QLabel(tr("Port")));
    m_portCombo = new PortComboBox;
    m_portCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_portCombo->setMinimumContentsLength(8);
    m_portCombo->setToolTip(tr("Open to scan for connected boards"));
    const QString savedPort = settings.value(QStringLiteral("port/address")).toString();
    if (!savedPort.isEmpty()) {
        m_portCombo->addItem(savedPort);
        m_portCombo->setItemData(0, savedPort, PortAddressRole);
    }
    connect(m_portCombo, &PortComboBox::popupAboutToShow, this, &MainWindow::scanPorts);
    connect(m_portCombo, QOverload<int>::of(&QComboBox::activated), this, &MainWindow::onPortActivated);
    connect(m_portCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index < 0)
            return;
        QSettings().setValue(QStringLiteral("port/address"), currentPort());
        if (m_serial)
            m_serial->setPortName(currentPort());   // follows the toolbar; reopens if open
    });
    toolbar->addWidget(m_portCombo);

    // Serial Monitor toggle, pushed to the right end.
    auto *spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);
    m_serialAction = toolbar->addAction(tr("Serial Monitor"));
    m_serialAction->setIconText(tr("Serial"));   // toolbar label; menus show the full text
    m_serialAction->setCheckable(true);
    m_serialAction->setToolTip(tr("Show/hide the Serial Monitor"));
    toolbar->widgetForAction(m_serialAction)->setObjectName(QStringLiteral("serialButton"));
    toolbar->widgetForAction(m_serialAction)->setFocusPolicy(Qt::NoFocus);
    connect(m_serialAction, &QAction::toggled, this, &MainWindow::toggleSerialMonitor);
}

QWidget *MainWindow::createOutputPane()
{
    auto *pane = new QWidget;
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QLabel(tr("Output"));
    header->setObjectName(QStringLiteral("outputHeader"));
    layout->addWidget(header);

    m_output = new QPlainTextEdit;
    m_output->setObjectName(QStringLiteral("output"));
    m_output->setReadOnly(true);
    m_output->setMaximumBlockCount(10000);
    m_output->setFont(Theme::monoFont(kOutputPointSize));
    m_output->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_output->setFrameShape(QFrame::NoFrame);
    layout->addWidget(m_output);
    return pane;
}

// ---------------------------------------------------------------------------
// Editor tabs
// ---------------------------------------------------------------------------

// One editor (with its own lexer: a lexer can only serve one editor) per file.
QsciScintilla *MainWindow::createEditor(const QString &filePath)
{
    auto *editor = new QsciScintilla;
    editor->setProperty("filePath", filePath);
    editor->setLexer(new ArduinoLexer(editor));
    setupEditor(editor);
    clearEditorBindings(editor);
    Theme::applyToEditor(editor, static_cast<QsciLexerCPP *>(editor->lexer()), m_theme,
                         Theme::monoFont(m_editorPointSize));
    connect(editor, &QsciScintilla::linesChanged, this, [this, editor] { updateMarginWidth(editor); });
    connect(editor, &QsciScintilla::modificationChanged, this, [this, editor] {
        updateTabTitle(editor);
        setWindowModified(anyModified());
    });
    return editor;
}

void MainWindow::setupEditor(QsciScintilla *editor)
{
    editor->setFrameShape(QFrame::NoFrame);
    editor->setUtf8(true);
    // Thin line-number margin, sized to the line count; margin 1 is a small gap
    // before the text. Both use the editor background (set by the theme).
    editor->setMarginType(0, QsciScintilla::NumberMargin);
    editor->setMarginLineNumbers(0, true);
    editor->setMarginType(1, QsciScintilla::SymbolMargin);
    editor->setMarginWidth(1, 12);
    editor->setMarginSensitivity(1, false);
    editor->setIndentationsUseTabs(false);
    editor->setTabWidth(m_tabWidth);
    editor->setAutoIndent(true);
    editor->setBraceMatching(QsciScintilla::SloppyBraceMatch);
    editor->setCaretLineVisible(true);
    editor->SendScintilla(QsciScintilla::SCI_SETCARETLINEVISIBLEALWAYS, 1);   // also when unfocused
    editor->setCaretWidth(2);

    // Scintilla reserves a fixed 2000 px scroll width by default, so the
    // horizontal scrollbar always shows. Start small and grow with the longest
    // line instead. (Raw messages: works on every QScintilla version.)
    editor->SendScintilla(QsciScintilla::SCI_SETSCROLLWIDTH, 1);
    editor->SendScintilla(QsciScintilla::SCI_SETSCROLLWIDTHTRACKING, 1);
}

QList<QsciScintilla *> MainWindow::editors() const
{
    QList<QsciScintilla *> list;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto *editor = qobject_cast<QsciScintilla *>(m_tabs->widget(i)))
            list << editor;
    }
    return list;
}

bool MainWindow::anyModified() const
{
    for (const QsciScintilla *editor : editors()) {
        if (editor->isModified())
            return true;
    }
    return false;
}

// Tab label: file name, with a dot while it has unsaved changes.
void MainWindow::updateTabTitle(QsciScintilla *editor)
{
    const int index = m_tabs->indexOf(editor);
    if (index < 0)
        return;
    const QString name = QFileInfo(editor->property("filePath").toString()).fileName();
    m_tabs->setTabText(index, editor->isModified() ? name + QStringLiteral("  ") + QChar(0x2022) : name);
    m_tabs->setTabToolTip(index, QDir::toNativeSeparators(editor->property("filePath").toString()));
}

// Accepts a sketch folder, an .ino/.pde file, or nothing (untitled temp sketch).
// Opens the main file plus every other .ino/.pde in the folder as tabs.
void MainWindow::resolveSketch(const QString &path)
{
    const QFileInfo info(path);
    if (!path.isEmpty() && info.isFile()) {
        m_sketchDir = info.absolutePath();
        m_sketchFile = info.absoluteFilePath();
    } else {
        m_sketchDir = !path.isEmpty() ? info.absoluteFilePath()
                                      : QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                            .filePath(QStringLiteral("myide/sketch_untitled"));
        m_sketchFile = QDir(m_sketchDir).filePath(QFileInfo(m_sketchDir).fileName() + QStringLiteral(".ino"));
    }

    // Close the previous sketch's tabs.
    m_editor = nullptr;
    while (m_tabs->count() > 0) {
        QWidget *w = m_tabs->widget(0);
        m_tabs->removeTab(0);
        delete w;
    }

    // Main file first, then the other sketch files alphabetically (like the Arduino IDE).
    QStringList files{m_sketchFile};
    const QStringList others = QDir(m_sketchDir).entryList({QStringLiteral("*.ino"), QStringLiteral("*.pde")},
                                                           QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QString &name : others) {
        const QString file = QDir(m_sketchDir).filePath(name);
        if (QFileInfo(file) != QFileInfo(m_sketchFile))
            files << file;
    }

    for (const QString &file : files) {
        QsciScintilla *editor = createEditor(file);
        QFile f(file);
        if (f.open(QIODevice::ReadOnly))
            editor->setText(QString::fromUtf8(f.readAll()));
        else if (file == m_sketchFile)
            editor->setText(QString::fromLatin1(kDefaultSketch));
        editor->setModified(false);
        m_tabs->addTab(editor, QString());
        updateTabTitle(editor);
        updateMarginWidth(editor);
    }
    m_tabs->setCurrentIndex(0);
    m_editor = editors().value(0);
}

// Writes every tab to its file (Verify/Upload do this too, since arduino-cli
// compiles all .ino files of the folder). Untitled / example sketches stay
// "modified" so closing still asks to Save As.
bool MainWindow::saveSketch()
{
    if (!QDir().mkpath(m_sketchDir)) {
        appendLog(tr("Error: cannot create sketch folder %1").arg(m_sketchDir));
        return false;
    }
    bool ok = true;
    for (QsciScintilla *editor : editors()) {
        const QString path = editor->property("filePath").toString();
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            appendLog(tr("Error: cannot write %1: %2").arg(QDir::toNativeSeparators(path), file.errorString()));
            ok = false;
            continue;
        }
        file.write(editor->text().toUtf8());
        if (!m_sketchIsTemp)
            editor->setModified(false);
    }
    return ok;
}

// Advanced Settings path if set and executable, otherwise PATH.
QString MainWindow::cliPath()
{
    if (m_cliPath.isEmpty()) {
        const QString custom = QSettings().value(QStringLiteral("advanced/cliPath")).toString();
        if (!custom.isEmpty()) {
            if (QFileInfo(custom).isExecutable())
                m_cliPath = custom;
            else
                appendLog(tr("Warning: arduino-cli path in Advanced Settings is not executable: %1. Using PATH.")
                              .arg(QDir::toNativeSeparators(custom)));
        }
    }
    if (m_cliPath.isEmpty())
        m_cliPath = QStandardPaths::findExecutable(QStringLiteral("arduino-cli"));
    if (m_cliPath.isEmpty())
        appendLog(tr("Error: arduino-cli was not found on PATH."));
    return m_cliPath;
}

QString MainWindow::currentFqbn() const
{
    return m_boardCombo->currentData(FqbnRole).toString();
}

QString MainWindow::currentPort() const
{
    return m_portCombo->currentData(PortAddressRole).toString();
}

// ---------------------------------------------------------------------------
// Verify / Upload
// ---------------------------------------------------------------------------

void MainWindow::verify()
{
    if (m_process && m_process->state() != QProcess::NotRunning)
        return;
    m_output->clear();
    if (currentFqbn().isEmpty()) {
        appendLog(tr("Error: no board selected."));
        return;
    }
    if (!saveSketch())
        return;
    startTask(tr("Verify"), QStringList{QStringLiteral("compile"), QStringLiteral("--fqbn"), currentFqbn()}
                                << compileOptions() << m_sketchDir);
}

void MainWindow::upload()
{
    if (m_process && m_process->state() != QProcess::NotRunning)
        return;
    m_output->clear();
    if (currentFqbn().isEmpty()) {
        appendLog(tr("Error: no board selected."));
        return;
    }
    if (currentPort().isEmpty()) {
        // No port chosen yet: scan once, auto-pick a single detected board, then upload.
        appendLog(tr("No port selected, scanning for boards..."), Theme::LogKind::Command);
        m_taskName = tr("Upload");
        m_uploadAfterScan = true;
        setBusy(true);
        scanPorts();
        return;
    }
    startUpload();
}

void MainWindow::startUpload()
{
    if (!saveSketch()) {
        setBusy(false);
        return;
    }
    // Free the port for the upload ("port busy" otherwise, especially on Windows);
    // reopened in reopenSerialAfterUpload() when the task ends.
    if (m_serial && m_serial->isOpen()) {
        appendLog(tr("Serial Monitor: closing %1 for upload.").arg(m_serial->portName()), Theme::LogKind::Command);
        m_serial->closePort();
        m_reopenSerialAfterUpload = true;
    }
    startTask(tr("Upload"), QStringList{QStringLiteral("compile"), QStringLiteral("--upload"),
                                        QStringLiteral("--fqbn"), currentFqbn(),
                                        QStringLiteral("--port"), currentPort()}
                                << compileOptions() << m_sketchDir);
}

void MainWindow::startTask(const QString &name, const QStringList &args)
{
    const QString cli = cliPath();
    if (cli.isEmpty()) {
        setBusy(false);
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
    m_taskName = name;

    appendLog(QStringLiteral("> arduino-cli %1").arg(args.join(QLatin1Char(' '))), Theme::LogKind::Command);
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
        appendLog(tr("arduino-cli crashed."), Theme::LogKind::Error);
    else if (exitCode == 0)
        appendLog(tr("%1 finished: OK.").arg(m_taskName), Theme::LogKind::Success);
    else
        appendLog(tr("%1 failed (exit code %2).").arg(m_taskName).arg(exitCode), Theme::LogKind::Error);
    setBusy(false);
    reopenSerialAfterUpload();
}

void MainWindow::onProcessError(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart) {
        appendLog(tr("Error: could not start arduino-cli: %1").arg(m_process->errorString()));
        setBusy(false);
        reopenSerialAfterUpload();
    }
}

// ---------------------------------------------------------------------------
// Serial Monitor
// ---------------------------------------------------------------------------

void MainWindow::toggleSerialMonitor(bool show)
{
    if (show && !m_serial) {
        // Created lazily on first open (CLAUDE.md startup rule).
        m_serial = new SerialMonitor;
        connect(m_serial, &SerialMonitor::sent, this, [this](const QString &text) {
            appendLog(QStringLiteral("Sent: ") + text, Theme::LogKind::Sent);
        });
        connect(m_serial, &SerialMonitor::message, this, [this](const QString &text, bool isError) {
            appendLog(text, isError ? Theme::LogKind::Error : Theme::LogKind::Command);
        });
        m_serial->setStyleSheet(Theme::styleSheet(m_theme));
        const int total = m_topSplitter->width();
        m_topSplitter->addWidget(m_serial);
        m_topSplitter->setStretchFactor(0, 3);
        m_topSplitter->setStretchFactor(1, 2);
        m_topSplitter->setSizes({total * 62 / 100, total * 38 / 100});
        themeSplitterHandles();
    }
    if (!m_serial)
        return;

    if (show) {
        m_serial->show();
        m_serial->setPortName(currentPort());
        m_serial->openPort();
        m_serial->findChild<QLineEdit *>()->setFocus();
    } else {
        m_serial->closePort();
        m_serial->hide();   // editor takes the full width
        m_reopenSerialAfterUpload = false;
    }
}

void MainWindow::reopenSerialAfterUpload()
{
    if (!m_reopenSerialAfterUpload)
        return;
    m_reopenSerialAfterUpload = false;
    if (m_serial && m_serial->isVisible()) {
        m_serial->setPortName(currentPort());
        m_serial->reopenWithRetry();
    }
}

// ---------------------------------------------------------------------------
// Background JSON queries (board list, port scan)
// ---------------------------------------------------------------------------

QProcess *MainWindow::startCliJson(const QStringList &args,
                                   std::function<void(const QJsonObject &)> onSuccess,
                                   std::function<void()> onDone)
{
    const QString cli = cliPath();
    if (cli.isEmpty()) {
        if (onDone)
            onDone();
        return nullptr;
    }

    auto *proc = new QProcess(this);
    const QString cmd = args.join(QLatin1Char(' '));
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc, cmd, onSuccess, onDone](int code, QProcess::ExitStatus status) {
        const QByteArray out = proc->readAllStandardOutput();
        const QByteArray err = proc->readAllStandardError();
        proc->deleteLater();
        if (status == QProcess::NormalExit && code == 0) {
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(out, &parseError);
            if (doc.isObject())
                onSuccess(doc.object());
            else
                appendLog(tr("Error: unexpected output from arduino-cli %1: %2").arg(cmd, parseError.errorString()));
        } else {
            const QString msg = QString::fromUtf8(err.isEmpty() ? out : err).trimmed();
            appendLog(tr("arduino-cli %1 failed: %2").arg(cmd, msg), Theme::LogKind::Error);
        }
        if (onDone)
            onDone();
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc, onDone](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        appendLog(tr("Error: could not start arduino-cli: %1").arg(proc->errorString()));
        proc->deleteLater();
        if (onDone)
            onDone();
    });
    proc->start(cli, args + QStringList{QStringLiteral("--json")});
    return proc;
}

// Boards come from `core list --json` (platforms[].releases[installed].boards).
// `board listall --json` has the same data but repeats each platform's full
// board list per board: ~15 MB / 11 s with esp32 installed, vs ~150 KB / 2 s.
void MainWindow::loadBoards()
{
    if (m_boardsProcess)
        return;
    m_boardsProcess = startCliJson({QStringLiteral("core"), QStringLiteral("list")},
        [this](const QJsonObject &json) { onBoardsLoaded(json); },
        [this] { m_boardsProcess = nullptr; });
}

void MainWindow::onBoardsLoaded(const QJsonObject &json)
{
    struct Board { QString name, fqbn; };
    QVector<Board> boards;
    QHash<QString, int> nameCount;

    for (const QJsonValue &pv : json.value(QStringLiteral("platforms")).toArray()) {
        const QJsonObject platform = pv.toObject();
        const QString version = platform.value(QStringLiteral("installed_version")).toString();
        const QJsonObject release = platform.value(QStringLiteral("releases")).toObject()
                                        .value(version).toObject();
        // For Tools > Board submenus: "arduino:avr" -> "Arduino AVR Boards".
        const QString platformName = release.value(QStringLiteral("name")).toString();
        if (!platformName.isEmpty())
            m_platformNames.insert(platform.value(QStringLiteral("id")).toString(), platformName);
        for (const QJsonValue &bv : release.value(QStringLiteral("boards")).toArray()) {
            const QJsonObject b = bv.toObject();
            const QString fqbn = b.value(QStringLiteral("fqbn")).toString();
            if (fqbn.isEmpty())
                continue;
            const QString name = b.value(QStringLiteral("name")).toString();
            boards.push_back({name, fqbn});
            ++nameCount[name];
        }
    }
    if (boards.isEmpty()) {
        appendLog(tr("No boards found. Install a core, e.g. `arduino-cli core install arduino:avr`."),
                  Theme::LogKind::Warning);
        return;
    }

    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    std::sort(boards.begin(), boards.end(), [&](const Board &a, const Board &b) {
        return collator.compare(a.name, b.name) < 0;
    });

    const QString keepFqbn = currentFqbn();
    const QString keepName = m_boardCombo->currentText();

    const QSignalBlocker blocker(m_boardCombo);
    m_boardCombo->clear();
    for (const Board &b : boards) {
        // Disambiguate boards with the same name in different platforms.
        const QString label = nameCount.value(b.name) > 1
            ? QStringLiteral("%1 (%2)").arg(b.name, b.fqbn.section(QLatin1Char(':'), 0, 1))
            : b.name;
        m_boardCombo->addItem(label, b.fqbn);
    }
    selectBoard(keepFqbn, keepName);
}

// Selects the board with this FQBN, adding it if it isn't in the list
// (e.g. the list hasn't loaded yet, or the core was removed).
void MainWindow::selectBoard(const QString &fqbn, const QString &name)
{
    if (fqbn.isEmpty())
        return;
    int index = m_boardCombo->findData(fqbn, FqbnRole);
    if (index < 0) {
        m_boardCombo->addItem(name.isEmpty() ? fqbn : name, fqbn);
        index = m_boardCombo->count() - 1;
    }
    m_boardCombo->setCurrentIndex(index);
    m_boardCombo->setToolTip(fqbn);
}

void MainWindow::scanPorts()
{
    if (m_portsProcess)
        return;
    statusBar()->showMessage(tr("Scanning ports..."));
    m_portsProcess = startCliJson({QStringLiteral("board"), QStringLiteral("list")},
        [this](const QJsonObject &json) { onPortsScanned(json); },
        [this] {
            m_portsProcess = nullptr;
            if (m_portMenu && m_portMenu->isVisible())
                populatePortMenu();   // Tools > Port is open: show the fresh list
            if (m_uploadAfterScan) {
                m_uploadAfterScan = false;
                if (currentPort().isEmpty()) {
                    appendLog(tr("Error: no port selected. Connect a board and choose it from the Port list."));
                    setBusy(false);
                } else {
                    startUpload();
                }
            } else if (!m_process || m_process->state() == QProcess::NotRunning) {
                statusBar()->clearMessage();
            }
        });
}

void MainWindow::onPortsScanned(const QJsonObject &json)
{
    struct Port { QString address, label, fqbn, boardName; };
    QVector<Port> ports;
    for (const QJsonValue &v : json.value(QStringLiteral("detected_ports")).toArray()) {
        const QJsonObject entry = v.toObject();
        const QJsonObject port = entry.value(QStringLiteral("port")).toObject();
        Port p;
        p.address = port.value(QStringLiteral("address")).toString();
        if (p.address.isEmpty())
            continue;
        p.label = port.value(QStringLiteral("label")).toString(p.address);
        const QJsonArray matches = entry.value(QStringLiteral("matching_boards")).toArray();
        if (!matches.isEmpty()) {
            const QJsonObject m = matches.first().toObject();
            p.fqbn = m.value(QStringLiteral("fqbn")).toString();
            p.boardName = m.value(QStringLiteral("name")).toString();
        }
        ports.push_back(p);
    }
    // Ports with a recognised board first.
    std::stable_sort(ports.begin(), ports.end(), [](const Port &a, const Port &b) {
        return !a.fqbn.isEmpty() && b.fqbn.isEmpty();
    });

    const QString previous = currentPort();
    const bool popupOpen = m_portCombo->isPopupVisible();

    int selectIndex = -1;
    int autoIndex = -1;   // the only port with a recognised board, if exactly one
    int recognised = 0;
    {
        const QSignalBlocker blocker(m_portCombo);
        m_portCombo->clear();
        for (const Port &p : ports) {
            const QString text = p.boardName.isEmpty() ? p.label
                                                       : QStringLiteral("%1 (%2)").arg(p.label, p.boardName);
            m_portCombo->addItem(text);
            const int i = m_portCombo->count() - 1;
            m_portCombo->setItemData(i, p.address, PortAddressRole);
            m_portCombo->setItemData(i, p.fqbn, FqbnRole);
            m_portCombo->setItemData(i, p.boardName, BoardNameRole);
            if (p.address == previous)
                selectIndex = i;
            if (!p.fqbn.isEmpty()) {
                ++recognised;
                autoIndex = i;
            }
        }
        m_portCombo->setCurrentIndex(selectIndex);
    }

    if (selectIndex < 0 && recognised == 1) {
        // Nothing (still) selected and exactly one board connected: pick it.
        m_portCombo->setCurrentIndex(autoIndex);
        onPortActivated(autoIndex);
    } else if (selectIndex < 0) {
        QSettings().setValue(QStringLiteral("port/address"), QString());
    }
    if (ports.isEmpty())
        m_portCombo->setToolTip(tr("No ports found. Connect a board and open this list again."));
    else
        m_portCombo->setToolTip(tr("Open to rescan"));

    if (popupOpen)
        m_portCombo->reshowPopup();   // resize the open popup to the new items
}

// Choosing a port whose board was recognised also selects that board.
void MainWindow::onPortActivated(int index)
{
    const QString fqbn = m_portCombo->itemData(index, FqbnRole).toString();
    if (!fqbn.isEmpty() && fqbn != currentFqbn())
        selectBoard(fqbn, m_portCombo->itemData(index, BoardNameRole).toString());
}

// ---------------------------------------------------------------------------

void MainWindow::updateMarginWidth(QsciScintilla *editor)
{
    // At least 3 digits wide, plus one digit of left padding.
    const int digits = qMax(3, int(QString::number(editor->lines()).size()));
    editor->setMarginWidth(0, QString(digits + 1, QLatin1Char('0')));
}

// Splitter handles: plain colored strips (palette, not stylesheet).
void MainWindow::themeSplitterHandles()
{
    const QColor border = Theme::colors(m_theme).border;
    for (QSplitter *s : {m_vSplitter, m_topSplitter}) {
        for (int i = 1; i < s->count(); ++i) {
            QSplitterHandle *h = s->handle(i);
            QPalette pal = h->palette();
            pal.setColor(QPalette::Window, border);
            h->setPalette(pal);
            h->setAutoFillBackground(true);
        }
    }
}

void MainWindow::applyTheme(Theme::Mode mode)
{
    const bool changed = mode != m_theme;
    m_theme = mode;
    if (changed) {
        qApp->setPalette(Theme::palette(mode));
        QSettings().setValue(QStringLiteral("view/theme"),
                             mode == Theme::Mode::Light ? QStringLiteral("light") : QStringLiteral("dark"));
    }
    (mode == Theme::Mode::Light ? m_lightThemeAction : m_darkThemeAction)->setChecked(true);

    // Stylesheet only on the widgets that need it, never on the editor.
    const QString qss = Theme::styleSheet(mode);
    // (The tab bar, not the tab widget: that would cascade into the editors.)
    for (QWidget *w : {static_cast<QWidget *>(m_toolbar), static_cast<QWidget *>(menuBar()), m_outputPane,
                       static_cast<QWidget *>(m_tabs->tabBar())})
        w->setStyleSheet(qss);
    if (m_serial)
        m_serial->setStyleSheet(qss);
    themeSplitterHandles();

    const Theme::Colors c = Theme::colors(mode);
    for (QsciScintilla *editor : editors()) {
        Theme::applyToEditor(editor, static_cast<QsciLexerCPP *>(editor->lexer()), mode,
                             Theme::monoFont(m_editorPointSize));
        updateMarginWidth(editor);
    }
    m_serialAction->setIcon(Theme::svgIcon(QStringLiteral(":/icons/serial.svg"), c.text));

    m_verifyAction->setIcon(Theme::svgIcon(QStringLiteral(":/icons/verify.svg"), c.onSecondary));
    m_uploadAction->setIcon(Theme::svgIcon(QStringLiteral(":/icons/upload.svg"), c.onPrimary));

    if (changed)
        recolorOutput();
}

// Re-applies line colors after a theme switch (each line remembers its kind).
void MainWindow::recolorOutput()
{
    QTextDocument *doc = m_output->document();
    QTextCursor cursor(doc);
    cursor.beginEditBlock();
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        QTextCharFormat format;
        format.setForeground(Theme::logColor(Theme::LogKind(qMax(0, block.userState())), m_theme));
        QTextCursor c(block);
        c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        c.setCharFormat(format);
    }
    cursor.endEditBlock();
}

// arduino-cli output: classify by content.
void MainWindow::appendLog(const QString &line)
{
    static const QRegularExpression errorRe(QStringLiteral("\\berror\\b"),
                                            QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression warningRe(QStringLiteral("\\bwarning\\b"),
                                              QRegularExpression::CaseInsensitiveOption);
    Theme::LogKind kind = Theme::LogKind::Normal;
    if (line.startsWith(QLatin1String("Sent: ")))
        kind = Theme::LogKind::Sent;
    else if (errorRe.match(line).hasMatch())
        kind = Theme::LogKind::Error;
    else if (warningRe.match(line).hasMatch())
        kind = Theme::LogKind::Warning;
    appendLog(line, kind);
}

void MainWindow::appendLog(const QString &line, Theme::LogKind kind)
{
    QTextCharFormat format;
    format.setForeground(Theme::logColor(kind, m_theme));

    QTextDocument *doc = m_output->document();
    QTextCursor cursor(doc);
    cursor.movePosition(QTextCursor::End);
    if (!doc->isEmpty())
        cursor.insertBlock();
    cursor.insertText(line, format);
    cursor.block().setUserState(int(kind));

    QScrollBar *bar = m_output->verticalScrollBar();
    bar->setValue(bar->maximum());
}

void MainWindow::setBusy(bool busy)
{
    // A focused button that gets disabled hands focus to the next widget (the
    // Board combo). Return it to the editor instead.
    if (busy) {
        const QWidget *focused = QApplication::focusWidget();
        if (focused && (focused->objectName() == QLatin1String("verifyButton")
                        || focused->objectName() == QLatin1String("uploadButton")))
            m_editor->setFocus();
    }
    m_verifyAction->setEnabled(!busy);
    m_uploadAction->setEnabled(!busy);
    if (busy)
        statusBar()->showMessage(m_taskName == tr("Upload") ? tr("Uploading...") : tr("Compiling..."));
    else
        statusBar()->clearMessage();
}
