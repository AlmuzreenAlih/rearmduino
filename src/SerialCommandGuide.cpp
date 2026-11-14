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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
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

SerialCommandGuide::SerialCommandGuide(Sender sender, QWidget *parent)
    : QWidget(parent), m_sender(std::move(sender))
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

    auto *scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("manualScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *list = new QWidget;
    list->setObjectName(QStringLiteral("manualList"));
    list->setAttribute(Qt::WA_StyledBackground);
    m_rows = new QVBoxLayout(list);
    m_rows->setContentsMargins(0, 0, 4, 0);
    m_rows->setSpacing(6);
    scroll->setWidget(list);
    layout->addWidget(scroll, 1);
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
    QLayoutItem *li;
    while ((li = m_rows->takeAt(0)) != nullptr) {
        if (QWidget *w = li->widget()) {
            w->hide();
            w->deleteLater();   // may hold the button whose menu triggered this
        }
        delete li;
    }
    m_add->setEnabled(m_loadError.isEmpty());

    if (m_commands.isEmpty() && m_loadError.isEmpty()) {
        auto *empty = new QLabel(tr("No commands yet. Click + Add."));
        empty->setObjectName(QStringLiteral("manualEmpty"));
        m_rows->addWidget(empty);
    }
    const QFont mono = Theme::monoFont(10);
    for (int i = 0; i < m_commands.size(); ++i) {
        // [command] - name
        auto *row = new QWidget;
        row->setContextMenuPolicy(Qt::CustomContextMenu);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);
        auto *button = new QPushButton(m_commands[i].command);
        button->setObjectName(QStringLiteral("commandButton"));
        button->setFont(mono);
        button->setAutoDefault(false);
        button->setCursor(Qt::PointingHandCursor);
        button->setContextMenuPolicy(Qt::CustomContextMenu);
        button->setToolTip(tr("Click to send \"%1\" to the serial port.\nRight-click to edit, move, copy or delete.")
                               .arg(m_commands[i].command));
        connect(button, &QPushButton::clicked, this, [this, i] { sendCommand(i); });
        connect(button, &QPushButton::customContextMenuRequested, this, [this, button, i](const QPoint &pos) {
            showMenu(i, button->mapToGlobal(pos));
        });
        connect(row, &QWidget::customContextMenuRequested, this, [this, row, i](const QPoint &pos) {
            showMenu(i, row->mapToGlobal(pos));
        });
        auto *name = new QLabel(QStringLiteral("- ") + m_commands[i].name);
        name->setObjectName(QStringLiteral("commandName"));
        rowLayout->addWidget(button);
        rowLayout->addWidget(name, 1);
        m_rows->addWidget(row);
    }
    m_rows->addStretch(1);
}

void SerialCommandGuide::sendCommand(int index)
{
    if (index < 0 || index >= m_commands.size())
        return;
    const Command &c = m_commands[index];
    const QString error = m_sender(c.command.toUtf8(), c.name);
    if (error.isEmpty())
        setStatus(tr("Sent \"%1\"  (%2)").arg(c.command, c.name), false);
    else
        setStatus(tr("Not sent: %1").arg(error), true);
}

void SerialCommandGuide::setStatus(const QString &text, bool isError)
{
    m_status->setText(text);
    m_status->setProperty("error", isError);
    m_status->style()->unpolish(m_status);
    m_status->style()->polish(m_status);
}

void SerialCommandGuide::showMenu(int index, const QPoint &globalPos)
{
    if (!m_loadError.isEmpty() || index < 0 || index >= m_commands.size())
        return;
    QMenu menu(this);
    QAction *copy = menu.addAction(tr("Copy Command"));
    QAction *edit = menu.addAction(tr("Edit..."));
    QAction *up = menu.addAction(tr("Move Up"));
    QAction *down = menu.addAction(tr("Move Down"));
    menu.addSeparator();
    QAction *del = menu.addAction(tr("Delete"));
    up->setEnabled(index > 0);
    down->setEnabled(index + 1 < m_commands.size());
    QAction *chosen = menu.exec(globalPos);
    // Deferred: the rows are rebuilt (deleted) by these.
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
