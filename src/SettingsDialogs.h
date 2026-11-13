#pragma once

#include <QDialog>
#include <QList>
#include <QString>

class QAction;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QTableWidget;
class QTabWidget;

// Stored in QSettings under "prefs/...".
struct Preferences {
    QString sketchbook;      // default folder for Open / Save As
    int fontSize = 11;       // editor, points
    int tabWidth = 2;
    bool light = false;      // theme
    bool verbose = false;    // arduino-cli -v for compile and upload
    QString warnings = QStringLiteral("default");   // none | default | more | all

    static Preferences load();
    void save() const;
    static QString defaultSketchbook();
};

class PreferencesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PreferencesDialog(const Preferences &prefs, QWidget *parent = nullptr);
    Preferences result() const;

private:
    QLineEdit *m_sketchbook = nullptr;
    QSpinBox *m_fontSize = nullptr;
    QSpinBox *m_tabWidth = nullptr;
    QComboBox *m_theme = nullptr;
    QCheckBox *m_verbose = nullptr;
    QComboBox *m_warnings = nullptr;
};

// Advanced settings: keyboard shortcuts for every registered action, and the
// arduino-cli path. Saves to QSettings on OK ("shortcuts/<id>", "advanced/cliPath").
class AdvancedSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    AdvancedSettingsDialog(const QList<QAction *> &actions, int tab, QWidget *parent = nullptr);

    void accept() override;

private:
    void filterRows(const QString &text);
    void resetAll();

    QList<QAction *> m_actions;
    QTabWidget *m_tabs = nullptr;
    QTableWidget *m_table = nullptr;
    QLineEdit *m_cliPath = nullptr;
};
