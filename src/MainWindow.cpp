#include "MainWindow.h"
#include "FindBar.h"
#include "ArduinoLexer.h"
#include "BoardComboBox.h"
#include "PortComboBox.h"
#include "Portable.h"
#include "UsbDrivers.h"
#include "ManualControl.h"
#include "SerialCommandGuide.h"
#include "SerialMonitor.h"
#include "Spinner.h"

#include <Qsci/qsciscintilla.h>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QMenu>
#include <QMenuBar>
#include <QProgressBar>
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
#include <QSerialPortInfo>
#include <QHBoxLayout>
#include <QSplitter>
#include <QStackedWidget>
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
constexpr int PortLabelRole = Qt::UserRole + 3;   // port combo: label without the status

// Port address as arduino-cli gives it: "COM3" on Windows, "/dev/ttyACM0" on Linux.
bool portIn(const QList<QSerialPortInfo> &ports, const QString &address)
{
    if (address.isEmpty())
        return false;
    for (const QSerialPortInfo &info : ports) {
        if (info.portName().compare(address, Qt::CaseInsensitive) == 0 || info.systemLocation() == address)
            return true;
    }
    return false;
}

constexpr qreal kOutputPointSize = 10;
// The Manual Control tab is hidden for now (code kept); set to true to bring it back.
constexpr bool kShowManualControl = false;

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
        if (m_findBar)
            m_findBar->setEditor(m_editor);
    });
    setupMenus();       // after the toolbar: menus reuse its actions
    applyShortcuts();   // defaults + Advanced Settings overrides
    m_topSplitter = new QSplitter(Qt::Horizontal);
    m_topSplitter->setChildrenCollapsible(false);
    // The editor tabs sit in a plain container so the Find bar can be added under them later.
    auto *editorArea = new QWidget;
    m_editorAreaLayout = new QVBoxLayout(editorArea);
    m_editorAreaLayout->setContentsMargins(0, 0, 0, 0);
    m_editorAreaLayout->setSpacing(0);
    m_editorAreaLayout->addWidget(m_tabs);
    m_topSplitter->addWidget(editorArea);

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
    // Ports: one scan a few seconds after startup, so the list shows board names
    // right away. `board list` can hit the network; this delayed scan is an
    // approved exception to "no network at startup". Later scans are on demand.
    QTimer::singleShot(0, this, &MainWindow::loadBoards);
    QTimer::singleShot(3000, this, &MainWindow::scanPorts);
    if (Portable::isPortable()) {
        const int pathLength = QDir::toNativeSeparators(Portable::appDir()).size();
        if (pathLength > Portable::kMaxFolderPath)
            appendLog(tr("Warning: this portable folder's path is long (%1 characters); compiling may fail with "
                         "\"No such file or directory\". Move the folder somewhere shorter, like C:\\REARMduino.")
                          .arg(pathLength));
        else if (pathLength > Portable::kMaxFolderPathEsp8266 && Portable::bundlesEsp8266())
            appendLog(tr("Warning: this portable folder's path is %1 characters long. ESP8266 boards only compile "
                         "from a path of about %2 characters or less (other boards are fine). For ESP8266, move "
                         "the folder somewhere shorter, like C:\\REARMduino.")
                          .arg(pathLength).arg(Portable::kMaxFolderPathEsp8266));
    }
    // A board without a driver (e.g. a CH340 clone on a fresh PC): offer the bundled one.
    if (UsbDrivers::supported())
        QTimer::singleShot(3000, this, [this] { checkUsbDrivers(false); });
#ifdef MYIDE_ACTIVATION
    // Tells the server this copy was used and learns if this PC was revoked. In the background,
    // well after the window is up; offline it does nothing.
    QTimer::singleShot(5000, this, &MainWindow::checkActivation);
