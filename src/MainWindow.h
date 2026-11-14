#pragma once

#include "Theme.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QMainWindow>
#include <QProcess>
#include <QString>

#include <functional>

class QAction;
class QComboBox;
class QJsonObject;
class QMenu;
class QPlainTextEdit;
class QSplitter;
class QStackedWidget;
class QTabBar;
class QTabWidget;
class ManualControl;
class SerialCommandGuide;
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

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void verify();
    void upload();
    void loadBoards();
    void scanPorts();
    void onPortActivated(int index);
    void onProcessOutput();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError error);
    void toggleSerialMonitor(bool show);

    // File menu (MainWindowMenus.cpp)
    void newSketch();
    void openSketch();
    bool save();
    bool saveAs();
    void showPreferences();
    void showAdvancedSettings(int tab = 0);

private:
    // Editor tabs
    QsciScintilla *createEditor(const QString &filePath);
    void setupEditor(QsciScintilla *editor);
    QList<QsciScintilla *> editors() const;
    bool anyModified() const;
    void clearEditorBindings(QsciScintilla *editor);
    void updateTabTitle(QsciScintilla *editor);
    void updateMarginWidth(QsciScintilla *editor);
    void themeEditorScrollBars(QsciScintilla *editor, const QString &qss);
    void setupToolbar();
    QWidget *createOutputPane();
    void showBottomTab(int index);   // 0 Output, 1 Manual Control (hidden), 2 Serial Command Guide
    QString manualSend(const QByteArray &data, const QString &label);

    // Menus (MainWindowMenus.cpp)
    void setupMenus();
    QAction *registerAction(QAction *action, const char *id, const QKeySequence &defaultKey);
    void applyShortcuts();
    void populateRecentMenu();
    void populateExamplesMenu();
    void loadExamples();
    void onExamplesLoaded(const QJsonObject &json);
    void populateBoardMenu();
    void populatePortMenu();
    void editCommand(int op);
    void toggleComment();
    void find();
    void findNext(bool forward);
    void goToLine();
    void showSketchFolder();
    void showAbout();
    void showPortableBuilder();
    // Offers the bundled driver for plugged-in boards that have none. interactive:
    // from the menu (always reports); otherwise asks at most once per kind per session.
    void checkUsbDrivers(bool interactive);

    // Sketch files (MainWindowMenus.cpp)
    bool maybeSave();
    void openSketchPath(const QString &path, bool isTemp);
    void openExample(const QString &exampleDir);
    void addRecent(const QString &sketchDir);
    void updateTitle();
    QString sketchbookDir() const;

    // Preferences
    void loadPreferences();
    void applyEditorPreferences();

    void resolveSketch(const QString &path);
    bool saveSketch();

    void applyTheme(Theme::Mode mode);
    void recolorOutput();
    void reopenSerialAfterUpload();
    void themeSplitterHandles();

    QString cliPath();
    void startTask(const QString &name, const QStringList &args);
    QStringList compileOptions() const;
    QProcess *startCliJson(const QStringList &args,
                           std::function<void(const QJsonObject &)> onSuccess,
                           std::function<void()> onDone);
    void onBoardsLoaded(const QJsonObject &json);
    void onPortsScanned(const QJsonObject &json);
    void startUpload();
    void updatePortStatus();          // "(PLUGGED)" / "(DISCONNECTED)" on each port item
    bool isPortPresent(const QString &address) const;

    void selectBoard(const QString &fqbn, const QString &name);
    QString currentFqbn() const;
    QString currentPort() const;

    void appendLog(const QString &line);                       // kind detected from text
    void appendLog(const QString &line, Theme::LogKind kind);
    void flushPartialLine();
    void setBusy(bool busy);

    Theme::Mode m_theme = Theme::Mode::Dark;

    // One editor per sketch file (.ino), in tabs; m_editor is the current tab's.
    QTabWidget *m_tabs = nullptr;
    QsciScintilla *m_editor = nullptr;
    QSet<int> m_shortcutKeys;   // keys taken by menu shortcuts (removed from QScintilla)
    QPlainTextEdit *m_output = nullptr;
    QSplitter *m_topSplitter = nullptr;   // editor | serial monitor
    QSplitter *m_vSplitter = nullptr;     // top area / output pane
    QToolBar *m_toolbar = nullptr;
    QWidget *m_outputPane = nullptr;
    QTabBar *m_bottomTabs = nullptr;            // Output | Manual Control
    QStackedWidget *m_bottomStack = nullptr;
    ManualControl *m_manual = nullptr;          // created on first open
    SerialCommandGuide *m_guide = nullptr;      // created on first open
    QSet<int> m_driverPromptsShown;             // UsbDrivers::Kind values asked about this session
    bool m_serialKeepFocus = false;             // opening the monitor for Manual Control
    QAction *m_verifyAction = nullptr;
    QAction *m_uploadAction = nullptr;
    QAction *m_serialAction = nullptr;
    SerialMonitor *m_serial = nullptr;     // created on first open
    bool m_reopenSerialAfterUpload = false;
    QAction *m_darkThemeAction = nullptr;
    QAction *m_lightThemeAction = nullptr;
    QComboBox *m_boardCombo = nullptr;
    PortComboBox *m_portCombo = nullptr;

    // Menus filled when opened.
    QMenu *m_recentMenu = nullptr;
    QMenu *m_examplesMenu = nullptr;
    QMenu *m_boardMenu = nullptr;
    QMenu *m_portMenu = nullptr;
    QList<QAction *> m_shortcutActions;     // everything shown in Keyboard Shortcuts
    QHash<QString, QString> m_platformNames; // "arduino:avr" -> "Arduino AVR Boards"

    // Examples (loaded on first open of File > Examples, per board).
    QProcess *m_examplesProcess = nullptr;
    QString m_examplesFqbn;   // board the Examples menu was filled for

    // Verify/Upload process (output streamed to the log pane).
    QProcess *m_process = nullptr;
    QTextDecoder *m_decoder = nullptr;
    QString m_partialLine;
    QString m_taskName;

    // Background JSON queries.
    QProcess *m_boardsProcess = nullptr;
    QProcess *m_portsProcess = nullptr;
    bool m_uploadAfterScan = false;
    QTimer *m_portWatch = nullptr;    // local port check (no arduino-cli), every second
    int m_portWasPresent = -1;        // last state of the selected port: -1 unknown, 0/1

    // Preferences
    qreal m_editorPointSize = 11;
    int m_tabWidth = 2;
    bool m_verbose = false;
    QString m_warnings = QStringLiteral("default");

    QString m_findText;
    QString m_cliPath;
    QString m_sketchDir;   // folder passed to arduino-cli
    QString m_sketchFile;  // <dir>/<dirname>.ino
    bool m_sketchIsTemp = false;   // untitled or example copy: Save goes to Save As
};
