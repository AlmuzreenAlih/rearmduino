#include "SerialCommandGuide.h"
#include "Theme.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSaveFile>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
const char kFileName[] = "serial-commands.json";

// JSON string literal (quotes and escapes) for the hand-formatted file.
QString quoted(const QString &s)
{
    const QString arr = QString::fromUtf8(QJsonDocument(QJsonArray{s}).toJson(QJsonDocument::Compact));
    return arr.mid(1, arr.size() - 2);   // strip [ ]
}
}

QString SerialCommandGuide::fileName()
{
    return QString::fromLatin1(kFileName);
}

SerialCommandGuide::SerialCommandGuide(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("commandGuide"));
    setAttribute(Qt::WA_StyledBackground);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);

    auto *header = new QHBoxLayout;
    auto *title = new QLabel(tr("Serial Commands"));
    title->setObjectName(QStringLiteral("manualTitle"));
    m_status = new QLabel;
    m_status->setObjectName(QStringLiteral("manualStatus"));
    m_add = new QToolButton;
    m_add->setObjectName(QStringLiteral("manualAdd"));
    m_add->setText(tr("+ Add"));
    m_add->setToolTip(tr("Add a serial command to the list"));
    m_add->setFocusPolicy(Qt::NoFocus);
    connect(m_add, &QToolButton::clicked, this, &SerialCommandGuide::addCommand);
    header->addWidget(title);
    header->addSpacing(12);
    header->addWidget(m_status, 1);
    header->addWidget(m_add);
    layout->addLayout(header);

    m_list = new QTreeWidget;
    m_list->setObjectName(QStringLiteral("commandList"));
    m_list->setColumnCount(2);
    m_list->setHeaderLabels({tr("Serial Text Command"), tr("Command Name")});
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->header()->setSectionsMovable(false);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_list->header()->setStretchLastSection(true);
    m_list->setColumnWidth(0, 260);
    connect(m_list, &QTreeWidget::customContextMenuRequested, this, &SerialCommandGuide::showMenu);
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        editCommand(m_list->indexOfTopLevelItem(item));
    });
    layout->addWidget(m_list, 1);

    m_empty = new QLabel(tr("No commands yet. Click + Add."));
    m_empty->setObjectName(QStringLiteral("manualEmpty"));
    layout->addWidget(m_empty);
}

// ---------------------------------------------------------------------------
// File
// ---------------------------------------------------------------------------

void SerialCommandGuide::setSketchDir(const QString &dir)
{
    if (dir == m_dir) {
        reloadIfChanged();
        return;
    }
    m_dir = dir;
    load();
}

void SerialCommandGuide::reloadIfChanged()
{
    const QFileInfo fi(QDir(m_dir).filePath(fileName()));
    const QDateTime t = fi.exists() ? fi.lastModified() : QDateTime();
    if (t != m_mtime)
        load();
}

void SerialCommandGuide::load()
{
    m_commands.clear();
    m_loadError.clear();
    m_mtime = QDateTime();

    const QString path = QDir(m_dir).filePath(fileName());
    QFile file(path);
    if (file.exists()) {
        m_mtime = QFileInfo(path).lastModified();
        if (!file.open(QIODevice::ReadOnly)) {
            m_loadError = tr("Cannot read %1: %2").arg(fileName(), file.errorString());
        } else {
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
            if (!doc.isObject()) {
                m_loadError = tr("%1 is not valid JSON (%2 at offset %3); fix it in a text editor.")
                                  .arg(fileName(), err.errorString()).arg(err.offset);
            } else {
                for (const QJsonValue &v : doc.object().value(QStringLiteral("commands")).toArray()) {
                    const QJsonObject o = v.toObject();
                    m_commands.append({o.value(QStringLiteral("command")).toString(),
                                       o.value(QStringLiteral("name")).toString()});
                }
            }
        }
    }
    rebuild();
    setStatus(m_loadError, !m_loadError.isEmpty());
}

bool SerialCommandGuide::save()
{
    if (!m_loadError.isEmpty())
        return false;   // never overwrite a file we couldn't read
    const QString path = QDir(m_dir).filePath(fileName());
    QDir().mkpath(m_dir);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(toJson().toUtf8()) < 0 || !file.commit()) {
        setStatus(tr("Cannot save %1: %2").arg(QDir::toNativeSeparators(path), file.errorString()), true);
        return false;
    }
    m_mtime = QFileInfo(path).lastModified();
    setStatus(QString(), false);
    return true;
}

// Hand-formatted (QJsonDocument sorts keys): command, name.
QString SerialCommandGuide::toJson() const
{
    QString out = QStringLiteral("{\n  \"commands\": [");
    for (int i = 0; i < m_commands.size(); ++i) {
        out += QStringLiteral("\n    { \"command\": %1, \"name\": %2 }")
                   .arg(quoted(m_commands[i].command), quoted(m_commands[i].name));
        if (i + 1 < m_commands.size())
            out += QLatin1Char(',');
    }
    out += m_commands.isEmpty() ? QStringLiteral("]\n}\n") : QStringLiteral("\n  ]\n}\n");
    return out;
}

// ---------------------------------------------------------------------------
// List
// ---------------------------------------------------------------------------

void SerialCommandGuide::rebuild()
{
    m_list->clear();
    const QFont mono = Theme::monoFont(10);
    for (const Command &c : m_commands) {
        auto *item = new QTreeWidgetItem(m_list, {c.command, c.name});
        item->setFont(0, mono);
        item->setToolTip(0, tr("Double-click to edit, right-click for more."));
    }
    m_add->setEnabled(m_loadError.isEmpty());
    m_empty->setVisible(m_commands.isEmpty() && m_loadError.isEmpty());
    m_list->setToolTip(QDir::toNativeSeparators(QDir(m_dir).filePath(fileName())));
}

