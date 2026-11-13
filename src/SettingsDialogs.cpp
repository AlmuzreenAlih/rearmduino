#include "SettingsDialogs.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// Preferences
// ---------------------------------------------------------------------------

QString Preferences::defaultSketchbook()
{
    // Same defaults as the Arduino IDE / arduino-cli: <Documents>/Arduino on
    // Windows and macOS, ~/Arduino on Linux.
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    return QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
        .filePath(QStringLiteral("Arduino"));
#else
    return QDir::home().filePath(QStringLiteral("Arduino"));
#endif
}

Preferences Preferences::load()
{
    QSettings s;
    Preferences p;
    p.sketchbook = s.value(QStringLiteral("prefs/sketchbook"), defaultSketchbook()).toString();
    p.fontSize = s.value(QStringLiteral("prefs/fontSize"), 11).toInt();
    p.tabWidth = s.value(QStringLiteral("prefs/tabWidth"), 2).toInt();
    p.light = s.value(QStringLiteral("view/theme")).toString() == QLatin1String("light");
    p.verbose = s.value(QStringLiteral("prefs/verbose"), false).toBool();
    p.warnings = s.value(QStringLiteral("prefs/warnings"), QStringLiteral("default")).toString();
    return p;
}

void Preferences::save() const
{
    QSettings s;
    s.setValue(QStringLiteral("prefs/sketchbook"), sketchbook);
    s.setValue(QStringLiteral("prefs/fontSize"), fontSize);
    s.setValue(QStringLiteral("prefs/tabWidth"), tabWidth);
    s.setValue(QStringLiteral("view/theme"), light ? QStringLiteral("light") : QStringLiteral("dark"));
    s.setValue(QStringLiteral("prefs/verbose"), verbose);
    s.setValue(QStringLiteral("prefs/warnings"), warnings);
}

PreferencesDialog::PreferencesDialog(const Preferences &prefs, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Preferences"));
    auto *form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    auto *bookRow = new QHBoxLayout;
    m_sketchbook = new QLineEdit(QDir::toNativeSeparators(prefs.sketchbook));
    auto *browse = new QPushButton(tr("Browse..."));
    bookRow->addWidget(m_sketchbook, 1);
    bookRow->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Sketchbook Location"), m_sketchbook->text());
        if (!dir.isEmpty())
            m_sketchbook->setText(QDir::toNativeSeparators(dir));
    });
    form->addRow(tr("Sketchbook location:"), bookRow);

    m_fontSize = new QSpinBox;
    m_fontSize->setRange(8, 32);
    m_fontSize->setValue(prefs.fontSize);
    m_fontSize->setSuffix(tr(" pt"));
    form->addRow(tr("Editor font size:"), m_fontSize);

    m_tabWidth = new QSpinBox;
    m_tabWidth->setRange(1, 8);
    m_tabWidth->setValue(prefs.tabWidth);
    form->addRow(tr("Tab size (spaces):"), m_tabWidth);

    m_theme = new QComboBox;
    m_theme->addItems({tr("Dark"), tr("Light")});
    m_theme->setCurrentIndex(prefs.light ? 1 : 0);
    form->addRow(tr("Theme:"), m_theme);

    m_verbose = new QCheckBox(tr("Show verbose output during compile and upload"));
    m_verbose->setChecked(prefs.verbose);
    form->addRow(QString(), m_verbose);

    m_warnings = new QComboBox;
    const QStringList levels{QStringLiteral("none"), QStringLiteral("default"), QStringLiteral("more"), QStringLiteral("all")};
    m_warnings->addItems({tr("None"), tr("Default"), tr("More"), tr("All")});
    for (int i = 0; i < levels.size(); ++i)
        m_warnings->setItemData(i, levels.at(i));
    m_warnings->setCurrentIndex(qMax(0, levels.indexOf(prefs.warnings)));
    form->addRow(tr("Compiler warnings:"), m_warnings);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addStretch();
    layout->addWidget(buttons);
    resize(560, sizeHint().height());
}