#endif
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
    m_boardCombo->setObjectName(QStringLiteral("boardCombo"));
    m_boardSpinner = new Spinner(m_boardCombo);
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
        updateResetLabel();
    });
    toolbar->addWidget(m_boardCombo);

    // Port: remembered port only; rescanned each time the dropdown opens.
    toolbar->addWidget(new QLabel(tr("Port")));
    m_portCombo = new PortComboBox;
    m_portCombo->setObjectName(QStringLiteral("portCombo"));
    m_portSpinner = new Spinner(m_portCombo);
    m_portCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_portCombo->setMinimumContentsLength(20);   // room for "COM3 (DISCONNECTED)"
    m_portCombo->setToolTip(tr("Open to scan for connected boards"));
    const QString savedPort = settings.value(QStringLiteral("port/address")).toString();
    if (!savedPort.isEmpty()) {
        m_portCombo->addItem(savedPort);
        m_portCombo->setItemData(0, savedPort, PortAddressRole);
        m_portCombo->setItemData(0, savedPort, PortLabelRole);
    }
    // Plugged/disconnected status: a cheap local check (QtSerialPort, no arduino-cli),
    // started after the window is up.
    m_portWatch = new QTimer(this);
    m_portWatch->setInterval(1000);
    connect(m_portWatch, &QTimer::timeout, this, &MainWindow::updatePortStatus);
    QTimer::singleShot(500, this, [this] { updatePortStatus(); m_portWatch->start(); });
    connect(m_portCombo, &PortComboBox::popupAboutToShow, this, &MainWindow::scanPorts);
    connect(m_portCombo, QOverload<int>::of(&QComboBox::activated), this, &MainWindow::onPortActivated);
    connect(m_portCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index < 0)
            return;
        m_portWasPresent = -1;   // a different port: don't report its state as a change
        m_serialHiddenByUnplug = false;
        QSettings().setValue(QStringLiteral("port/address"), currentPort());
        if (m_serial)
            m_serial->setPortName(currentPort());   // follows the toolbar; reopens if open
        updateResetLabel();
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

    // Tabs: Output | Manual Control (hidden) | Serial Command Guide (both created on first use).
    auto *header = new QWidget;
    header->setObjectName(QStringLiteral("outputHeaderRow"));
    header->setAttribute(Qt::WA_StyledBackground);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(4, 0, 4, 0);
    m_bottomTabs = new QTabBar;
    m_bottomTabs->setObjectName(QStringLiteral("outputTabs"));
    m_bottomTabs->setDrawBase(false);
    m_bottomTabs->setExpanding(false);
    m_bottomTabs->setFocusPolicy(Qt::NoFocus);
    // Bold via the widget font, not QSS: tab sizes ignore a QSS font-weight
    // (the labels would be clipped).
    QFont tabFont = m_bottomTabs->font();
    tabFont.setWeight(QFont::DemiBold);
    m_bottomTabs->setFont(tabFont);
    m_bottomTabs->addTab(tr("Output"));
    m_bottomTabs->addTab(tr("Manual Control"));
    m_bottomTabs->setTabToolTip(1, tr("Buttons that send to the serial port (saved in the sketch folder)"));
    m_bottomTabs->setTabVisible(1, kShowManualControl);
    m_bottomTabs->addTab(tr("Serial Command Guide"));
    m_bottomTabs->setTabToolTip(2, tr("The sketch's serial commands; click one to send it (saved in the sketch folder)"));
    headerLayout->addWidget(m_bottomTabs);
    headerLayout->addStretch(1);
    layout->addWidget(header);

    m_bottomStack = new QStackedWidget;
    layout->addWidget(m_bottomStack, 1);
    connect(m_bottomTabs, &QTabBar::currentChanged, this, &MainWindow::showBottomTab);

    m_output = new QPlainTextEdit;
    m_output->setObjectName(QStringLiteral("output"));
    m_output->setReadOnly(true);
    m_output->setMaximumBlockCount(10000);
    m_output->setFont(Theme::monoFont(kOutputPointSize));
    m_output->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_output->setFrameShape(QFrame::NoFrame);
    m_bottomStack->addWidget(m_output);
    return pane;
}

