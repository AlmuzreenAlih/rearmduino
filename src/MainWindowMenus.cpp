// MainWindow: menu bar, sketch file handling, edit commands, keyboard shortcuts
// and preferences. Everything that needs a scan or a subprocess (Examples,
// Board, Port, Open Recent) is filled only when its menu opens.

#include "MainWindow.h"
#include "ArduinoLexer.h"
#include "ManualControl.h"
#include "PortComboBox.h"
#include "SettingsDialogs.h"

#include <Qsci/qscicommand.h>
#include <Qsci/qscicommandset.h>
#include <Qsci/qsciglobal.h>
#include <Qsci/qsciscintilla.h>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QCollator>
#include <QComboBox>
#include <QDate>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QLocale>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QUrl>

#include <algorithm>

namespace {
enum EditOp { Undo, Redo, Cut, Copy, Paste, SelectAll };
constexpr int kMaxRecent = 10;
constexpr int FqbnRole = Qt::UserRole;
constexpr int PortAddressRole = Qt::UserRole + 1;
const char kReferenceUrl[] = "https://www.arduino.cc/reference/en/";

bool copyDir(const QString &from, const QString &to)
{
    const QDir src(from);
    if (!src.exists() || !QDir().mkpath(to))
        return false;
    for (const QFileInfo &fi : src.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
        const QString dst = QDir(to).filePath(fi.fileName());
        if (fi.isDir()) {
            if (!copyDir(fi.filePath(), dst))
                return false;
        } else {
            QFile::remove(dst);
            if (!QFile::copy(fi.filePath(), dst))
                return false;
        }
    }
    return true;
}

// arduino-cli's rule: letters, digits, _ - . ; must not start with - or . ; max 63.
bool validSketchName(const QString &name)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_.-]{0,62}$"));
    return re.match(name).hasMatch();
}

// QMenu::clear() deletes actions but not submenus/action groups parented to the menu.
void clearMenu(QMenu *menu)
{
    menu->clear();
    for (QObject *child : menu->children()) {
        if (qobject_cast<QMenu *>(child) || qobject_cast<QActionGroup *>(child))
            child->deleteLater();
    }
}

QAction *addDisabled(QMenu *menu, const QString &text)
{
    QAction *a = menu->addAction(text);
    a->setEnabled(false);
    return a;
}
}

// ---------------------------------------------------------------------------
// Menu bar
// ---------------------------------------------------------------------------