Preferences PreferencesDialog::result() const
{
    Preferences p;
    p.sketchbook = QDir::fromNativeSeparators(m_sketchbook->text().trimmed());
    if (p.sketchbook.isEmpty())
        p.sketchbook = Preferences::defaultSketchbook();
    p.fontSize = m_fontSize->value();
    p.tabWidth = m_tabWidth->value();
    p.light = m_theme->currentIndex() == 1;
    p.verbose = m_verbose->isChecked();
    p.warnings = m_warnings->currentData().toString();
    return p;
}

// ---------------------------------------------------------------------------
// Advanced settings
// ---------------------------------------------------------------------------

namespace {
QString plainText(const QAction *a)
{
    QString t = a->text();
    t.remove(QLatin1Char('&'));
    if (t.endsWith(QLatin1String("...")))
        t.chop(3);
    return t;
}
}

AdvancedSettingsDialog::AdvancedSettingsDialog(const QList<QAction *> &actions, int tab, QWidget *parent)
    : QDialog(parent), m_actions(actions)
{
    setWindowTitle(tr("Advanced Settings"));
    m_tabs = new QTabWidget;

    // --- Keyboard shortcuts ---
    auto *shortcutsPage = new QWidget;
    auto *sl = new QVBoxLayout(shortcutsPage);
    auto *search = new QLineEdit;
    search->setPlaceholderText(tr("Search commands"));
    search->setClearButtonEnabled(true);
    sl->addWidget(search);

    m_table = new QTableWidget(m_actions.size(), 3);
    m_table->setHorizontalHeaderLabels({tr("Command"), tr("Shortcut"), tr("Default")});
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->setColumnWidth(1, 180);
    for (int row = 0; row < m_actions.size(); ++row) {
        QAction *a = m_actions.at(row);
        auto *name = new QTableWidgetItem(plainText(a));
        name->setToolTip(a->objectName());
        m_table->setItem(row, 0, name);
        auto *edit = new QKeySequenceEdit(a->shortcut());
        // One chord only (e.g. Ctrl+Shift+M); multi-chord sequences are confusing.
        connect(edit, &QKeySequenceEdit::editingFinished, edit, [edit] {
            const QKeySequence k = edit->keySequence();
            if (k.count() > 1)
                edit->setKeySequence(QKeySequence(k[0]));
        });
        m_table->setCellWidget(row, 1, edit);
        const QKeySequence def = a->property("defaultShortcut").value<QKeySequence>();
        m_table->setItem(row, 2, new QTableWidgetItem(def.toString(QKeySequence::NativeText)));
    }
    sl->addWidget(m_table, 1);

    auto *rowButtons = new QHBoxLayout;
    auto *clearButton = new QPushButton(tr("Clear Selected"));
    auto *resetButton = new QPushButton(tr("Reset All to Defaults"));
    rowButtons->addWidget(new QLabel(tr("Click a shortcut field and press the new key combination.")), 1);
    rowButtons->addWidget(clearButton);
    rowButtons->addWidget(resetButton);
    sl->addLayout(rowButtons);
    connect(search, &QLineEdit::textChanged, this, &AdvancedSettingsDialog::filterRows);
    connect(resetButton, &QPushButton::clicked, this, &AdvancedSettingsDialog::resetAll);
    connect(clearButton, &QPushButton::clicked, this, [this] {
        const int row = m_table->currentRow();
        if (row >= 0)
            static_cast<QKeySequenceEdit *>(m_table->cellWidget(row, 1))->clear();
    });
    m_tabs->addTab(shortcutsPage, tr("Keyboard Shortcuts"));

    // --- arduino-cli ---
    auto *cliPage = new QWidget;
    auto *cl = new QFormLayout(cliPage);
    auto *cliRow = new QHBoxLayout;
    m_cliPath = new QLineEdit(QDir::toNativeSeparators(QSettings().value(QStringLiteral("advanced/cliPath")).toString()));
    m_cliPath->setPlaceholderText(tr("Empty: find arduino-cli on PATH"));
    auto *browse = new QPushButton(tr("Browse..."));
    cliRow->addWidget(m_cliPath, 1);
    cliRow->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("arduino-cli executable"), m_cliPath->text());
        if (!f.isEmpty())
            m_cliPath->setText(QDir::toNativeSeparators(f));
    });
    cl->addRow(tr("arduino-cli path:"), cliRow);
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("arduino-cli"));
    auto *found = new QLabel(onPath.isEmpty() ? tr("Not found on PATH.")
                                              : tr("Found on PATH: %1").arg(QDir::toNativeSeparators(onPath)));
    found->setWordWrap(true);
    found->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cl->addRow(QString(), found);
    m_tabs->addTab(cliPage, tr("arduino-cli"));

    m_tabs->setCurrentIndex(qBound(0, tab, m_tabs->count() - 1));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &AdvancedSettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_tabs, 1);
    layout->addWidget(buttons);
    resize(640, 560);
}

