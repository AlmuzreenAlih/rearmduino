#include "MainWindow.h"
#include "PortComboBox.h"

#include <Qsci/qsciscintilla.h>
#include <Qsci/qscilexercpp.h>

#include <QAction>
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
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextCodec>
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

// Monospace font for the editor and output pane. Windows' "system fixed font"
// is Courier New, so use Consolas there; elsewhere use the desktop's monospace font.
QFont monoFont()
{
#ifdef Q_OS_WIN
    QFont font(QStringLiteral("Consolas"), 10);
    font.setStyleHint(QFont::Monospace);
    return font;
#else
    return QFontDatabase::systemFont(QFontDatabase::FixedFont);
#endif
}

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
    setupToolbar();

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
    m_output->setFont(monoFont());
    m_output->setLineWrapMode(QPlainTextEdit::NoWrap);

    auto *vertical = new QSplitter(Qt::Vertical);
    vertical->addWidget(m_topSplitter);
    vertical->addWidget(m_output);
    vertical->setStretchFactor(0, 3);
    vertical->setStretchFactor(1, 1);
    setCentralWidget(vertical);

    resize(1000, 700);
    resolveSketch(sketchPath);

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
    QToolBar *toolbar = addToolBar(tr("Main"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    m_verifyAction = toolbar->addAction(tr("Verify"), this, &MainWindow::verify);
    m_verifyAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    m_verifyAction->setToolTip(tr("Compile the sketch (Ctrl+R)"));

    m_uploadAction = toolbar->addAction(tr("Upload"), this, &MainWindow::upload);
    m_uploadAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_U));
    m_uploadAction->setToolTip(tr("Compile and upload to the selected port (Ctrl+U)"));

    toolbar->addSeparator();
    QSettings settings;

    // Board: starts with just the remembered board; the full list loads later.
    toolbar->addWidget(new QLabel(tr(" Board: ")));
    m_boardCombo = new QComboBox;
    m_boardCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_boardCombo->setMinimumContentsLength(24);
    m_boardCombo->setMaxVisibleItems(25);
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
    toolbar->addWidget(new QLabel(tr("  Port: ")));
    m_portCombo = new PortComboBox;
    m_portCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_portCombo->setMinimumContentsLength(18);
    m_portCombo->setToolTip(tr("Open to scan for connected boards"));
    const QString savedPort = settings.value(QStringLiteral("port/address")).toString();
    if (!savedPort.isEmpty()) {
        m_portCombo->addItem(savedPort);
        m_portCombo->setItemData(0, savedPort, PortAddressRole);
    }
    connect(m_portCombo, &PortComboBox::popupAboutToShow, this, &MainWindow::scanPorts);
    connect(m_portCombo, QOverload<int>::of(&QComboBox::activated), this, &MainWindow::onPortActivated);
    connect(m_portCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0)
            QSettings().setValue(QStringLiteral("port/address"), currentPort());
    });
    toolbar->addWidget(m_portCombo);
}

void MainWindow::setupEditor()
{
    const QFont font = monoFont();

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

    // Scintilla reserves a fixed 2000 px scroll width by default, so the
    // horizontal scrollbar always shows. Start small and grow with the longest
    // line instead. (Raw messages: works on every QScintilla version.)
    m_editor->SendScintilla(QsciScintilla::SCI_SETSCROLLWIDTH, 1);
    m_editor->SendScintilla(QsciScintilla::SCI_SETSCROLLWIDTHTRACKING, 1);
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

QString MainWindow::cliPath()
{
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
    startTask(tr("Verify"), {QStringLiteral("compile"), QStringLiteral("--fqbn"), currentFqbn(), m_sketchDir});
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
        appendLog(tr("No port selected, scanning for boards..."));
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
    // TODO(serial monitor): close the port here if the monitor has it open and
    // reopen it in onProcessFinished(), otherwise the upload fails with "port busy".
    startTask(tr("Upload"), {QStringLiteral("compile"), QStringLiteral("--upload"),
                             QStringLiteral("--fqbn"), currentFqbn(),
                             QStringLiteral("--port"), currentPort(), m_sketchDir});
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
        appendLog(tr("%1 finished: OK.").arg(m_taskName));
    else
        appendLog(tr("%1 failed (exit code %2).").arg(m_taskName).arg(exitCode));
    setBusy(false);
}

void MainWindow::onProcessError(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart) {
        appendLog(tr("Error: could not start arduino-cli: %1").arg(m_process->errorString()));
        setBusy(false);
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
            appendLog(tr("arduino-cli %1 failed: %2").arg(cmd, msg));
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
        appendLog(tr("No boards found. Install a core, e.g. `arduino-cli core install arduino:avr`."));
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

void MainWindow::appendLog(const QString &line)
{
    m_output->appendPlainText(line);
    QScrollBar *bar = m_output->verticalScrollBar();
    bar->setValue(bar->maximum());
}

void MainWindow::setBusy(bool busy)
{
    m_verifyAction->setEnabled(!busy);
    m_uploadAction->setEnabled(!busy);
    if (busy)
        statusBar()->showMessage(m_taskName == tr("Upload") ? tr("Uploading...") : tr("Compiling..."));
    else
        statusBar()->clearMessage();
}