void MainWindow::showBottomTab(int index)
{
    if (index == 2) {
        if (!m_guide) {
            // Lazily, like Manual Control; sends the same way (see manualSend).
            m_guide = new SerialCommandGuide([this](const QByteArray &data, const QString &label) {
                return manualSend(data, label);
            });
            m_bottomStack->addWidget(m_guide);
            m_guide->setSketchDir(m_sketchDir);
        } else {
            m_guide->reloadIfChanged();
        }
        m_bottomStack->setCurrentWidget(m_guide);
        m_guide->setFocus();   // hotkeys work right away (not while the editor has focus)
        return;
    }
    if (index != 1) {
        m_bottomStack->setCurrentWidget(m_output);
        return;
    }
    if (!m_manual) {
        // Created lazily (startup rule); inherits the pane's stylesheet.
        m_manual = new ManualControl([this](const QByteArray &data, const QString &label) {
            return manualSend(data, label);
        });
        m_bottomStack->addWidget(m_manual);
        m_manual->setSketchDir(m_sketchDir);
    } else {
        m_manual->reloadIfChanged();
    }
    m_bottomStack->setCurrentWidget(m_manual);
    m_manual->setFocus();   // hotkeys active right away
}

// Manual Control and Serial Command Guide buttons: write to the toolbar's port through the Serial Monitor,
// opening it if needed (so the board's replies are visible).
QString MainWindow::manualSend(const QByteArray &data, const QString &label)
{
    QString error;
    if (m_process && m_process->state() != QProcess::NotRunning && m_taskName == tr("Upload"))
        error = tr("upload in progress");
    else if (currentPort().isEmpty())
        error = tr("no port selected (choose one in the toolbar)");
    else if (!isPortPresent(currentPort()))
        error = tr("%1 is disconnected (plug the board in)").arg(currentPort());
    if (error.isEmpty() && (!m_serial || !m_serial->isOpen())) {
        // Opening the port resets most boards (DTR), and bytes that arrive while
        // the bootloader runs can keep the sketch from starting. So this press
        // only opens the port; the board's boot output shows in the monitor.
        m_serialKeepFocus = true;   // don't move focus to the monitor's input (ends key holds)
        if (!m_serialAction->isChecked())
            m_serialAction->setChecked(true);
        else if (m_serial)
            m_serial->openPort();
        m_serialKeepFocus = false;
        if (m_serial && m_serial->isOpen()) {
            const QString note = tr("opened %1; the board restarts when the port opens. Press again once it's ready")
                                     .arg(currentPort());
            appendLog(tr("Serial: %1.").arg(note), Theme::LogKind::Command);
            return note;
        }
        error = tr("cannot open %1 (see Output)").arg(currentPort());
    }
    if (error.isEmpty())
        error = m_serial->writeRaw(data + '\n');   // each command is one line
    if (!error.isEmpty()) {
        appendLog(tr("Not sent (%1): %2").arg(label, error), Theme::LogKind::Error);
        return error;
    }
    // With the Serial Command Guide showing, Output is hidden: put the line in the
    // Serial Monitor instead, between the board's replies.
    if (m_bottomTabs->currentIndex() == 2)
        m_serial->showSent(QString::fromUtf8(data));
    else
        appendLog(QStringLiteral("Sent: %1  (%2)").arg(QString::fromUtf8(data), label), Theme::LogKind::Sent);
    return {};
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
    themeEditorScrollBars(editor, Theme::scrollBarStyleSheet(m_theme));
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

// Only the scrollbars get the stylesheet (the whole editor would be slow), plus
// a plain corner square where the two scrollbars meet. QScintilla editors keep
// the palette they were created with, so this is also what makes the scrollbars
// follow a runtime theme switch.
void MainWindow::themeEditorScrollBars(QsciScintilla *editor, const QString &qss)
{
    editor->verticalScrollBar()->setStyleSheet(qss);
    editor->horizontalScrollBar()->setStyleSheet(qss);
    QWidget *corner = editor->cornerWidget();
    if (!corner) {
        corner = new QWidget;
        corner->setAutoFillBackground(true);
        editor->setCornerWidget(corner);
    }
    QPalette pal = corner->palette();
    pal.setColor(QPalette::Window, Theme::colors(m_theme).editorBg);
    corner->setPalette(pal);
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

    // No <folder>.ino yet: new sketch -> template. But if the folder already has
    // other .ino files, start it empty (they may define setup()/loop(); a
    // template would clash with them).
    const bool mainExists = QFileInfo::exists(m_sketchFile);
    if (!mainExists && files.size() > 1)
        appendLog(tr("Note: %1 has no %2; created it empty (arduino-cli needs the main file to match the folder name).")
                      .arg(QDir::toNativeSeparators(m_sketchDir), QFileInfo(m_sketchFile).fileName()),
                  Theme::LogKind::Warning);

    for (const QString &file : files) {
        QsciScintilla *editor = createEditor(file);
        QFile f(file);
        if (f.open(QIODevice::ReadOnly))
            editor->setText(QString::fromUtf8(f.readAll()));
        else if (file == m_sketchFile && files.size() == 1)
            editor->setText(QString::fromLatin1(kDefaultSketch));
        editor->setModified(false);
        m_tabs->addTab(editor, QString());
        updateTabTitle(editor);
        updateMarginWidth(editor);
    }
    m_tabs->setCurrentIndex(0);
    m_editor = editors().value(0);
    if (m_manual)
        m_manual->setSketchDir(m_sketchDir);   // buttons belong to the sketch
    if (m_guide)
        m_guide->setSketchDir(m_sketchDir);
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
        m_cliPath = Portable::bundledCli();   // portable copy ships its own
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
    m_bottomTabs->setCurrentIndex(0);   // show the build log
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
    m_bottomTabs->setCurrentIndex(0);   // show the build log
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
    if (!isPortPresent(currentPort())) {
        appendLog(tr("Error: %1 is disconnected. Plug the board in, or choose another port from the Port list.")
                      .arg(currentPort()));
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
    m_taskIsUpload = args.contains(QStringLiteral("--upload"));
    setBusy(true);
    beginProgress(m_taskIsUpload);
    m_process->setProcessEnvironment(Portable::cliEnvironment());
    m_process->start(cli, args);
}

void MainWindow::onProcessOutput()
{
    // arduino-cli 1.5.1 (Pi) colors some lines with ANSI escapes; 1.4.1 doesn't.
    static const QRegularExpression ansi(QStringLiteral("\x1b\\[[0-9;]*[A-Za-z]"));
    m_partialLine += m_decoder->toUnicode(m_process->readAll());
    m_partialLine.remove(ansi);   // on the joined buffer: an escape may span two reads
    updateProgress(m_partialLine);
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
    // A finished Verify is a full compile: remember how long it took for the next progress bar.
    if (status == QProcess::NormalExit && exitCode == 0 && !m_taskIsUpload)
        m_compileMs.insert(m_progressKey, m_taskClock.elapsed());
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
    if (show && !isPortPresent(currentPort())) {
        // Nothing to talk to: keep the monitor hidden.
        const QSignalBlocker blocker(m_serialAction);
        m_serialAction->setChecked(false);
        const QString why = currentPort().isEmpty()
            ? tr("Serial Monitor: no board connected. Plug one in and choose its port.")
            : tr("Serial Monitor: %1 is disconnected. Plug the board in first.").arg(currentPort());
        appendLog(why, Theme::LogKind::Warning);
        statusBar()->showMessage(why, 5000);
        return;
    }
    if (show && !m_serial) {
        // Created lazily on first open (CLAUDE.md startup rule).
        m_serial = new SerialMonitor;
        connect(m_serial, &SerialMonitor::sent, this, [this](const QString &text) {
            appendLog(QStringLiteral("Sent: ") + text, Theme::LogKind::Sent);
        });
        connect(m_serial, &SerialMonitor::message, this, [this](const QString &text, bool isError) {
            appendLog(text, isError ? Theme::LogKind::Error : Theme::LogKind::Command);
        });
        connect(m_serial, &SerialMonitor::portStuck, this, &MainWindow::offerUsbRestart);
        updateResetLabel();
        m_serial->setStyleSheet(Theme::styleSheet(m_theme));
        m_serial->setTheme(m_theme);
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
        if (!m_serialKeepFocus)
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
    proc->setProcessEnvironment(Portable::cliEnvironment());
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
    m_boardSpinner->start();
    m_boardsProcess = startCliJson({QStringLiteral("core"), QStringLiteral("list")},
        [this](const QJsonObject &json) { onBoardsLoaded(json); },
        [this] { m_boardsProcess = nullptr; m_boardSpinner->stop(); });
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
    m_portSpinner->start();
    m_portCombo->setLoading(true);
    m_portsProcess = startCliJson({QStringLiteral("board"), QStringLiteral("list")},
        [this](const QJsonObject &json) { onPortsScanned(json); },
        [this] {
            m_portsProcess = nullptr;
            m_portSpinner->stop();
            m_portCombo->setLoading(false);
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
    if (UsbDrivers::supported())
        checkUsbDrivers(false);   // "my board isn't listed": maybe it has no driver
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
    bool previousMissing = false;   // the selected port is not in the scan
    {
        const QSignalBlocker blocker(m_portCombo);
        m_portCombo->clear();
        for (const Port &p : ports) {
            m_portCombo->addItem(p.label);   // status and board name added by updatePortStatus()
            const int i = m_portCombo->count() - 1;
            m_portCombo->setItemData(i, p.address, PortAddressRole);
            m_portCombo->setItemData(i, p.label, PortLabelRole);
            m_portCombo->setItemData(i, p.fqbn, FqbnRole);
            m_portCombo->setItemData(i, p.boardName, BoardNameRole);
            if (p.address == previous)
                selectIndex = i;
            if (!p.fqbn.isEmpty()) {
                ++recognised;
                autoIndex = i;
            }
        }
        // The selected port is gone: keep it, shown "(DISCONNECTED)", rather than
        // silently dropping the selection (and the saved port).
        if (selectIndex < 0 && !previous.isEmpty()) {
            m_portCombo->addItem(previous);
            selectIndex = m_portCombo->count() - 1;
            m_portCombo->setItemData(selectIndex, previous, PortAddressRole);
            m_portCombo->setItemData(selectIndex, previous, PortLabelRole);
            previousMissing = true;
        }
        m_portCombo->setCurrentIndex(selectIndex);
    }
    updatePortStatus();

    // A port appeared while the selected one was disconnected (or none was
    // selected): select it. Only select; the Serial Monitor stays closed, since
    // another program may want that port.
    int newIndex = -1;
    if (!m_newPort.isEmpty()) {
        for (int i = 0; i < m_portCombo->count(); ++i) {
            const QString address = m_portCombo->itemData(i, PortAddressRole).toString();
            if (address == m_newPort || address == QStringLiteral("/dev/") + m_newPort)
                newIndex = i;
        }
        if (newIndex >= 0)
            m_newPort.clear();
    }
    if (newIndex >= 0 && (selectIndex < 0 || !isPortPresent(previous))) {
        const QString label = m_portCombo->itemData(newIndex, PortLabelRole).toString();
        m_portCombo->setCurrentIndex(newIndex);
        onPortActivated(newIndex);
        appendLog(previous.isEmpty()
                      ? tr("Selected the newly connected port %1.").arg(label)
                      : tr("%1 is disconnected: selected the newly connected port %2. "
                           "The Serial Monitor was not opened.").arg(previous, label),
                  Theme::LogKind::Warning);
    } else if ((selectIndex < 0 || previousMissing) && recognised == 1) {
        // Nothing selected, or the selected port is disconnected, and exactly one
        // board connected: pick it (e.g. the board came back on another port).
        const QString label = m_portCombo->itemData(autoIndex, PortLabelRole).toString();
        m_portCombo->setCurrentIndex(autoIndex);
        onPortActivated(autoIndex);
        if (previousMissing)
            appendLog(tr("%1 is disconnected: selected %2, where a board is connected. "
                         "The Serial Monitor was not opened.").arg(previous, label),
                      Theme::LogKind::Warning);
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

// Is the port there right now? QSerialPortInfo is a quick local lookup (Windows:
// device list, Linux: /sys), unlike "arduino-cli board list".
bool MainWindow::isPortPresent(const QString &address) const
{
    return portIn(QSerialPortInfo::availablePorts(), address);
}

// Arduino boards (an arduino:... board selected, or Arduino's USB IDs on the port)
// get "Reset Arduino"; others (ESP32, ...) "Reset Board".
void MainWindow::updateResetLabel()
{
    if (!m_serial)
        return;
    bool arduino = currentFqbn().startsWith(QLatin1String("arduino:"));
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
        if (info.hasVendorIdentifier() && portIn({info}, currentPort())
            && (info.vendorIdentifier() == 0x2341 || info.vendorIdentifier() == 0x2A03))
            arduino = true;
    }
    m_serial->setBoardIsArduino(arduino);
}

// Port items read "COM3 (PLUGGED) - Arduino Mega or Mega 2560" or "COM3 (DISCONNECTED)".
void MainWindow::updatePortStatus()
{
    const QList<QSerialPortInfo> available = QSerialPortInfo::availablePorts();
    auto present = [&available](const QString &address) { return portIn(available, address); };

    // Newly connected ports: if the selected port is disconnected (or none is
    // selected), rescan so onPortsScanned() can select the new one. Not during an
    // upload or a USB restart, where boards re-enumerate on purpose.
    QStringList names;
    for (const QSerialPortInfo &info : available)
        names << info.portName();
    if (m_seenPortsValid) {
        const QString selected = currentPort();
        const bool uploading = m_process && m_process->state() != QProcess::NotRunning;
        if (!uploading && !m_usbRestartBusy && (selected.isEmpty() || !present(selected))) {
            for (const QString &name : names) {
                if (!m_seenPorts.contains(name)) {
                    m_newPort = name;
                    m_newPortTries = 3;
                }
            }
        }
    }
    m_seenPorts = names;
    m_seenPortsValid = true;
    if (!m_newPort.isEmpty() && !m_portsProcess) {
        if (m_newPortTries-- > 0)
            scanPorts();
        else
            m_newPort.clear();   // arduino-cli does not list it
    }

    for (int i = 0; i < m_portCombo->count(); ++i) {
        const QString address = m_portCombo->itemData(i, PortAddressRole).toString();
        QString label = m_portCombo->itemData(i, PortLabelRole).toString();
        if (label.isEmpty())
            label = address;
        const bool here = present(address);
        QString text = label + (here ? PortComboBox::pluggedText() : PortComboBox::disconnectedText());
        const QString board = m_portCombo->itemData(i, BoardNameRole).toString();
        if (!board.isEmpty())
            text += QStringLiteral(" - ") + board;
        if (m_portCombo->itemText(i) != text)
            m_portCombo->setItemText(i, text);

        if (i == m_portCombo->currentIndex()) {
            const int state = here ? 1 : 0;
            if (m_portWasPresent >= 0 && state != m_portWasPresent) {
                statusBar()->showMessage(here ? tr("%1 plugged in").arg(label)
                                              : tr("%1 disconnected").arg(label), 5000);
                // No board, no monitor; it comes back when the board is plugged in again.
                // (Not during an upload: native-USB boards drop off the bus while flashing.)
                const bool uploading = m_process && m_process->state() != QProcess::NotRunning;
                if (!here && !uploading && m_serialAction->isChecked()) {
                    m_serialHiddenByUnplug = true;
                    m_serialAction->setChecked(false);
                    appendLog(tr("Serial Monitor hidden: %1 was unplugged. It reopens when the board is back.").arg(label),
                              Theme::LogKind::Warning);
                } else if (here && m_serialHiddenByUnplug) {
                    m_serialHiddenByUnplug = false;
                    m_serialAction->setChecked(true);
                }
            }
            m_portWasPresent = state;
            m_portCombo->setToolTip(here ? tr("%1 is plugged in. Open to rescan.").arg(label)
                                         : tr("%1 is disconnected. Plug the board in, or open to rescan.").arg(label));
        }
    }
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
    const QString scrollQss = Theme::scrollBarStyleSheet(mode);
    for (QsciScintilla *editor : editors()) {
        Theme::applyToEditor(editor, static_cast<QsciLexerCPP *>(editor->lexer()), mode,
                             Theme::monoFont(m_editorPointSize));
        themeEditorScrollBars(editor, scrollQss);
        updateMarginWidth(editor);
    }
    if (m_serial)
        m_serial->setTheme(mode);
    if (m_findBar)
        m_findBar->setTheme(mode);
    m_serialAction->setIcon(Theme::svgIcon(QStringLiteral(":/icons/serial.svg"), c.text));
    m_portCombo->setStatusColors(c.success, c.warning);   // (PLUGGED) green, (DISCONNECTED) amber
    m_boardSpinner->setColors(c.primary, c.editorBg);
    m_portSpinner->setColors(c.primary, c.editorBg);
    m_portCombo->setSpinnerColor(c.primary);

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
    if (busy) {
        statusBar()->showMessage(tr("Compiling..."));
    } else {
        statusBar()->clearMessage();
        if (m_progress) {
            m_progressTick->stop();
            m_progress->hide();
        }
    }
}

void MainWindow::beginProgress(bool isUpload)
{
    if (!m_progress) {
        m_progress = new QProgressBar;
        m_progress->setTextVisible(false);
        m_progress->setFixedSize(180, 10);
        statusBar()->addPermanentWidget(m_progress);
        m_progressTick = new QTimer(this);
        m_progressTick->setInterval(100);
        connect(m_progressTick, &QTimer::timeout, this, &MainWindow::tickProgress);
    }
    m_taskIsUpload = isUpload;
    m_uploadPhase = false;
    m_progressKey = m_sketchDir + QLatin1Char('|') + currentFqbn();
    m_compileEstimate = m_compileMs.value(m_progressKey, 0);
    m_taskClock.start();
    // Range 0-0 is Qt's "busy" stripe: used until there is something to measure against.
    m_progress->setRange(0, m_compileEstimate > 0 ? 1000 : 0);
    m_progress->setValue(0);
    m_progress->show();
    if (m_compileEstimate > 0)
        m_progressTick->start();
}

// Compiling: fills toward 95% over the time the last compile took, then waits there.
void MainWindow::tickProgress()
{
    if (m_uploadPhase || m_compileEstimate <= 0)
        return;
    m_progress->setValue(int(qMin<qint64>(950, m_taskClock.elapsed() * 1000 / m_compileEstimate)));
}

// Called with the output not yet split into lines (uploaders redraw their progress in place).
void MainWindow::updateProgress(const QString &output)
{
    if (!m_progress || !m_taskIsUpload)
        return;
    // esptool (ESP32/ESP8266): "Writing at 0x00010000... (42 %)".
    static const QRegularExpression percentRe(QStringLiteral("\\((\\d{1,3}) ?%\\)"));
    int percent = -1;
    auto it = percentRe.globalMatch(output);
    while (it.hasNext())
        percent = it.next().captured(1).toInt();
    // avrdude (verbose upload only): "Writing | #####", 50 marks when done.
    const int bar = output.lastIndexOf(QLatin1String("Writing | "));
    if (percent == -1 && bar != -1)
        percent = qMin(100, output.mid(bar).count(QLatin1Char('#')) * 2);

    // The size report ends the compile; ESP8266 prints none, so its first percentage does.
    if (!m_uploadPhase && (percent != -1 || output.contains(QLatin1String("Sketch uses ")))) {
        m_compileMs.insert(m_progressKey, m_taskClock.elapsed());
        m_uploadPhase = true;
        m_progressTick->stop();
        m_progress->setRange(0, 0);
        statusBar()->showMessage(tr("Uploading..."));
    }
    if (m_uploadPhase && percent != -1) {
        m_progress->setRange(0, 100);
        m_progress->setValue(qBound(0, percent, 100));
    }
}