void SerialCommandGuide::setStatus(const QString &text, bool isError)
{
    m_status->setText(text);
    m_status->setProperty("error", isError);
    m_status->style()->unpolish(m_status);
    m_status->style()->polish(m_status);
}

void SerialCommandGuide::showMenu(const QPoint &pos)
{
    if (!m_loadError.isEmpty())
        return;
    QTreeWidgetItem *item = m_list->itemAt(pos);
    QMenu menu(this);
    if (!item) {
        menu.addAction(tr("Add..."), this, &SerialCommandGuide::addCommand);
        menu.exec(m_list->viewport()->mapToGlobal(pos));
        return;
    }
    const int index = m_list->indexOfTopLevelItem(item);
    QAction *copy = menu.addAction(tr("Copy Command"));
    QAction *edit = menu.addAction(tr("Edit..."));
    QAction *up = menu.addAction(tr("Move Up"));
    QAction *down = menu.addAction(tr("Move Down"));
    menu.addSeparator();
    QAction *del = menu.addAction(tr("Delete"));
    up->setEnabled(index > 0);
    down->setEnabled(index + 1 < m_commands.size());
    QAction *chosen = menu.exec(m_list->viewport()->mapToGlobal(pos));
    // Deferred: the list is rebuilt (its items deleted) by these.
    if (chosen == copy)
        copyCommand(index);
    else if (chosen == edit)
        QTimer::singleShot(0, this, [this, index] { editCommand(index); });
    else if (chosen == up)
        QTimer::singleShot(0, this, [this, index] { moveCommand(index, -1); });
    else if (chosen == down)
        QTimer::singleShot(0, this, [this, index] { moveCommand(index, 1); });
    else if (chosen == del)
        QTimer::singleShot(0, this, [this, index] { removeCommand(index); });
}

// ---------------------------------------------------------------------------
// Add / edit / move / delete
// ---------------------------------------------------------------------------

void SerialCommandGuide::addCommand()
{
    if (!m_loadError.isEmpty())
        return;
    Command cmd;
    if (!runDialog(cmd, -1, tr("Add Serial Command")))
        return;
    m_commands.append(cmd);
    save();
    rebuild();
    m_list->setCurrentItem(m_list->topLevelItem(m_commands.size() - 1));
}

void SerialCommandGuide::editCommand(int index)
{
    if (index < 0 || index >= m_commands.size() || !m_loadError.isEmpty())
        return;
    Command cmd = m_commands[index];
    if (!runDialog(cmd, index, tr("Edit Serial Command")))
        return;
    m_commands[index] = cmd;
    save();
    rebuild();
    m_list->setCurrentItem(m_list->topLevelItem(index));
}

void SerialCommandGuide::removeCommand(int index)
{
    if (index < 0 || index >= m_commands.size())
        return;
    const auto answer = QMessageBox::question(this, tr("Delete Command"),
                                              tr("Delete \"%1\"?").arg(m_commands[index].command));
    if (answer != QMessageBox::Yes)
        return;
    m_commands.removeAt(index);
    save();
    rebuild();
}

void SerialCommandGuide::moveCommand(int index, int delta)
{
    const int to = index + delta;
    if (index < 0 || index >= m_commands.size() || to < 0 || to >= m_commands.size())
        return;
    m_commands.swapItemsAt(index, to);
    save();
    rebuild();
    m_list->setCurrentItem(m_list->topLevelItem(to));
}

void SerialCommandGuide::copyCommand(int index)
{
    if (index < 0 || index >= m_commands.size())
        return;
    QApplication::clipboard()->setText(m_commands[index].command);
    setStatus(tr("Copied \"%1\"").arg(m_commands[index].command), false);
}

bool SerialCommandGuide::runDialog(Command &cmd, int editingIndex, const QString &title)
{
    QDialog dlg(this);
    dlg.setObjectName(QStringLiteral("manualDialog"));
    dlg.setWindowTitle(title);
    dlg.setWindowFlags(dlg.windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto *layout = new QVBoxLayout(&dlg);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto *command = new QLineEdit(cmd.command);
    command->setFont(Theme::monoFont(10));
    command->setPlaceholderText(tr("e.g. MIXER_HOME"));
    command->setMinimumWidth(280);
    form->addRow(tr("Serial Text Command"), command);

    auto *name = new QLineEdit(cmd.name);
    name->setPlaceholderText(tr("e.g. Mixer Home"));
    form->addRow(tr("Command Name"), name);
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, [&] {
        if (command->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dlg, title, tr("Enter the serial text command."));
            command->setFocus();
            return;
        }
        if (name->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dlg, title, tr("Enter a command name."));
            name->setFocus();
            return;
        }
        for (int i = 0; i < m_commands.size(); ++i) {
            if (i != editingIndex && m_commands[i].command == command->text().trimmed()) {
                QMessageBox::warning(&dlg, title, tr("\"%1\" is already in the list (%2).")
                                                      .arg(m_commands[i].command, m_commands[i].name));
                command->setFocus();
                return;
            }
        }
        dlg.accept();
    });
    command->setFocus();

    if (dlg.exec() != QDialog::Accepted)
        return false;
    cmd.command = command->text().trimmed();
    cmd.name = name->text().trimmed();
    return true;
}