void MainWindow::setupMenus()
{
    QMenuBar *bar = menuBar();
    const auto key = [](int k) { return QKeySequence(k); };

    // File
    QMenu *file = bar->addMenu(tr("&File"));
    registerAction(file->addAction(tr("&New Sketch"), this, &MainWindow::newSketch), "file.new", key(Qt::CTRL | Qt::Key_N));
    registerAction(file->addAction(tr("&Open..."), this, &MainWindow::openSketch), "file.open", key(Qt::CTRL | Qt::Key_O));
    m_recentMenu = file->addMenu(tr("Open &Recent"));
    connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::populateRecentMenu);
    m_examplesMenu = file->addMenu(tr("&Examples"));
    connect(m_examplesMenu, &QMenu::aboutToShow, this, &MainWindow::populateExamplesMenu);
    file->addSeparator();
    registerAction(file->addAction(tr("&Save"), this, [this] { save(); }), "file.save", key(Qt::CTRL | Qt::Key_S));
    registerAction(file->addAction(tr("Save &As..."), this, [this] { saveAs(); }), "file.saveAs",
                   key(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    file->addSeparator();
    registerAction(file->addAction(tr("&Preferences..."), this, &MainWindow::showPreferences), "file.preferences",
                   key(Qt::CTRL | Qt::Key_Comma));
    registerAction(file->addAction(tr("Advanced Se&ttings..."), this, [this] { showAdvancedSettings(0); }),
                   "file.advancedSettings", QKeySequence());
    file->addSeparator();
    registerAction(file->addAction(tr("&Quit"), this, &QWidget::close), "file.quit", key(Qt::CTRL | Qt::Key_Q));

    // Edit
    QMenu *edit = bar->addMenu(tr("&Edit"));
    registerAction(edit->addAction(tr("&Undo"), this, [this] { editCommand(Undo); }), "edit.undo", key(Qt::CTRL | Qt::Key_Z));
    registerAction(edit->addAction(tr("&Redo"), this, [this] { editCommand(Redo); }), "edit.redo", QKeySequence(QKeySequence::Redo));
    edit->addSeparator();
    registerAction(edit->addAction(tr("Cu&t"), this, [this] { editCommand(Cut); }), "edit.cut", key(Qt::CTRL | Qt::Key_X));
    registerAction(edit->addAction(tr("&Copy"), this, [this] { editCommand(Copy); }), "edit.copy", key(Qt::CTRL | Qt::Key_C));
    registerAction(edit->addAction(tr("&Paste"), this, [this] { editCommand(Paste); }), "edit.paste", key(Qt::CTRL | Qt::Key_V));
    registerAction(edit->addAction(tr("Select &All"), this, [this] { editCommand(SelectAll); }), "edit.selectAll",
                   key(Qt::CTRL | Qt::Key_A));
    edit->addSeparator();
    registerAction(edit->addAction(tr("Comment/Uncomment"), this, &MainWindow::toggleComment), "edit.toggleComment",
                   key(Qt::CTRL | Qt::Key_Slash));
    edit->addSeparator();
    registerAction(edit->addAction(tr("&Find..."), this, &MainWindow::find), "edit.find", key(Qt::CTRL | Qt::Key_F));
    registerAction(edit->addAction(tr("Find &Next"), this, [this] { findNext(true); }), "edit.findNext", key(Qt::Key_F3));
    registerAction(edit->addAction(tr("Find Pre&vious"), this, [this] { findNext(false); }), "edit.findPrevious",
                   key(Qt::SHIFT | Qt::Key_F3));
    registerAction(edit->addAction(tr("&Go to Line..."), this, &MainWindow::goToLine), "edit.goToLine",
                   key(Qt::CTRL | Qt::Key_L));

    // Sketch (Verify/Upload are the toolbar actions)
    QMenu *sketch = bar->addMenu(tr("&Sketch"));
    sketch->addAction(m_verifyAction);
    registerAction(m_verifyAction, "sketch.verify", key(Qt::CTRL | Qt::Key_R));
    sketch->addAction(m_uploadAction);
    registerAction(m_uploadAction, "sketch.upload", key(Qt::CTRL | Qt::Key_U));
    sketch->addSeparator();
    registerAction(sketch->addAction(tr("Show Sketch &Folder"), this, &MainWindow::showSketchFolder), "sketch.showFolder",
                   key(Qt::CTRL | Qt::Key_K));

    // Tools
    QMenu *tools = bar->addMenu(tr("&Tools"));
    m_boardMenu = tools->addMenu(tr("&Board"));
    connect(m_boardMenu, &QMenu::aboutToShow, this, &MainWindow::populateBoardMenu);
    m_portMenu = tools->addMenu(tr("&Port"));
    connect(m_portMenu, &QMenu::aboutToShow, this, [this] {
        scanPorts();          // refreshed in onPortsScanned() while the menu is open
        populatePortMenu();
    });
    tools->addSeparator();
    tools->addAction(m_serialAction);
    registerAction(m_serialAction, "tools.serialMonitor", key(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
    for (QAction *a : {m_verifyAction, m_uploadAction, m_serialAction})
        a->setIconVisibleInMenu(false);   // icons are colored for the toolbar buttons

    // View
    QMenu *view = bar->addMenu(tr("&View"));
    auto *group = new QActionGroup(this);
    m_darkThemeAction = view->addAction(tr("Dark Theme"));
    m_lightThemeAction = view->addAction(tr("Light Theme"));
    for (QAction *a : {m_darkThemeAction, m_lightThemeAction}) {
        a->setCheckable(true);
        group->addAction(a);
    }
    connect(m_darkThemeAction, &QAction::triggered, this, [this] { applyTheme(Theme::Mode::Dark); });
    connect(m_lightThemeAction, &QAction::triggered, this, [this] { applyTheme(Theme::Mode::Light); });
    registerAction(m_darkThemeAction, "view.darkTheme", QKeySequence());
    registerAction(m_lightThemeAction, "view.lightTheme", QKeySequence());

    // Help
    QMenu *help = bar->addMenu(tr("&Help"));
    registerAction(help->addAction(tr("Arduino &Reference"), this,
                                   [] { QDesktopServices::openUrl(QUrl(QString::fromLatin1(kReferenceUrl))); }),
                   "help.reference", QKeySequence());
    registerAction(help->addAction(tr("&Keyboard Shortcuts..."), this, [this] { showAdvancedSettings(0); }),
                   "help.keyboardShortcuts", QKeySequence());
    help->addSeparator();
    registerAction(help->addAction(tr("&About myide"), this, &MainWindow::showAbout), "help.about", QKeySequence());
}

QAction *MainWindow::registerAction(QAction *action, const char *id, const QKeySequence &defaultKey)
{
    action->setObjectName(QString::fromLatin1(id));
    action->setProperty("defaultShortcut", defaultKey);
    if (!action->property("baseToolTip").isValid())
        action->setProperty("baseToolTip", action->toolTip());
    addAction(action);   // shortcut works window-wide
    m_shortcutActions.append(action);
    return action;
}

// Default shortcuts, overridden by Advanced Settings ("shortcuts/<id>").
void MainWindow::applyShortcuts()
{
    QSettings settings;
    m_shortcutKeys.clear();
    for (QAction *a : m_shortcutActions) {
        const QString settingsKey = QStringLiteral("shortcuts/") + a->objectName();
        const QKeySequence k = settings.contains(settingsKey)
            ? QKeySequence(settings.value(settingsKey).toString(), QKeySequence::PortableText)
            : a->property("defaultShortcut").value<QKeySequence>();
        a->setShortcut(k);
        const QString base = a->property("baseToolTip").toString();
        a->setToolTip(k.isEmpty() ? base : QStringLiteral("%1 (%2)").arg(base, k.toString(QKeySequence::NativeText)));

        if (!k.isEmpty())
            m_shortcutKeys.insert(k[0]);
    }
    for (QsciScintilla *editor : editors())
        clearEditorBindings(editor);
}

// QScintilla binds keys of its own (Ctrl+U lowercase, Ctrl+L cut line,
// Ctrl+/ word part, ...) and would swallow them before the menu shortcut.
// Called for every editor tab, including ones opened later.
void MainWindow::clearEditorBindings(QsciScintilla *editor)
{
    for (QsciCommand *c : editor->standardCommands()->commands()) {
        if (m_shortcutKeys.contains(c->key()))
            c->setKey(0);
        if (m_shortcutKeys.contains(c->alternateKey()))
            c->setAlternateKey(0);
    }
}

// ---------------------------------------------------------------------------
// Dynamic menus
// ---------------------------------------------------------------------------

void MainWindow::populateRecentMenu()
{
    clearMenu(m_recentMenu);
    const QStringList recent = QSettings().value(QStringLiteral("recent/sketches")).toStringList();
    int shown = 0;
    for (const QString &dir : recent) {
        if (!QFileInfo(dir).isDir())
            continue;
        QAction *a = m_recentMenu->addAction(QFileInfo(dir).fileName());
        a->setToolTip(QDir::toNativeSeparators(dir));
        a->setStatusTip(QDir::toNativeSeparators(dir));
        connect(a, &QAction::triggered, this, [this, dir] {
            if (maybeSave())
                openSketchPath(dir, false);
        });
        ++shown;
    }
    if (shown == 0)
        addDisabled(m_recentMenu, tr("No recent sketches"));
    m_recentMenu->addSeparator();
    m_recentMenu->addAction(tr("Clear Recent"), this, [] { QSettings().remove(QStringLiteral("recent/sketches")); });
}

void MainWindow::populateExamplesMenu()
{
    if (!m_examplesFqbn.isEmpty() && m_examplesFqbn == currentFqbn())
        return;   // already filled for this board
    clearMenu(m_examplesMenu);
    addDisabled(m_examplesMenu, tr("Loading examples..."));
    loadExamples();
}

// `lib examples --json`: examples of installed libraries, including the
// libraries bundled with the selected board's core (--fqbn).
void MainWindow::loadExamples()
{
    if (m_examplesProcess)
        return;
    const QString fqbn = currentFqbn();
    QStringList args{QStringLiteral("lib"), QStringLiteral("examples")};
    if (!fqbn.isEmpty())
        args << QStringLiteral("--fqbn") << fqbn;
    m_examplesProcess = startCliJson(args,
        [this, fqbn](const QJsonObject &json) {
            m_examplesFqbn = fqbn;
            onExamplesLoaded(json);
        },
        [this] {
            m_examplesProcess = nullptr;
            if (m_examplesFqbn.isEmpty()) {
                clearMenu(m_examplesMenu);
                addDisabled(m_examplesMenu, tr("Could not load examples (see Output)"));
            }
        });
}

void MainWindow::onExamplesLoaded(const QJsonObject &json)
{
    struct Lib { QString name; QStringList paths; };
    QVector<Lib> forBoard, custom, other;
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);

    for (const QJsonValue &v : json.value(QStringLiteral("examples")).toArray()) {
        const QJsonObject entry = v.toObject();
        const QJsonObject lib = entry.value(QStringLiteral("library")).toObject();
        Lib l;
        l.name = lib.value(QStringLiteral("name")).toString();
        for (const QJsonValue &p : entry.value(QStringLiteral("examples")).toArray())
            l.paths << p.toString();
        if (l.name.isEmpty() || l.paths.isEmpty())
            continue;
        std::sort(l.paths.begin(), l.paths.end(), [&](const QString &a, const QString &b) {
            return collator.compare(QFileInfo(a).fileName(), QFileInfo(b).fileName()) < 0;
        });
        const QString location = lib.value(QStringLiteral("location")).toString();
        if (location == QLatin1String("platform") || location == QLatin1String("ref-platform"))
            forBoard.push_back(l);
        else if (location == QLatin1String("user"))
            custom.push_back(l);
        else
            other.push_back(l);
    }

    clearMenu(m_examplesMenu);
    const auto addSection = [&](const QString &title, QVector<Lib> &libs) {
        if (libs.isEmpty())
            return;
        std::sort(libs.begin(), libs.end(), [&](const Lib &a, const Lib &b) {
            return collator.compare(a.name, b.name) < 0;
        });
        if (!m_examplesMenu->isEmpty())
            m_examplesMenu->addSeparator();
        addDisabled(m_examplesMenu, title);
        for (const Lib &l : libs) {
            QMenu *sub = m_examplesMenu->addMenu(l.name);
            for (const QString &path : l.paths) {
                QAction *a = sub->addAction(QFileInfo(path).fileName());
                connect(a, &QAction::triggered, this, [this, path] { openExample(path); });
            }
        }
    };
    addSection(tr("Examples for %1").arg(m_boardCombo->currentText()), forBoard);
    addSection(tr("Examples from Custom Libraries"), custom);
    addSection(tr("Other Examples"), other);
    if (m_examplesMenu->isEmpty())
        addDisabled(m_examplesMenu, tr("No examples for this board"));
}

void MainWindow::populateBoardMenu()
{
    clearMenu(m_boardMenu);
    m_boardMenu->addAction(tr("Search Boards..."), this, [this] {
        m_boardCombo->setFocus();
        m_boardCombo->showPopup();
    });
    m_boardMenu->addSeparator();
    if (m_boardCombo->count() <= 1 && m_boardsProcess) {
        addDisabled(m_boardMenu, tr("Loading boards..."));
        return;
    }

    // One submenu per core, e.g. "Arduino AVR Boards", "esp32".
    QMap<QString, QVector<int>> byPlatform;
    for (int i = 0; i < m_boardCombo->count(); ++i)
        byPlatform[m_boardCombo->itemData(i, FqbnRole).toString().section(QLatin1Char(':'), 0, 1)].append(i);
    QStringList platforms = byPlatform.keys();
    std::sort(platforms.begin(), platforms.end(), [this](const QString &a, const QString &b) {
        return m_platformNames.value(a, a).compare(m_platformNames.value(b, b), Qt::CaseInsensitive) < 0;
    });

    auto *group = new QActionGroup(m_boardMenu);
    const QString current = currentFqbn();
    for (const QString &platform : platforms) {
        QMenu *sub = m_boardMenu->addMenu(m_platformNames.value(platform, platform));
        for (int i : byPlatform.value(platform)) {
            const QString fqbn = m_boardCombo->itemData(i, FqbnRole).toString();
            const QString name = m_boardCombo->itemText(i);
            QAction *a = sub->addAction(name);
            a->setCheckable(true);
            a->setChecked(fqbn == current);
            group->addAction(a);
            connect(a, &QAction::triggered, this, [this, fqbn, name] { selectBoard(fqbn, name); });
            if (fqbn == current)
                sub->setTitle(sub->title() + QStringLiteral("  ") + QChar(0x2022));   // marks the core in use
        }
    }
}

void MainWindow::populatePortMenu()
{
    clearMenu(m_portMenu);
    if (m_portsProcess)
        addDisabled(m_portMenu, tr("Scanning ports..."));
    auto *group = new QActionGroup(m_portMenu);
    for (int i = 0; i < m_portCombo->count(); ++i) {
        QAction *a = m_portMenu->addAction(m_portCombo->itemText(i));
        a->setCheckable(true);
        a->setChecked(i == m_portCombo->currentIndex());
        group->addAction(a);
        connect(a, &QAction::triggered, this, [this, i] {
            m_portCombo->setCurrentIndex(i);
            onPortActivated(i);
        });
    }
    if (m_portCombo->count() == 0 && !m_portsProcess)
        addDisabled(m_portMenu, tr("No ports found"));
}

// ---------------------------------------------------------------------------
// Edit commands
// ---------------------------------------------------------------------------

// Acts on the focused text widget (serial input, output pane) or the editor.
void MainWindow::editCommand(int op)
{
    QWidget *focused = QApplication::focusWidget();
    if (auto *line = qobject_cast<QLineEdit *>(focused)) {
        switch (op) {
        case Undo: line->undo(); break;
        case Redo: line->redo(); break;
        case Cut: line->cut(); break;
        case Copy: line->copy(); break;
        case Paste: line->paste(); break;
        case SelectAll: line->selectAll(); break;
        }
        return;
    }
    if (auto *text = qobject_cast<QPlainTextEdit *>(focused)) {
        switch (op) {
        case Undo: text->undo(); break;
        case Redo: text->redo(); break;
        case Cut: text->cut(); break;
        case Copy: text->copy(); break;
        case Paste: text->paste(); break;
        case SelectAll: text->selectAll(); break;
        }
        return;
    }
    switch (op) {
    case Undo: m_editor->undo(); break;
    case Redo: m_editor->redo(); break;
    case Cut: m_editor->cut(); break;
    case Copy: m_editor->copy(); break;
    case Paste: m_editor->paste(); break;
    case SelectAll: m_editor->selectAll(); break;
    }
}

// "// " at the lines' common indentation; removed again if every non-blank line has it.
void MainWindow::toggleComment()
{
    int lineFrom, indexFrom, lineTo, indexTo;
    if (m_editor->hasSelectedText()) {
        m_editor->getSelection(&lineFrom, &indexFrom, &lineTo, &indexTo);
        if (indexTo == 0 && lineTo > lineFrom)
            --lineTo;   // selection ends at the start of the next line
    } else {
        m_editor->getCursorPosition(&lineFrom, &indexFrom);
        lineTo = lineFrom;
    }

    bool allCommented = true;
    int minIndent = INT_MAX;
    for (int l = lineFrom; l <= lineTo; ++l) {
        const QString text = m_editor->text(l);
        const QString trimmed = text.trimmed();
        if (trimmed.isEmpty())
            continue;
        if (!trimmed.startsWith(QLatin1String("//")))
            allCommented = false;
        minIndent = qMin(minIndent, int(text.indexOf(trimmed.at(0))));
    }
    if (minIndent == INT_MAX)
        return;   // only blank lines

    m_editor->beginUndoAction();
    for (int l = lineFrom; l <= lineTo; ++l) {
        const QString text = m_editor->text(l);
        if (text.trimmed().isEmpty())
            continue;
        if (allCommented) {
            const int pos = text.indexOf(QLatin1String("//"));
            const int len = (text.size() > pos + 2 && text.at(pos + 2) == QLatin1Char(' ')) ? 3 : 2;
            m_editor->setSelection(l, pos, l, pos + len);
            m_editor->removeSelectedText();
        } else {
            m_editor->insertAt(QStringLiteral("// "), l, minIndent);
        }
    }
    m_editor->endUndoAction();
    if (lineTo > lineFrom)
        m_editor->setSelection(lineFrom, 0, lineTo, m_editor->text(lineTo).size());
}

void MainWindow::find()
{
    const QString initial = m_editor->hasSelectedText() ? m_editor->selectedText() : m_findText;
    bool ok = false;
    const QString text = QInputDialog::getText(this, tr("Find"), tr("Find:"), QLineEdit::Normal, initial, &ok);
    if (!ok || text.isEmpty())
        return;
    m_findText = text;
    findNext(true);
}

void MainWindow::findNext(bool forward)
{
    if (m_findText.isEmpty()) {
        find();
        return;
    }
    int lineFrom, indexFrom, lineTo, indexTo;
    if (m_editor->hasSelectedText()) {
        m_editor->getSelection(&lineFrom, &indexFrom, &lineTo, &indexTo);
    } else {
        m_editor->getCursorPosition(&lineFrom, &indexFrom);
        lineTo = lineFrom;
        indexTo = indexFrom;
    }
    // Start after the current match (forward) or before it (backward); wraps around.
    const bool found = forward
        ? m_editor->findFirst(m_findText, false, false, false, true, true, lineTo, indexTo)
        : m_editor->findFirst(m_findText, false, false, false, true, false, lineFrom, indexFrom);
    statusBar()->showMessage(found ? QString() : tr("\"%1\" not found").arg(m_findText), 3000);
    m_editor->setFocus();
}

void MainWindow::goToLine()
{
    int line, index;
    m_editor->getCursorPosition(&line, &index);
    bool ok = false;
    const int target = QInputDialog::getInt(this, tr("Go to Line"), tr("Line (1-%1):").arg(m_editor->lines()),
                                            line + 1, 1, m_editor->lines(), 1, &ok);
    if (!ok)
        return;
    m_editor->setCursorPosition(target - 1, 0);
    m_editor->ensureLineVisible(target - 1);
    m_editor->setFocus();
}

void MainWindow::showSketchFolder()
{
    QDir().mkpath(m_sketchDir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_sketchDir));
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About myide"),
        tr("<h3>myide %1</h3>"
           "<p>A fast, lightweight Arduino IDE.</p>"
           "<p>Qt %2 &middot; QScintilla %3<br>arduino-cli: %4</p>"
           "<p>Editor font: JetBrains Mono (SIL Open Font License 1.1)</p>")
            .arg(QStringLiteral(MYIDE_VERSION), QString::fromLatin1(qVersion()), QStringLiteral(QSCINTILLA_VERSION_STR),
                 m_cliPath.isEmpty() ? tr("not found yet") : QDir::toNativeSeparators(m_cliPath)));
}