void AdvancedSettingsDialog::filterRows(const QString &text)
{
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const auto *edit = static_cast<QKeySequenceEdit *>(m_table->cellWidget(row, 1));
        const bool match = text.isEmpty()
            || m_table->item(row, 0)->text().contains(text, Qt::CaseInsensitive)
            || edit->keySequence().toString(QKeySequence::NativeText).contains(text, Qt::CaseInsensitive);
        m_table->setRowHidden(row, !match);
    }
}

void AdvancedSettingsDialog::resetAll()
{
    for (int row = 0; row < m_actions.size(); ++row) {
        auto *edit = static_cast<QKeySequenceEdit *>(m_table->cellWidget(row, 1));
        edit->setKeySequence(m_actions.at(row)->property("defaultShortcut").value<QKeySequence>());
    }
}

void AdvancedSettingsDialog::accept()
{
    // Refuse duplicate shortcuts (Qt would make both ambiguous: neither fires).
    QHash<QString, QStringList> byKey;
    for (int row = 0; row < m_actions.size(); ++row) {
        const QKeySequence k = static_cast<QKeySequenceEdit *>(m_table->cellWidget(row, 1))->keySequence();
        if (!k.isEmpty())
            byKey[k.toString(QKeySequence::NativeText)].append(m_table->item(row, 0)->text());
    }
    QStringList conflicts;
    for (auto it = byKey.cbegin(); it != byKey.cend(); ++it) {
        if (it.value().size() > 1)
            conflicts << QStringLiteral("%1: %2").arg(it.key(), it.value().join(QStringLiteral(", ")));
    }
    if (!conflicts.isEmpty()) {
        m_tabs->setCurrentIndex(0);
        QMessageBox::warning(this, tr("Duplicate shortcuts"),
                             tr("Each shortcut can only be used once:\n\n%1").arg(conflicts.join(QLatin1Char('\n'))));
        return;
    }

    QSettings s;
    for (int row = 0; row < m_actions.size(); ++row) {
        const QAction *a = m_actions.at(row);
        const QKeySequence k = static_cast<QKeySequenceEdit *>(m_table->cellWidget(row, 1))->keySequence();
        const QString key = QStringLiteral("shortcuts/") + a->objectName();
        if (k == a->property("defaultShortcut").value<QKeySequence>())
            s.remove(key);   // only store changes
        else
            s.setValue(key, k.toString(QKeySequence::PortableText));
    }
    const QString cli = QDir::fromNativeSeparators(m_cliPath->text().trimmed());
    if (cli.isEmpty())
        s.remove(QStringLiteral("advanced/cliPath"));
    else
        s.setValue(QStringLiteral("advanced/cliPath"), cli);
    QDialog::accept();
}