// ---------------------------------------------------------------------------
// Sketch files
// ---------------------------------------------------------------------------

QString MainWindow::sketchbookDir() const
{
    return QSettings().value(QStringLiteral("prefs/sketchbook"), Preferences::defaultSketchbook()).toString();
}

void MainWindow::updateTitle()
{
    QString name = QFileInfo(m_sketchFile).completeBaseName();
    if (m_sketchIsTemp)
        name += tr(" (not saved)");
    setWindowTitle(QStringLiteral("%1[*] - myide").arg(name));
    setWindowModified(anyModified());
}

// Returns false if the user cancelled. Covers all tabs.
bool MainWindow::maybeSave()
{
    if (!anyModified())
        return true;
    const auto answer = QMessageBox::question(
        this, tr("Save changes?"),
        tr("Do you want to save changes to \"%1\"?").arg(QFileInfo(m_sketchFile).completeBaseName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save)
        return save();
    return answer == QMessageBox::Discard;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (maybeSave())
        event->accept();
    else
        event->ignore();
}

void MainWindow::openSketchPath(const QString &path, bool isTemp)
{
    m_sketchIsTemp = isTemp;
    resolveSketch(path);
    if (!isTemp)
        addRecent(m_sketchDir);
    updateTitle();
    m_editor->setFocus();
}

void MainWindow::addRecent(const QString &sketchDir)
{
    QSettings s;
    QStringList recent = s.value(QStringLiteral("recent/sketches")).toStringList();
    recent.removeAll(sketchDir);
    recent.prepend(sketchDir);
    while (recent.size() > kMaxRecent)
        recent.removeLast();
    s.setValue(QStringLiteral("recent/sketches"), recent);
}

// Untitled sketches live in <temp>/myide/sketch_<mmmdd><a-z> until saved.
void MainWindow::newSketch()
{
    if (!maybeSave())
        return;
    const QString base = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(QStringLiteral("myide"));
    const QString stem = QStringLiteral("sketch_") + QLocale::c().toString(QDate::currentDate(), QStringLiteral("MMMdd")).toLower();
    QString dir;
    for (char c = 'a'; c <= 'z'; ++c) {
        dir = QDir(base).filePath(stem + QLatin1Char(c));
        if (!QFileInfo::exists(dir))
            break;
    }
    openSketchPath(dir, true);
}

void MainWindow::openSketch()
{
    if (!maybeSave())
        return;
    QString file = QFileDialog::getOpenFileName(this, tr("Open Sketch"), sketchbookDir(),
                                                tr("Arduino sketches (*.ino *.pde);;All files (*)"));
    if (file.isEmpty())
        return;

    // arduino-cli needs <name>/<name>.ino; offer to fix it like the Arduino IDE does.
    const QFileInfo fi(file);
    const QString name = fi.completeBaseName();
    if (fi.dir().dirName() != name) {
        const auto answer = QMessageBox::question(
            this, tr("Move into a sketch folder?"),
            tr("\"%1\" must be inside a folder named \"%2\" to compile.\n\nCreate that folder and move the file into it?")
                .arg(fi.fileName(), name));
        if (answer != QMessageBox::Yes)
            return;
        const QString newDir = fi.dir().filePath(name);
        if (QFileInfo::exists(newDir)) {
            QMessageBox::warning(this, tr("Open Sketch"), tr("A folder named \"%1\" already exists there.").arg(name));
            return;
        }
        const QString moved = QDir(newDir).filePath(fi.fileName());
        if (!QDir().mkpath(newDir) || !QFile::rename(file, moved)) {
            QMessageBox::warning(this, tr("Open Sketch"), tr("Could not move the file into \"%1\".").arg(newDir));
            return;
        }
        file = moved;
    }
    openSketchPath(file, false);
}

// Examples are opened as a copy in the temp folder, so the library stays untouched.
void MainWindow::openExample(const QString &exampleDir)
{
    if (!maybeSave())
        return;
    const QString name = QFileInfo(exampleDir).fileName();
    const QString dest = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                             .filePath(QStringLiteral("myide/examples/") + name);
    QDir(dest).removeRecursively();
    if (!copyDir(exampleDir, dest)) {
        appendLog(tr("Error: could not copy example %1").arg(QDir::toNativeSeparators(exampleDir)));
        return;
    }
    openSketchPath(dest, true);
    appendLog(tr("Opened example \"%1\" (a copy; use Save As to keep your changes).").arg(name), Theme::LogKind::Command);
}

bool MainWindow::save()
{
    if (m_sketchIsTemp)
        return saveAs();
    if (!saveSketch())
        return false;
    statusBar()->showMessage(tr("Saved %1").arg(QDir::toNativeSeparators(m_sketchFile)), 3000);
    return true;
}

bool MainWindow::saveAs()
{
    QDir().mkpath(sketchbookDir());
    const QString suggested = QDir(sketchbookDir()).filePath(QFileInfo(m_sketchDir).fileName());
    const QString chosen = QFileDialog::getSaveFileName(this, tr("Save Sketch As"), suggested,
                                                        tr("Sketch folder (*)"), nullptr,
                                                        QFileDialog::DontConfirmOverwrite);
    if (chosen.isEmpty())
        return false;

    const QFileInfo ci(chosen);
    const QString suffix = ci.suffix().toLower();
    const QString name = (suffix == QLatin1String("ino") || suffix == QLatin1String("pde")) ? ci.completeBaseName()
                                                                                          : ci.fileName();
    if (!validSketchName(name)) {
        QMessageBox::warning(this, tr("Save Sketch As"),
                             tr("\"%1\" is not a valid sketch name.\n\nUse letters, digits, _ - and . "
                                "(not starting with - or .), at most 63 characters.").arg(name));
        return false;
    }
    const QString newDir = ci.dir().filePath(name);
    if (QFileInfo::exists(newDir) && QDir(newDir) != QDir(m_sketchDir)) {
        QMessageBox::warning(this, tr("Save Sketch As"), tr("A folder named \"%1\" already exists there.").arg(name));
        return false;
    }
    if (!QDir().mkpath(newDir)) {
        QMessageBox::warning(this, tr("Save Sketch As"), tr("Could not create \"%1\".").arg(newDir));
        return false;
    }

    // Carry the sketch's other files (headers, data, ...) along. Files open in
    // tabs are written from the editors below instead.
    QSet<QString> tabFiles;
    for (const QsciScintilla *editor : editors())
        tabFiles.insert(QFileInfo(editor->property("filePath").toString()).absoluteFilePath());
    const QDir oldDir(m_sketchDir);
    for (const QFileInfo &fi : oldDir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (tabFiles.contains(fi.absoluteFilePath()))
            continue;
        const QString dst = QDir(newDir).filePath(fi.fileName());
        if (fi.isDir())
            copyDir(fi.absoluteFilePath(), dst);
        else
            QFile::copy(fi.absoluteFilePath(), dst);
    }

    // Main tab becomes <name>.ino; the other tabs keep their file names.
    const QString newMain = QDir(newDir).filePath(name + QStringLiteral(".ino"));
    for (QsciScintilla *editor : editors()) {
        const QString old = editor->property("filePath").toString();
        const bool isMain = QFileInfo(old).absoluteFilePath() == QFileInfo(m_sketchFile).absoluteFilePath();
        editor->setProperty("filePath", isMain ? newMain : QDir(newDir).filePath(QFileInfo(old).fileName()));
    }
    m_sketchDir = newDir;
    m_sketchFile = newMain;
    m_sketchIsTemp = false;
    if (m_manual)
        m_manual->setSketchDir(m_sketchDir);   // its JSON file was copied above
    if (!saveSketch())
        return false;
    for (QsciScintilla *editor : editors())
        updateTabTitle(editor);
    addRecent(m_sketchDir);
    updateTitle();
    statusBar()->showMessage(tr("Saved %1").arg(QDir::toNativeSeparators(m_sketchFile)), 3000);
    return true;
}

// ---------------------------------------------------------------------------
// Preferences / Advanced settings
// ---------------------------------------------------------------------------

void MainWindow::loadPreferences()
{
    const Preferences p = Preferences::load();
    m_editorPointSize = p.fontSize;
    m_tabWidth = p.tabWidth;
    m_verbose = p.verbose;
    m_warnings = p.warnings;
}

void MainWindow::applyEditorPreferences()
{
    for (QsciScintilla *editor : editors()) {
        editor->setTabWidth(m_tabWidth);
        Theme::applyToEditor(editor, static_cast<QsciLexerCPP *>(editor->lexer()), m_theme,
                             Theme::monoFont(m_editorPointSize));
        updateMarginWidth(editor);
    }
}

QStringList MainWindow::compileOptions() const
{
    QStringList options;
    if (m_verbose)
        options << QStringLiteral("-v");
    if (m_warnings != QLatin1String("default"))
        options << QStringLiteral("--warnings") << m_warnings;
    return options;
}

void MainWindow::showPreferences()
{
    PreferencesDialog dialog(Preferences::load(), this);
    dialog.setStyleSheet(Theme::styleSheet(m_theme));
    if (dialog.exec() != QDialog::Accepted)
        return;
    const Preferences p = dialog.result();
    p.save();
    loadPreferences();
    applyTheme(p.light ? Theme::Mode::Light : Theme::Mode::Dark);
    applyEditorPreferences();
}

void MainWindow::showAdvancedSettings(int tab)
{
    AdvancedSettingsDialog dialog(m_shortcutActions, tab, this);
    dialog.setStyleSheet(Theme::styleSheet(m_theme));
    if (dialog.exec() != QDialog::Accepted)
        return;
    applyShortcuts();
    m_cliPath.clear();   // re-resolve (path may have changed)
}
